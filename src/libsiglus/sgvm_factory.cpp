// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
// -----------------------------------------------------------------------

#include "libsiglus/sgvm_factory.hpp"

#include "core/asset_scanner.hpp"
#include "core/button_action_table.hpp"
#include "core/capture.hpp"
#include "core/counter.hpp"
#include "core/game_config.hpp"
#include "core/gameexe.hpp"
#include "core/input.hpp"
#include "core/interaction_manager.hpp"
#include "core/kidoku_table.hpp"
#include "core/mask.hpp"
#include "core/mwnd_config.hpp"
#include "core/stage.hpp"
#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/flow.hpp"
#include "libsiglus/bindings/frame_action.hpp"
#include "libsiglus/bindings/loader.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/gexedat.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "libsiglus/siglus_scene_renderer.hpp"
#include "log/domain_logger.hpp"
#include "m6/vm_factory.hpp"
#include "srbind/module.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"
#include "utilities/file.hpp"
#include "utilities/mapped_file.hpp"
#include "utilities/string_utilities.hpp"
#include "vm/function.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <boost/algorithm/string/predicate.hpp>
#include <filesystem>
#include <format>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace libsiglus {
namespace fs = std::filesystem;
namespace sr = serilang;
namespace sb = srbind;

static DomainLogger logger("SiglusFactory");

// Load Gameexe.ini config
static Gameexe LoadGameexe(std::shared_ptr<AssetScanner> scanner) {
  auto pth = scanner->FindFile("Gameexe", {"dat", "ini"});
  if (!pth.has_value()) {
    logger(Severity::Error) << "Gameexe.dat not found: " << pth.error().what();
    return {};
  }

  if (pth->extension() == ".ini") {
    auto gexe = Gameexe::FromFile(pth.value());
    if (gexe.has_value())
      return gexe.value();
    logger(Severity::Error) << "Error while loading " << pth->string() << ": "
                            << gexe.error().message;
  } else {
    try {
      return CreateGexe(pth.value());
    } catch (std::exception& e) {
      logger(Severity::Error)
          << "Error while loading " << pth->string() << ": " << e.what();
    }
  }
  return {};
}

static std::shared_ptr<CaptureManager> CreateCaptureManager(
    SiglusMachine& runtime) {
  auto& graphics = runtime.system->graphics();
  const auto screen_size = graphics.screen_size();
  auto captures = std::make_shared<CaptureManager>(
      CaptureConfig::Parse(*runtime.gameexe, screen_size), runtime.save_pth,
      [&graphics, renderer = runtime.renderer,
       screen_size](const CaptureRequest& request) {
        const bool buffer = request.kind == CaptureKind::Buffer;
        return graphics.RenderToSurface(
            buffer ? request.size : screen_size,
            [&] { renderer->RenderCapture(request.end); }, buffer,
            request.offset);
      },
      [&runtime, &graphics](fs::path path) {
        std::string name = path.string();
        std::replace(name.begin(), name.end(), '\\', '/');
        path = fs::path(name);
        path = CorrectPath(
            path.is_absolute() ? path.root_path() : runtime.base_pth,
            path.relative_path().string());
        if (path.empty()) {
          const auto found = runtime.asset_scanner->FindFile(
              name, {"g00", "pdt", "png", "bmp"});
          if (!found)
            throw found.error();
          path = *found;
        }
        return graphics.GetBackend()->LoadSurface(path);
      },
      [&graphics] {
        std::vector<std::string> names;
        for (const auto& [name, image] : graphics.loaded_images())
          if (!image.expired())
            names.push_back(name);
        return names;
      });
  const auto table = (*runtime.gameexe)("THUMBTABLE_FILE").Str();
  if (table && !table->empty()) {
    auto name = fs::path(*table).replace_extension("dat").string();
    const auto path = runtime.asset_scanner->FindFile(name, {"dat"});
    if (!path)
      throw path.error();
    captures->LoadThumbnailTable(*path);
  }
  return captures;
}

std::unique_ptr<Stage> SGVMFactory::CreateStage(Gameexe& gexe) {
  const int obj_count = gexe("OBJECT.CNT").Int().value_or(256);
  const int effect_cnt = gexe("EFFECT.CNT").Int().value_or(0);
  const int quake_count = gexe("QUAKE.CNT").Int().value_or(16);
  return std::make_unique<Stage>(obj_count, effect_cnt, 0, quake_count);
}

