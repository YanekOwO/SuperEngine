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
#include "core/gameexe.hpp"
#include "core/input.hpp"
#include "core/interaction_manager.hpp"
#include "core/kidoku_table.hpp"
#include "core/mwnd_config.hpp"
#include "core/stage.hpp"
#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/flow.hpp"
#include "libsiglus/bindings/loader.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/gexedat.hpp"
#include "libsiglus/intern_name.hpp"
#include "libsiglus/mask.hpp"
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
#include "vm/instruction.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <array>
#include <boost/algorithm/string/predicate.hpp>
#include <chrono>
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
namespace chr = std::chrono;
namespace fs = std::filesystem;
namespace sr = serilang;
namespace sb = srbind;

static DomainLogger logger("SiglusFactory");

namespace {}  // namespace

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

SiglusRuntime SGVMFactory::Create() {
  SiglusRuntime rt;
  rt.platform_implementor = platform_implementor_;
  rt.vm = std::make_unique<sr::VM>(m6::VMFactory::Create());
  sr::VM& vm = *rt.vm;
  std::shared_ptr<sr::GarbageCollector> gc = vm.gc_;

  fs::path seen_path = CorrectPathCase(base_path_ / "scene.pck");
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
  gexe.parseLine("#SCREENSIZE_MOD=999,1920,1080");
  MwndConfig mwnd_config = MwndConfig::ParseSiglus(gexe);
  const int default_window = mwnd_config.default_window();

  // Init sdl system
  SystemOptions system_options;
  system_options.fast_forward = fast_forward_;
  rt.system = std::make_unique<System>(gexe, rt.asset_scanner,
                                       std::move(mwnd_config), system_options);
  rt.system->text().set_active_window(default_window);
  rt.stage = std::make_unique<Stage>(rt.system->graphics().GetObjectLayerSize(),
                                     gexe("EFFECT.CNT").Int().value_or(0));
  const int mask_count = gexe("MASK.CNT").Int().value_or(16);
  if (mask_count < 0 || mask_count > 256)
    throw std::runtime_error("MASK.CNT must be between 0 and 256");
  rt.mask_list = std::make_shared<MaskList>(
      static_cast<std::size_t>(mask_count), rt.system->event().GetClock());
  rt.renderer = std::make_shared<SiglusSceneRenderer>(*rt.stage, *rt.system,
                                                      rt.mask_list);
  rt.system->graphics().BindSceneRenderer(rt.renderer);

  rt.local_config = std::make_shared<Gameexe>();
  rt.global_config = std::make_shared<Gameexe>();
  rt.kidoku_table = std::make_shared<KidokuTable>();

  rt.system->text().SetMwndCallHandler(
      [loader = rt.loader.get(), &vm](const MwndConfig::CallTarget& target) {
        sr::Code* thunk = nullptr;
        if (target.command) {
          thunk = binding::MakeSceneEntryThunk(vm, *loader, target.scene,
                                               *target.command);
        } else {
          thunk = binding::MakeSceneEntryThunk(
              vm, *loader, target.scene,
              GetZlabelId(target.entrypoint.value_or(0)));
        }
        vm.AddFiber(thunk);
      });

  rt.system->text().SetMwndActionHandler(
      [local = rt.local_config, global = rt.global_config,
       gameexe = rt.gameexe](const MwndConfig::Button& button, bool execute) {
        using Action = MwndConfig::ButtonAction;
        const auto key = [&](std::string_view root) {
          return std::format("{}.{:03}", root, button.action_option);
        };
        switch (button.action) {
          case Action::LocalSwitch:
            if (execute)
              local->SetIntAt(key("LOCAL_EXTRA_SWITCH"), button.mode == 0);
            return true;
          case Action::GlobalSwitch:
            if (execute) {
              const std::string name = key("GLOBAL_EXTRA_SWITCH");
              global->SetIntAt(name, !(*global)(name).Int().value_or(0));
            }
            return true;
          case Action::LocalMode:
          case Action::GlobalMode: {
            if (!execute)
              return true;
            const bool is_global = button.action == Action::GlobalMode;
            Gameexe& store = is_global ? *global : *local;
            const std::string name =
                key(is_global ? "GLOBAL_EXTRA_MODE" : "LOCAL_EXTRA_MODE");
            const std::string count_key = std::format(
                "{}.{}.{:03}.ITEM_CNT", is_global ? "DIALOG" : "SYSCOMMENU",
                is_global ? "GLOBAL_EXTRA_MODE" : "LOCAL_EXTRA_MODE",
                button.action_option);
            const int count =
                std::max(1, (*gameexe)(count_key).Int().value_or(1));
            store.SetIntAt(name, (store(name).Int().value_or(0) + 1) % count);
            return true;
          }
          default:
            return false;
        }
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
  rt.system->event().AddListener(20, rt.system_event_listener);

  for (auto it = binding::SiglusBindingRegistry::cbegin();
       it != binding::SiglusBindingRegistry::cend(); ++it) {
    it->second(rt);
  }
  std::shared_ptr<InputListener> input = rt.input_event_listener;
  rt.interaction_manager = std::make_unique<InteractionManager>(
      *rt.stage, *input, ButtonActionTable::ParseSiglus(gexe));
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
  m.def("capture", [] {
    // TODO: create capture thumb image
  });

  // abuse the vm scheduler to refresh sdl regularly
  auto cb_holder = std::make_shared<std::function<void()>>();
  *cb_holder = [cb_holder, vm = rt.vm.get(), system = rt.system.get(), input,
                interaction_manager = rt.interaction_manager.get()]() {
    constexpr auto period =
        chr::duration_cast<chr::steady_clock::duration>(chr::seconds(1)) / 60;
    auto next = chr::steady_clock::now() + period;

    input->ResetState();
    system->Run([interaction_manager] { interaction_manager->Update(); });
    if (system->IsQuitRequested()) {
      vm->RequestStop();
      return;
    }

    vm->scheduler_.PushCallbackAt(*cb_holder, next);
  };
  rt.exec_sdl_callback = [cb_holder]() { (*cb_holder)(); };
  rt.vm->scheduler_.PushCallbackAfter(rt.exec_sdl_callback,
                                      chr::milliseconds(2));

  return rt;
}

}  // namespace libsiglus
