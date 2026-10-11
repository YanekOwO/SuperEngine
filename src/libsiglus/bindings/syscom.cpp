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

#include <chrono>
#include <format>
#include <memory>
#include <string>
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

std::string LocalSyscomKey(std::string_view root, int id) {
  return std::format("{}.{:03}", root, id);
}

void ApplyMwndSyscomDefaults(System* sys,
                             const std::shared_ptr<Gameexe>& gameexe) {
  if (!sys || !gameexe)
    return;
  TextSystem& text = sys->text();
  auto readskip = (*gameexe)("SYSCOMMENU.READ_SKIP");
  text.SetReadSkipButtonAvailable(readskip.IntAt(0).value_or(1) != 0,
                                  readskip.IntAt(1).value_or(1) != 0);
  auto automode = (*gameexe)("SYSCOMMENU.AUTO_MODE");
  text.SetAutoModeButtonAvailable(automode.IntAt(0).value_or(1) != 0,
                                  automode.IntAt(1).value_or(1) != 0);
  text.ResetMwndButtonPolicy();
}

void ResetLocalSyscomState(System* sys,
                           const std::shared_ptr<Gameexe>& gameexe,
                           const std::shared_ptr<Gameexe>& local) {
  ApplyMwndSyscomDefaults(sys, gameexe);
  if (!gameexe || !local)
    return;
  for (int id = 0; id < 256; ++id) {
    auto switch_source = (*gameexe)("SYSCOMMENU", "LOCAL_EXTRA_SWITCH", id);
    if (switch_source.Exists()) {
      const std::string key = LocalSyscomKey("LOCAL_EXTRA_SWITCH", id);
      local->SetIntAt(key, switch_source.IntAt(2).value_or(0));
      local->SetIntAt(key + ".EXIST", switch_source.IntAt(0).value_or(1));
      local->SetIntAt(key + ".ENABLE", switch_source.IntAt(1).value_or(1));
    }
    auto mode_source = (*gameexe)("SYSCOMMENU", "LOCAL_EXTRA_MODE", id);
    if (mode_source.Exists()) {
      const std::string key = LocalSyscomKey("LOCAL_EXTRA_MODE", id);
      local->SetIntAt(key, mode_source.IntAt(2).value_or(0));
      local->SetIntAt(key + ".EXIST", mode_source.IntAt(0).value_or(1));
      local->SetIntAt(key + ".ENABLE", mode_source.IntAt(1).value_or(1));
    }
  }
}

}  // namespace