std::unique_ptr<SiglusMachine> SGVMFactory::Create() {
  std::unique_ptr<SiglusMachine> ret = std::make_unique<SiglusMachine>();
  SiglusMachine& rt = *ret;

  rt.platform_implementor = platform_implementor_;
  rt.vm = std::make_unique<sr::VM>(m6::VMFactory::Create());
  sr::VM& vm = *rt.vm;
  std::shared_ptr<sr::GarbageCollector> gc = vm.gc_;

  fs::path seen_path = CorrectPath(base_path_, "scene.pck");
  MappedFile archive_mf(seen_path);
  rt.archive = std::make_shared<Archive>(Archive::Create(archive_mf.Read()));
  rt.loader = std::make_unique<binding::Loader>(*rt.archive, vm, debug_);

  rt.base_pth = base_path_;
  rt.save_pth = rt.base_pth / "save";
  rt.asset_scanner = std::make_shared<AssetScanner>();
  rt.asset_scanner->IndexDirectory(rt.base_pth);

  rt.gameexe = std::make_shared<Gameexe>(LoadGameexe(rt.asset_scanner));
  Gameexe& gexe = *rt.gameexe;
  gexe.SetStringAt("CAPTION", "SiglusTest");
  gexe.SetStringAt("REGNAME", "sjis: SIGLUS\\TEST");
  gexe.SetIntAt("NAME_ENC", 0);
  gexe.SetIntAt("SUBTITLE", 0);
  gexe.SetIntAt("MOUSE_CURSOR", 0);
  gexe.SetStringAt("__GAMEPATH", base_path_.string());
  MwndConfig mwnd_config = MwndConfig::ParseSiglus(gexe);
  const int default_window = mwnd_config.default_window();

  rt.stage = CreateStage(gexe);

  rt.local_config = MakeSiglusLocalConfig(gexe);
  rt.global_config = MakeSiglusGlobalConfig(gexe);

  // Init sdl system
  SystemOptions system_options;
  system_options.engine = SystemOptions::Engine::Siglus;
  system_options.fast_forward = fast_forward_;
  rt.system = std::make_unique<System>(gexe, *rt.stage, rt.local_config,
                                       rt.global_config, rt.asset_scanner,
                                       std::move(mwnd_config), system_options);
  rt.system->text().set_active_window(default_window);
  rt.screen_quakes =
      std::make_shared<QuakeList>(rt.stage->quakes.Size(kLayerFg));

  int counter_count = gexe("COUNTER.CNT").Int().value_or(16);
  if (counter_count < 0)
    counter_count = 0;
  rt.counter_list =
      std::make_shared<CounterList>(static_cast<std::size_t>(counter_count));

  int mask_count = gexe("MASK.CNT").Int().value_or(16);
  if (mask_count < 0)
    mask_count = 0;
  rt.mask_list =
      std::make_shared<MaskList>(static_cast<std::size_t>(mask_count));
  rt.renderer = std::make_shared<SiglusSceneRenderer>(
      *rt.stage, *rt.system, rt.mask_list, rt.screen_quakes, rt.local_config);
  rt.system->graphics().BindSceneRenderer(rt.renderer);
  rt.captures = CreateCaptureManager(rt);
  rt.reset_local_memory.emplace_back([&rt] { rt.ResetCaptures(); });

  rt.kidoku_table = std::make_shared<KidokuTable>();

  int frame_action_count = gexe("FRAME_ACTION_CH.CNT").Int().value_or(4);
  frame_action_count = std::clamp(frame_action_count, 0, 256);
  rt.frame_action_manager = std::make_shared<binding::FrameActionManager>(
      vm, *rt.stage, rt.local_config,
      static_cast<std::size_t>(frame_action_count));

  rt.system->text().SetMwndFrameActionHandler(
      [loader = rt.loader.get(), manager = rt.frame_action_manager](
          std::shared_ptr<GraphicsObject> object,
          const MwndConfig::CallTarget& target) {
        manager->StartConfiguredButton(*loader, std::move(object), target);
      });

  rt.system->text().SetMwndCallHandler(
      [loader = rt.loader.get(), &vm](const MwndConfig::CallTarget& target) {
        sr::Function* entry;
        if (target.command) {
          entry = binding::ResolveSceneCommand(*loader, target.scene,
                                               *target.command);
        } else {
          entry = binding::ResolveSceneEntry(*loader, target.scene,
                                             target.entrypoint.value_or(0));
        }
        vm.ScheduleExecute(entry);
      });

  rt.system->text().SetMwndActionHandler(
      [local = &rt.local_config, global = &rt.global_config,
       gameexe = rt.gameexe](const MwndConfig::Button& button, bool execute) {
        using Action = MwndConfig::ButtonAction;
        const int id = button.action_option;
        if (id < 0)
          return false;
        const auto mode_count = [&](bool is_global) {
          return std::max(
              1, (*gameexe)(std::format("{}.{}.{:03}.ITEM_CNT",
                                        is_global ? "DIALOG" : "SYSCOMMENU",
                                        is_global ? "GLOBAL_EXTRA_MODE"
                                                  : "LOCAL_EXTRA_MODE",
                                        id))
                     .Int()
                     .value_or(1));
        };
        switch (button.action) {
          case Action::LocalSwitch:
            if (static_cast<std::size_t>(id) >=
                local->syscommenu.local_extra_switch.size())
              return false;
            if (execute)
              local->syscommenu.local_extra_switch[id].onoff = button.mode == 0;
            return true;
          case Action::GlobalSwitch:
            if (static_cast<std::size_t>(id) >=
                global->global_extra_switch_flag.size())
              return false;
            if (execute)
              global->global_extra_switch_flag[id] =
                  !global->global_extra_switch_flag[id];
            return true;
          case Action::LocalMode:
            if (static_cast<std::size_t>(id) >=
                local->syscommenu.local_extra_mode.size())
              return false;
            if (execute) {
              auto& mode = local->syscommenu.local_extra_mode[id].mode;
              mode = (mode + 1) % mode_count(false);
            }
            return true;
          case Action::GlobalMode:
            if (static_cast<std::size_t>(id) >=
                global->global_extra_mode_flag.size())
              return false;
            if (execute) {
              auto& mode = global->global_extra_mode_flag[id];
              mode = (mode + 1) % mode_count(true);
            }
            return true;
          default:
            return false;
        }
      });

  rt.system->text().SetMwndActionStatusHandler(
      [local = &rt.local_config,
       global = &rt.global_config](const MwndConfig::Button& button) {
        using Action = MwndConfig::ButtonAction;
        TextSystem::MwndActionStatus status;
        const int id = button.action_option;
        if (id < 0)
          return status;
        switch (button.action) {
          case Action::LocalSwitch:
            if (static_cast<std::size_t>(id) <
                local->syscommenu.local_extra_switch.size()) {
              const auto& entry = local->syscommenu.local_extra_switch[id];
              status.supported = true;
              status.exists = entry.exist;
              status.enabled = entry.enable;
              status.mode = entry.onoff;
            }
            break;
          case Action::LocalMode:
            if (static_cast<std::size_t>(id) <
                local->syscommenu.local_extra_mode.size()) {
              const auto& entry = local->syscommenu.local_extra_mode[id];
              status.supported = true;
              status.exists = entry.exist;
              status.enabled = entry.enable;
              status.mode = entry.mode;
            }
            break;
          case Action::GlobalSwitch:
            if (static_cast<std::size_t>(id) <
                global->global_extra_switch_flag.size()) {
              status.supported = true;
              status.mode = global->global_extra_switch_flag[id];
            }
            break;
          case Action::GlobalMode:
            if (static_cast<std::size_t>(id) <
                global->global_extra_mode_flag.size()) {
              status.supported = true;
              status.mode = global->global_extra_mode_flag[id];
            }
            break;
          default:
            break;
        }
        return status;
      });

  struct SystemEventListener : public EventListener {
    sr::VM& vm;
    System& sys;
    SystemEventListener(sr::VM& v, System& s) : vm(v), sys(s) {}
    void OnEvent(std::shared_ptr<Event> event) override {
      if (std::visit(
              [&](auto& event) -> bool {
                using T = std::decay_t<decltype(event)>;
                if constexpr (std::same_as<T, Quit>) {
                  vm.RequestStop();
                  return true;
                }
                if constexpr (std::same_as<T, VideoExpose>) {
                  sys.graphics().ForceRefresh();
                  return true;
                }
                if constexpr (std::same_as<T, VideoResize>) {
                  sys.graphics().Resize(event.size);
                  return true;
                }
                if constexpr (std::same_as<T, MouseMotion>) {
                  event.pos = sys.graphics().DisplayToScreenPoint(event.pos);
                  return false;
                }
                return false;
              },
              *event))
        *event = std::monostate();
    }
  };
  rt.system_event_listener =
      std::make_shared<SystemEventListener>(vm, *rt.system);
  rt.system->event().AddListener(30, rt.system_event_listener);

  for (auto it = binding::SiglusBindingRegistry::cbegin();
       it != binding::SiglusBindingRegistry::cend(); ++it) {
    it->second(rt);
  }
  std::shared_ptr<InputListener> input = rt.input_event_listener;

  rt.interaction_manager = std::make_unique<InteractionManager>(
      *rt.stage, *input, ButtonActionTable::ParseSiglus(gexe));
  const auto button_config = [](const GraphicsObject& object, int slot) {
    auto config = object.Param().button.config.value_or(MwndConfig::Button{});
    config.slot_id = slot;
    config.action_no = object.Param().GetButtonAction();
    config.se_no = object.Param().GetButtonSe();
    return config;
  };
  rt.interaction_manager->SetMwndButtonHandlers(
      [text = &rt.system->text(), button_config](const GraphicsObject& object,
                                                 int slot) {
        const auto state = text->GetMwndButtonPresentation(
            button_config(object, slot), object.Param().button.call != nullptr);
        return InteractionManager::ButtonPresentation{
            state.visible, state.enabled, state.touch_enabled, state.activated};
      },
      [text = &rt.system->text(), &vm, button_config](
          std::shared_ptr<GraphicsObject> object, int slot,
          InteractionManager::MwndButtonEvent event) {
        using Event = InteractionManager::MwndButtonEvent;
        const auto config = button_config(*object, slot);
        switch (event) {
          case Event::Hit:
            text->PlayMwndButtonSound(config, TextSystem::MwndButtonSound::Hit);
            break;
          case Event::Push:
            text->PlayMwndButtonSound(config,
                                      TextSystem::MwndButtonSound::Push);
            break;
          case Event::Decide:
            text->PlayMwndButtonSound(config,
                                      TextSystem::MwndButtonSound::Decide);
            [[fallthrough]];
          case Event::Repeat:
            if (object->Param().button.call)
              vm.ScheduleExecute(object->Param().button.call);
            else
              text->ExecuteMwndButton(config);
            break;
        }
      },
      rt.system->event().GetClock());

  struct MwndButtonEventListener final : EventListener {
    InteractionManager& interaction;
    explicit MwndButtonEventListener(InteractionManager& manager)
        : interaction(manager) {}
    void OnEvent(std::shared_ptr<Event> event) override {
      if (event && interaction.HandleMwndButtonEvent(*event))
        *event = std::monostate();
    }
  };
  rt.mwnd_button_event_listener =
      std::make_shared<MwndButtonEventListener>(*rt.interaction_manager);
  rt.system->event().AddListener(18, rt.mwnd_button_event_listener);
  sb::module_ m(gc.get(), vm.globals_.get());

  m.def("__builtin_streq", [](sr::Value lhs, sr::Value rhs) -> sr::Value {
    const auto* lstr = lhs.Get_if<sr::String>();
    const auto* rstr = rhs.Get_if<sr::String>();
    if (lstr && rstr) {
      [[likely]] return boost::iequals(lstr->str_, rstr->str_);
    }
    return lhs.Hash() == rhs.Hash();
  });
  m.def("__builtin_dbgvalue", [](sr::VM& vm, sr::Value value) -> sr::Value {
    std::string s;
    if (const auto* str = value.Get_if<sr::String>())
      s = '"' + EncodeText(str->str_) + '"';
    else
      s = value.Str();
    return vm.gc_->Allocate<sr::String>(std::move(s));
  });
  m.def(
      "__builtin_dbgprint",
      [](std::vector<sr::Value> args) {
        for (const auto& it : args)
          std::cerr << it.Str();
        std::cerr << std::endl;
      },
      sb::vararg);

  m.def("savepoint", [] {
    // TODO: implement save/load and serialization support
    return 0;
  });

  return ret;
}

}  // namespace libsiglus
