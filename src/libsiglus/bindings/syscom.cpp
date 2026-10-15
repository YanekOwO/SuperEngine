// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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

#include "libsiglus/bindings/registry.hpp"

#include "core/game_config.hpp"
#include "core/gameexe.hpp"
#include "core/input.hpp"
#include "core/mask.hpp"
#include "core/stage.hpp"
#include "libsiglus/bindings/flow.hpp"
#include "libsiglus/bindings/frame_action.hpp"
#include "libsiglus/bindings/util.hpp"
#include "platforms/implementor.hpp"
#include "srbind/srbind.hpp"
#include "systems/graphics_system.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"
#include "vm/exception.hpp"
#include "vm/function.hpp"
#include "vm/object.hpp"
#include "vm/promise.hpp"
#include "vm/value.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace libsiglus::binding {
namespace sb = srbind;
using namespace serilang;

namespace {

struct ReturnMenuParams {
  bool warning = false;
  bool se_play = false;
  bool fade_out = false;
  bool preserve_backlog = false;

  static ReturnMenuParams Parse(std::vector<Value> raw_args) {
    CallPacket packet = CallPacket::DecodeFrom(std::move(raw_args));
    if (packet.args.size() != 3) {
      throw RuntimeError(std::format(
          "return_to_menu expects 3 arguments, got {}", packet.args.size()));
    }

    ReturnMenuParams result{
        .warning = RequireInt(packet.args[0], "return_to_menu warning") != 0,
        .se_play = RequireInt(packet.args[1], "return_to_menu se_play") != 0,
        .fade_out = RequireInt(packet.args[2], "return_to_menu fade_out") != 0,
    };
    ForEachKeywordId(packet.kwargs, [&](int id, const Value& value) {
      if (id == 0) {
        result.preserve_backlog =
            RequireInt(value, "return_to_menu preserve backlog") != 0;
      }
    });
    return result;
  }
};

std::pair<std::string, int> ReadMenuScene(Gameexe& gameexe) {
  auto menu = gameexe("MENU_SCENE");
  std::string scene = menu.StrAt(0).value_or("_menu");
  int zlabel = menu.IntAt(1).value_or(0);
  if (scene.empty())
    throw RuntimeError("MENU_SCENE has an empty scene name");
  return {std::move(scene), zlabel};
}

void ResetLocalSyscomState(System* sys,
                           const std::shared_ptr<Gameexe>& gameexe,
                           LocalConfig& local) {
  local.syscommenu =
      gameexe ? MakeSiglusLocalConfig(*gameexe).syscommenu : SyscomMenuConfig{};
  local.mwnd_btn_disable_all = false;
  local.disable_mwnd_btn_touch = false;
  std::fill(local.mwnd_btn_disable.begin(), local.mwnd_btn_disable.end(),
            false);
  sys->text().mwnd_capture_window_.reset();
}

}  // namespace