void BindSyscom(SiglusMachine& runtime) {
  auto* sys = runtime.system.get();
  auto* stage = runtime.stage.get();
  auto* loader = runtime.loader.get();
  auto gameexe = runtime.gameexe;
  auto local_config = runtime.local_config;
  auto input = runtime.input_event_listener;
  auto mask_list = runtime.mask_list;
  auto platform = runtime.platform_implementor;
  ResetLocalSyscomState(sys, gameexe, local_config);
  runtime.reset_local_memory.emplace_back([sys, gameexe, local_config] {
    ResetLocalSyscomState(sys, gameexe, local_config);
    if (sys)
      sys->text().SetSkipMode(false);
  });
  auto reset_local_memory = runtime.reset_local_memory;
  auto frame_actions = runtime.frame_action_manager;
  auto finish_active_wipe = runtime.finish_active_wipe;
  auto no_wipe_animation = runtime.no_wipe_animation;
  auto skip_wipe_animation = runtime.skip_wipe_animation;
  const bool default_no_wipe_animation =
      gameexe && (*gameexe)("SYSTEM.NO_WIPE_ANIME").Int().value_or(0) != 0;
  const bool default_skip_wipe_animation =
      !gameexe || (*gameexe)("SYSTEM.SKIP_WIPE_ANIME").Int().value_or(1) != 0;
  *no_wipe_animation = default_no_wipe_animation;
  *skip_wipe_animation = default_skip_wipe_animation;

  VM& vm = *runtime.vm;
  sb::module_ m(vm, "syscom");
  m.def("menu_enable", [sys] { sys->EnableSyscom(); });
  m.def("menu_disable", [sys] { sys->DisableSyscom(); });
  m.def("btn_enable_all", [sys] {
    if (sys)
      sys->text().SetMwndButtonsEnabled(true);
  });
  m.def("btn_enable", [sys](int slot) {
    if (sys)
      sys->text().SetMwndButtonEnabled(slot, true);
  });
  m.def("btn_disable_all", [sys] {
    if (sys)
      sys->text().SetMwndButtonsEnabled(false);
  });
  m.def("btn_disable", [sys](int slot) {
    if (sys)
      sys->text().SetMwndButtonEnabled(slot, false);
  });
  m.def("touch_enable", [sys] {
    if (sys)
      sys->text().SetMwndButtonTouchEnabled(true);
  });
  m.def("touch_disable", [sys] {
    if (sys)
      sys->text().SetMwndButtonTouchEnabled(false);
  });
  m.def("init_flags", [sys, gameexe, local_config] {
    ResetLocalSyscomState(sys, gameexe, local_config);
  });

  m.def("set_readskip", [sys](int value) {
    if (sys)
      sys->text().SetSkipMode(value != 0);
  });
  m.def("get_readskip",
        [sys] { return sys && sys->text().skip_mode() ? 1 : 0; });
  m.def("set_enable_readskip", [sys](int value) {
    if (sys) {
      auto& text = sys->text();
      text.SetReadSkipButtonAvailable(text.readskip_button_exists(),
                                      value != 0);
    }
  });
  m.def("get_enable_readskip",
        [sys] { return sys && sys->text().readskip_button_enabled() ? 1 : 0; });
  m.def("set_exist_readskip", [sys](int value) {
    if (sys) {
      auto& text = sys->text();
      text.SetReadSkipButtonAvailable(value != 0,
                                      text.readskip_button_enabled());
    }
  });
  m.def("get_exist_readskip",
        [sys] { return sys && sys->text().readskip_button_exists() ? 1 : 0; });
  m.def("is_readskip_enable", [sys] {
    if (!sys)
      return 0;
    auto& text = sys->text();
    return text.readskip_button_exists() && text.readskip_button_enabled() &&
                   text.kidoku_read()
               ? 1
               : 0;
  });

  m.def("get_automode",
        [sys] { return sys && sys->text().auto_mode() ? 1 : 0; });
  m.def("set_enable_automode", [sys](int value) {
    if (sys) {
      auto& text = sys->text();
      text.SetAutoModeButtonAvailable(text.automode_button_exists(),
                                      value != 0);
    }
  });
  m.def("get_enable_automode",
        [sys] { return sys && sys->text().automode_button_enabled() ? 1 : 0; });
  m.def("set_exist_automode", [sys](int value) {
    if (sys) {
      auto& text = sys->text();
      text.SetAutoModeButtonAvailable(value != 0,
                                      text.automode_button_enabled());
    }
  });
  m.def("get_exist_automode",
        [sys] { return sys && sys->text().automode_button_exists() ? 1 : 0; });
  m.def("is_automode_enable", [sys] {
    return sys && sys->text().automode_button_exists() &&
                   sys->text().automode_button_enabled()
               ? 1
               : 0;
  });

  const auto set_local = [local_config](std::string_view root, int id,
                                        int value) {
    if (local_config && id >= 0)
      local_config->SetIntAt(LocalSyscomKey(root, id), value);
  };
  const auto get_local = [local_config](std::string_view root, int id) {
    if (!local_config || id < 0)
      return 0;
    return (*local_config)(LocalSyscomKey(root, id)).Int().value_or(0);
  };
  const auto set_local_field = [local_config](std::string_view root, int id,
                                              std::string_view field,
                                              int value) {
    if (local_config && id >= 0)
      local_config->SetIntAt(
          LocalSyscomKey(root, id) + "." + std::string(field), value);
  };
  const auto get_local_field = [local_config](std::string_view root, int id,
                                              std::string_view field) {
    if (!local_config || id < 0)
      return 0;
    return (*local_config)(LocalSyscomKey(root, id) + "." + std::string(field))
        .Int()
        .value_or(0);
  };
  m.def("set_extraswitch", [set_local](int id, int value) {
    set_local("LOCAL_EXTRA_SWITCH", id, value != 0);
  });
  m.def("get_extraswitch", [get_local](int id) {
    return get_local("LOCAL_EXTRA_SWITCH", id) != 0 ? 1 : 0;
  });
  m.def("set_enable_extraswitch", [set_local_field](int id, int value) {
    set_local_field("LOCAL_EXTRA_SWITCH", id, "ENABLE", value != 0);
  });
  m.def("get_enable_extraswitch", [get_local_field](int id) {
    return get_local_field("LOCAL_EXTRA_SWITCH", id, "ENABLE") != 0 ? 1 : 0;
  });
  m.def("set_exist_extraswitch", [set_local_field](int id, int value) {
    set_local_field("LOCAL_EXTRA_SWITCH", id, "EXIST", value != 0);
  });
  m.def("get_exist_extraswitch", [get_local_field](int id) {
    return get_local_field("LOCAL_EXTRA_SWITCH", id, "EXIST") != 0 ? 1 : 0;
  });
  m.def("is_extraswitch_enable", [get_local_field](int id) {
    return get_local_field("LOCAL_EXTRA_SWITCH", id, "EXIST") != 0 &&
                   get_local_field("LOCAL_EXTRA_SWITCH", id, "ENABLE") != 0
               ? 1
               : 0;
  });
  m.def("set_localmode", [set_local](int id, int value) {
    set_local("LOCAL_EXTRA_MODE", id, value);
  });
  m.def("get_localmode",
        [get_local](int id) { return get_local("LOCAL_EXTRA_MODE", id); });
  m.def("set_enable_localmode", [set_local_field](int id, int value) {
    set_local_field("LOCAL_EXTRA_MODE", id, "ENABLE", value != 0);
  });
  m.def("get_enable_localmode", [get_local_field](int id) {
    return get_local_field("LOCAL_EXTRA_MODE", id, "ENABLE") != 0 ? 1 : 0;
  });
  m.def("set_exist_localmode", [set_local_field](int id, int value) {
    set_local_field("LOCAL_EXTRA_MODE", id, "EXIST", value != 0);
  });
  m.def("get_exist_localmode", [get_local_field](int id) {
    return get_local_field("LOCAL_EXTRA_MODE", id, "EXIST") != 0 ? 1 : 0;
  });
  m.def("is_localmode_enable", [get_local_field](int id) {
    return get_local_field("LOCAL_EXTRA_MODE", id, "EXIST") != 0 &&
                   get_local_field("LOCAL_EXTRA_MODE", id, "ENABLE") != 0
               ? 1
               : 0;
  });

  m.def(
      "return_to_menu",
      [sys, stage, loader, gameexe, input, mask_list, platform,
       reset_local_memory, frame_actions,
       finish_active_wipe](VM& vm, Fiber& fiber, std::vector<Value> args) {
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

        auto restart = [&vm, sys, stage, input, mask_list, reset_local_memory,
                        frame_actions, finish_active_wipe, entry,
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
          if (sys) {
            if (preserve) {
              sys->sound().Reset();
              sys->graphics().Reset();
              sys->EnableSyscom();
            } else {
              sys->Reset();
            }
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

  m.def("set_enable_hidemwnd", [](bool) {
    // TODO
  });
  m.def("set_no_wipe_anime_onoff",
        [no_wipe_animation](int value) { *no_wipe_animation = value != 0; });
  m.def("set_no_wipe_anime_onoff_default",
        [no_wipe_animation, default_no_wipe_animation] {
          *no_wipe_animation = default_no_wipe_animation;
        });
  m.def("get_no_wipe_anime_onoff",
        [no_wipe_animation] { return *no_wipe_animation ? 1 : 0; });
  m.def("set_skip_wipe_anime_onoff", [skip_wipe_animation](int value) {
    *skip_wipe_animation = value != 0;
  });
  m.def("set_skip_wipe_anime_onoff_default",
        [skip_wipe_animation, default_skip_wipe_animation] {
          *skip_wipe_animation = default_skip_wipe_animation;
        });
  m.def("get_skip_wipe_anime_onoff",
        [skip_wipe_animation] { return *skip_wipe_animation ? 1 : 0; });
  m.def("set_automode", [sys](int val) -> void {
    if (!sys)
      return;
    sys->text().SetAutoMode(val);
  });
  m.def("set_all_volume_default", [sys]() -> void {
    if (!sys)
      return;
    auto& sound = sys->sound();
    sound.SetBgmVolumeMod(255);
    sound.SetKoeVolume(255, 0);
    sound.SetSeVolumeMod(255);
  });
  m.def("set_bgm_volume_default", [sys]() -> void {
    if (!sys)
      return;
    auto& sound = sys->sound();
    sound.SetBgmVolumeMod(255);
  });
  m.def("set_koe_volume_default", [sys]() -> void {
    if (!sys)
      return;
    auto& sound = sys->sound();
    sound.SetKoeVolume(255, 0);
  });
  m.def("set_koe_onoff_default", []() -> void {
    // TODO
  });
  m.def("set_bgm_onoff_default", [sys]() -> void {
    if (!sys)
      return;
    auto& sound = sys->sound();
    sound.SetBgmEnabled(1);
  });
  m.def("set_all_onoff_default", [] {
    // TODO
  });
  m.def("set_koemode_default", [] {
    // TODO
  });
  m.def("set_bgmfade_volume_default", [] {
    // TODO
  });
  m.def("set_bgmfade_onoff_default", [] {
    // TODO
  });
  m.def("set_pcm_volume_default", [] {
    // TODO
  });
  m.def("set_pcm_onoff_default", [] {
    // TODO
  });
  m.def("set_se_volume_default", [] {
    // TODO
  });
  m.def("set_se_onoff_default", [] {
    // TODO
  });
  m.def("set_mov_volume_default", [] {
    // TODO
  });
  m.def("set_mov_onoff_default", [] {
    // TODO
  });
  // TODO
  m.def("get_all_onoff", [] { return 1; });
  m.def("get_all_volume", [] { return 1; });
  m.def("get_bgm_onoff", [] { return 1; });
  m.def("get_bgm_volume", [] { return 1; });
  m.def("get_koe_onoff", [] { return 1; });
  m.def("get_koemode", [] { return 1; });
  m.def("get_koe_volume", [] { return 1; });
  m.def("get_bgmfade_onoff", [] { return 1; });
  m.def("get_bgmfade_volume", [] { return 1; });
  m.def("get_pcm_onoff", [] { return 1; });
  m.def("get_pcm_volume", [] { return 1; });
  m.def("get_se_onoff", [] { return 1; });
  m.def("get_se_volume", [] { return 1; });
  m.def("get_mov_onoff", [] { return 1; });
  m.def("get_mov_volume", [] { return 1; });
  m.def("get_all_onoff", [] { return 1; });
  m.def("get_bgm_onoff", [] { return 1; });
  m.def("get_koe_onoff", [] { return 1; });
  m.def("get_koemode", [] { return 1; });
  m.def("get_bgmfade_onoff", [] { return 1; });
  m.def("get_pcm_onoff", [] { return 1; });
  m.def("get_se_onoff", [] { return 1; });
  m.def("get_mov_onoff", [] { return 1; });
  m.def("get_mov_volume", [] { return 1; });
}

RLVM_REGISTER(SiglusBindingRegistry, "4_syscom", BindSyscom)

}  // namespace libsiglus::binding
