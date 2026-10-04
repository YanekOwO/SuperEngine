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
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/intern_name.hpp"
#include "platforms/implementor.hpp"
#include "srbind/srbind.hpp"
#include "systems/graphics_system.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"
#include "vm/exception.hpp"
#include "vm/object.hpp"
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

}  // namespace

void BindSyscom(SiglusRuntime& runtime) {
  auto* sys = runtime.system.get();
  auto* stage = runtime.stage.get();
  auto* loader = runtime.loader.get();
  auto gameexe = runtime.gameexe;
  auto input = runtime.input_event_listener;
  auto mask_list = runtime.mask_list;
  auto platform = runtime.platform_implementor;
  auto reset_local_memory = runtime.reset_local_memory;

  VM& vm = *runtime.vm;
  sb::module_ m(vm, "syscom");
  m.def("menu_enable", [sys] { sys->EnableSyscom(); });
  m.def("menu_disable", [sys] { sys->DisableSyscom(); });

  m.def(
      "return_to_menu",
      [sys, stage, loader, gameexe, input, mask_list, platform,
       reset_local_memory](VM& vm, Fiber& fiber, std::vector<Value> args) {
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
        Code* thunk = MakeSceneEntryThunk(vm, *loader, std::move(scene_name),
                                          GetZlabelId(zlabel));

        if (params.se_play && sys)
          sys->sound().PlaySe(6);

        auto restart = [sys, stage, input, mask_list, reset_local_memory, &vm,
                        thunk, preserve = params.preserve_backlog] {
          if (reset_local_memory)
            reset_local_memory();
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
          vm.AddFiber(thunk);
        };

        fiber.frames.clear();
        if (params.fade_out && sys && !sys->ShouldFastForward()) {
          const int duration =
              std::max(0, (*gameexe)("LOAD.WIPE").IntAt(1).value_or(0));
          if (duration > 0) {
            sys->sound().BgmFadeOut(duration);
            vm.scheduler_.PushCallbackAfter(
                std::move(restart), std::chrono::milliseconds(duration));
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
  m.def("get_no_wipe_anime_onoff", [] {
    // TODO
    return 0;
  });
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

RLVM_REGISTER(SiglusBindingRegistry, "syscom", BindSyscom)

}  // namespace libsiglus::binding