void BindSyscom(SiglusMachine& runtime) {
  auto* sys = runtime.system.get();
  if (!sys)
    throw std::runtime_error("syscom bindings require a System");
  auto* stage = runtime.stage.get();
  auto* loader = runtime.loader.get();
  auto gameexe = runtime.gameexe;
  auto* local_config = &runtime.local_config;
  auto* global_config = &runtime.global_config;
  ResetLocalSyscomState(sys, gameexe, *local_config);
  runtime.reset_local_memory.emplace_back([sys, gameexe, local_config] {
    ResetLocalSyscomState(sys, gameexe, *local_config);
    sys->text().SetSkipMode(false);
  });
  const GlobalConfig defaults =
      gameexe ? MakeSiglusGlobalConfig(*gameexe) : GlobalConfig{};

  VM& vm = *runtime.vm;
  sb::module_ m(vm, "syscom");
  m.def("menu_enable", [sys] { sys->EnableSyscom(); });
  m.def("menu_disable", [sys] { sys->DisableSyscom(); });
  m.def("btn_enable_all",
        [&cfg = runtime.local_config] { cfg.mwnd_btn_disable_all = false; });
  m.def("btn_enable", [&cfg = runtime.local_config](int slot) {
    if (slot < 0)
      return;
    auto& disabled = cfg.mwnd_btn_disable;
    if (disabled.size() <= static_cast<std::size_t>(slot))
      disabled.resize(static_cast<std::size_t>(slot) + 1, false);
    disabled[slot] = false;
  });
  m.def("btn_disable_all",
        [local_config] { local_config->mwnd_btn_disable_all = true; });
  m.def("btn_disable", [local_config](int slot) {
    if (slot < 0)
      return;
    auto& disabled = local_config->mwnd_btn_disable;
    if (disabled.size() <= static_cast<std::size_t>(slot))
      disabled.resize(static_cast<std::size_t>(slot) + 1, false);
    disabled[slot] = true;
  });
  m.def("touch_enable",
        [local_config] { local_config->disable_mwnd_btn_touch = false; });
  m.def("touch_disable",
        [local_config] { local_config->disable_mwnd_btn_touch = true; });
  m.def("init_flags", [sys, gameexe, local_config] {
    ResetLocalSyscomState(sys, gameexe, *local_config);
  });

  m.def("set_readskip",
        [sys](int value) { sys->text().SetSkipMode(value != 0); });
  m.def("get_readskip", [sys] { return sys->text().skip_mode() ? 1 : 0; });
  m.def("set_enable_readskip", [local_config](int value) {
    local_config->syscommenu.read_skip.enable = value != 0;
  });
  m.def("get_enable_readskip", [local_config] {
    return local_config->syscommenu.read_skip.enable ? 1 : 0;
  });
  m.def("set_exist_readskip", [local_config](int value) {
    local_config->syscommenu.read_skip.exist = value != 0;
  });
  m.def("get_exist_readskip", [local_config] {
    return local_config->syscommenu.read_skip.exist ? 1 : 0;
  });
  m.def("is_readskip_enable", [sys, local_config] {
    const auto& entry = local_config->syscommenu.read_skip;
    return entry.exist && entry.enable && sys->text().kidoku_read() ? 1 : 0;
  });

  m.def("get_automode", [local_config, global_config] {
    return EffectiveAutoMode(*local_config, *global_config) ? 1 : 0;
  });
  m.def("set_enable_automode", [local_config](int value) {
    local_config->syscommenu.auto_mode.enable = value != 0;
  });
  m.def("get_enable_automode", [local_config] {
    return local_config->syscommenu.auto_mode.enable ? 1 : 0;
  });
  m.def("set_exist_automode", [local_config](int value) {
    local_config->syscommenu.auto_mode.exist = value != 0;
  });
  m.def("get_exist_automode", [local_config] {
    return local_config->syscommenu.auto_mode.exist ? 1 : 0;
  });
  m.def("is_automode_enable", [local_config] {
    const auto& entry = local_config->syscommenu.auto_mode;
    return entry.exist && entry.enable ? 1 : 0;
  });

  const auto bind_extra =
      [&](auto entries, std::string_view value_setter,
          std::string_view value_getter, std::string_view enable_setter,
          std::string_view enable_getter, std::string_view exist_setter,
          std::string_view exist_getter, std::string_view is_enabled) {
        using Entry = std::remove_reference_t<decltype((*entries)[0])>;
        const auto find = [entries](int id) -> Entry* {
          return id >= 0 && static_cast<std::size_t>(id) < entries->size()
                     ? &(*entries)[id]
                     : nullptr;
        };
        m.def(value_setter, [find](int id, int value) {
          if (auto* entry = find(id)) {
            if constexpr (std::is_same_v<Entry, ExtraSwitchConfig>)
              entry->onoff = value != 0;
            else
              entry->mode = value;
          }
        });
        m.def(value_getter, [find](int id) {
          auto* entry = find(id);
          if (!entry)
            return 0;
          if constexpr (std::is_same_v<Entry, ExtraSwitchConfig>)
            return entry->onoff ? 1 : 0;
          else
            return entry->mode;
        });
        m.def(enable_setter, [find](int id, int value) {
          if (auto* entry = find(id))
            entry->enable = value != 0;
        });
        m.def(enable_getter, [find](int id) {
          auto* entry = find(id);
          return entry && entry->enable ? 1 : 0;
        });
        m.def(exist_setter, [find](int id, int value) {
          if (auto* entry = find(id))
            entry->exist = value != 0;
        });
        m.def(exist_getter, [find](int id) {
          auto* entry = find(id);
          return entry && entry->exist ? 1 : 0;
        });
        m.def(is_enabled, [find](int id) {
          auto* entry = find(id);
          return entry && entry->exist && entry->enable ? 1 : 0;
        });
      };
  bind_extra(&local_config->syscommenu.local_extra_switch, "set_extraswitch",
             "get_extraswitch", "set_enable_extraswitch",
             "get_enable_extraswitch", "set_exist_extraswitch",
             "get_exist_extraswitch", "is_extraswitch_enable");
  bind_extra(&local_config->syscommenu.local_extra_mode, "set_localmode",
             "get_localmode", "set_enable_localmode", "get_enable_localmode",
             "set_exist_localmode", "get_exist_localmode",
             "is_localmode_enable");

  m.def(
      "return_to_menu",
      [sys, stage, loader, gameexe, &input = runtime.input_event_listener,
       &mask_list = runtime.mask_list, &platform = runtime.platform_implementor,
       &reset_local_memory = runtime.reset_local_memory,
       &frame_actions = runtime.frame_action_manager,
       &finish_active_wipe = runtime.finish_active_wipe](
          VM& vm, Fiber& fiber, std::vector<Value> args) {
        ReturnMenuParams params = ReturnMenuParams::Parse(std::move(args));
        if (!gameexe || !loader)
          throw RuntimeError("return_to_menu requires game configuration");

        if (params.warning && platform) {
          const std::string prompt =
              (*gameexe)("WARNINGINFO.RETURNMENU_WARNING_STR")
                  .Str()
                  .value_or("Return to the title screen?");
          if (!platform->AskUserPrompt(prompt, "", "Yes", "No")) {
            return;
          }
        }

        auto [scene_name, zlabel] = ReadMenuScene(*gameexe);
        serilang::Function* entry =
            ResolveSceneEntry(*loader, std::move(scene_name), zlabel);

        if (params.se_play && sys)
          sys->sound().PlaySe(6);

        if (finish_active_wipe && *finish_active_wipe)
          (*finish_active_wipe)();

        auto restart = [&vm, sys, stage, &input, &mask_list,
                        &reset_local_memory, &frame_actions,
                        &finish_active_wipe, entry,
                        preserve = params.preserve_backlog]() mutable {
          // Cancel timers before resetting the stage they refer to.
          if (finish_active_wipe && *finish_active_wipe)
            (*finish_active_wipe)();
          if (frame_actions)
            frame_actions->CancelGlobals();
          for (auto& cb : reset_local_memory)
            if (cb)
              cb();
          if (stage)
            stage->Reset();
          if (mask_list)
            mask_list->Reset();

          if (preserve) {
            sys->sound().Reset();
            sys->graphics().Reset();
            sys->EnableSyscom();
          } else {
            sys->Reset();
          }

          if (input)
            input->ResetState();
          vm.ScheduleExecute(entry);
        };

        fiber.frames.clear();
        if (params.fade_out && sys && !sys->ShouldFastForward()) {
          const int duration =
              std::max(0, (*gameexe)("LOAD.WIPE").IntAt(1).value_or(0));
          if (duration > 0) {
            sys->sound().BgmFadeOut(duration);
            auto faded = std::make_shared<Promise>();
            vm.TrackPendingPromise(faded);
            vm.scheduler_.PushCallbackAfter(
                [faded, restart = std::move(restart)]() mutable {
                  faded->Resolve(nil);
                  restart();
                },
                std::chrono::milliseconds(duration));
            return;
          }
        }
        restart();
      },
      sb::vararg);

  m.def("get_new_qsave_no", [] {
    // TODO: implement save/load
    return 0;
  });
  m.def("qsave", [](int, int, int) {
    // TODO: implement save/load
    return 0;
  });

  m.def("set_enable_hidemwnd", [local_config](bool value) {
    local_config->syscommenu.hide_mwnd.enable = value;
  });
  const auto add_global_bool =
      [&](bool GlobalSystemConfig::* field, std::string_view setter,
          std::string_view reset, std::string_view getter) {
        m.def(setter, [global_config, field](int value) {
          global_config->system.*field = value != 0;
        });
        m.def(reset, [global_config, field, initial = defaults.system.*field] {
          global_config->system.*field = initial;
        });
        m.def(getter, [global_config, field] {
          return global_config->system.*field ? 1 : 0;
        });
      };
  add_global_bool(&GlobalSystemConfig::disable_wipe_anime,
                  "set_no_wipe_anime_onoff", "set_no_wipe_anime_onoff_default",
                  "get_no_wipe_anime_onoff");
  add_global_bool(
      &GlobalSystemConfig::skip_wipe_anime, "set_skip_wipe_anime_onoff",
      "set_skip_wipe_anime_onoff_default", "get_skip_wipe_anime_onoff");
  m.def("set_automode", [global_config](int value) {
    global_config->enable_auto_mode = value != 0;
  });
  const auto apply_sound = [sys] { sys->sound().ApplyConfiguration(); };
  const auto add_sound = [&](std::string_view name, SoundCategory category) {
    const unsigned index = SoundIndex(category);
    const std::string prefix(name);
    m.def("set_" + prefix + "_volume",
          [global_config, index, apply_sound](int value) {
            global_config->sound_user_volume[index] = std::clamp(value, 0, 255);
            apply_sound();
          });
    m.def("set_" + prefix + "_volume_default",
          [global_config, index, initial = defaults.sound_user_volume[index],
           apply_sound] {
            global_config->sound_user_volume[index] = initial;
            apply_sound();
          });
    m.def("get_" + prefix + "_volume", [global_config, index] {
      return global_config->sound_user_volume[index];
    });
    m.def("set_" + prefix + "_onoff",
          [global_config, index, apply_sound](int value) {
            global_config->play_sound_check[index] = value != 0;
            apply_sound();
          });
    m.def("set_" + prefix + "_onoff_default",
          [global_config, index, initial = defaults.play_sound_check[index],
           apply_sound] {
            global_config->play_sound_check[index] = initial;
            apply_sound();
          });
    m.def("get_" + prefix + "_onoff", [global_config, index] {
      return global_config->play_sound_check[index] ? 1 : 0;
    });
  };
  add_sound("bgm", SoundCategory::Bgm);
  add_sound("koe", SoundCategory::Voice);
  add_sound("pcm", SoundCategory::Pcm);
  add_sound("se", SoundCategory::Se);
  add_sound("mov", SoundCategory::Movie);
  m.def("set_all_volume", [global_config, apply_sound](int value) {
    global_config->all_sound_user_volume = std::clamp(value, 0, 255);
    apply_sound();
  });
  m.def("set_all_volume_default",
        [global_config, initial = defaults.all_sound_user_volume, apply_sound] {
          global_config->all_sound_user_volume = initial;
          apply_sound();
        });
  m.def("get_all_volume",
        [global_config] { return global_config->all_sound_user_volume; });
  m.def("set_all_onoff", [global_config, apply_sound](int value) {
    global_config->play_all_sound_check = value != 0;
    apply_sound();
  });
  m.def("set_all_onoff_default",
        [global_config, initial = defaults.play_all_sound_check, apply_sound] {
          global_config->play_all_sound_check = initial;
          apply_sound();
        });
  m.def("get_all_onoff", [global_config] {
    return global_config->play_all_sound_check ? 1 : 0;
  });
  m.def("set_koemode", [global_config, apply_sound](int value) {
    global_config->koe_mode = value;
    apply_sound();
  });
  m.def("set_koemode_default",
        [global_config, initial = defaults.koe_mode, apply_sound] {
          global_config->koe_mode = initial;
          apply_sound();
        });
  m.def("get_koemode", [global_config] { return global_config->koe_mode; });
  m.def("set_bgmfade_volume", [global_config, apply_sound](int value) {
    global_config->bgmfade_volume = std::clamp(value, 0, 255);
    apply_sound();
  });
  m.def("set_bgmfade_volume_default",
        [global_config, initial = defaults.bgmfade_volume, apply_sound] {
          global_config->bgmfade_volume = initial;
          apply_sound();
        });
  m.def("get_bgmfade_volume",
        [global_config] { return global_config->bgmfade_volume; });
  m.def("set_bgmfade_onoff", [global_config, apply_sound](int value) {
    global_config->bgmfade_use_check = value != 0;
    apply_sound();
  });
  m.def("set_bgmfade_onoff_default",
        [global_config, initial = defaults.bgmfade_use_check, apply_sound] {
          global_config->bgmfade_use_check = initial;
          apply_sound();
        });
  m.def("get_bgmfade_onoff",
        [global_config] { return global_config->bgmfade_use_check ? 1 : 0; });
}

RLVM_REGISTER(SiglusBindingRegistry, "4_syscom", BindSyscom)

}  // namespace libsiglus::binding
