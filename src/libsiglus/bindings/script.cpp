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

#include "core/gameexe.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "srbind/srbind.hpp"
#include "systems/system.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <array>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace libsiglus::binding {

namespace sb = srbind;
using namespace serilang;

void BindScript(SiglusMachine& rt) {
  if (!rt.local_config)
    rt.local_config = std::make_shared<Gameexe>();
  if (!rt.global_config)
    rt.global_config = std::make_shared<Gameexe>();
  auto lcfg = rt.local_config;

  auto& vm = *rt.vm;
  sb::module_ m(vm, "script");
  auto add_int_setter = [&](std::string_view name, std::string_view setter) {
    m.def(setter,
          [cfg = lcfg.get(), name](int value) { cfg->SetIntAt(name, value); });
  };
  auto add_int_getter = [&](std::string_view name, std::string_view getter) {
    m.def(getter, [cfg = lcfg.get(), name]() -> Value {
      auto result = (*cfg)(name).Int();
      return Value(result.value_or(0));
    });
  };
  auto add_int_cfg = [&](std::string_view name, std::string_view setter,
                         std::optional<std::string_view> getter = {}) {
    add_int_setter(name, setter);
    if (getter)
      add_int_getter(name, *getter);
  };
  auto add_int_cfg_with_default =
      [&](std::string_view name, std::string_view setter,
          std::string_view set_default, std::string_view getter,
          int default_value = -1) {
        add_int_cfg(name, setter, getter);
        m.def(set_default, [cfg = lcfg.get(), name, default_value] {
          cfg->SetIntAt(name, default_value);
        });
      };
  auto add_bool_cfg = [&](std::string_view name, std::string_view set_true,
                          std::string_view set_false,
                          std::optional<std::string_view> setter = {},
                          std::optional<std::string_view> getter = {}) {
    m.def(set_true, [cfg = lcfg.get(), name] { cfg->SetIntAt(name, 1); });
    m.def(set_false, [cfg = lcfg.get(), name] { cfg->SetIntAt(name, 0); });
    if (setter)
      add_int_setter(name, *setter);
    if (getter)
      add_int_getter(name, *getter);
  };
  auto add_flag_cfg = [&](std::string_view name, std::string_view setter,
                          std::string_view getter) {
    add_int_cfg(name, setter, getter);
  };

  add_bool_cfg("auto_savepoint", "set_auto_savepoint_on",
               "set_auto_savepoint_off");
  add_bool_cfg("skip_disable", "set_skip_disable", "set_skip_enable",
               "set_skip_disable_flag", "get_skip_disable_flag");
  add_bool_cfg("ctrl_skip", "set_ctrl_skip_disable", "set_ctrl_skip_enable",
               "set_ctrl_skip_disable_flag", "get_ctrl_skip_disable_flag");
  m.def("check_skip", [sys = rt.system.get()]() -> int {
    return sys && sys->ShouldFastForward() ? 1 : 0;
  });
  add_bool_cfg("not_stop_skip_by_click", "set_stop_skip_by_key_disable",
               "set_stop_skip_by_key_enable");
  add_bool_cfg("not_skip_msg_by_click", "set_end_msg_by_key_disable",
               "set_end_msg_by_key_enable");
  add_bool_cfg("skip_unread_message", "skip_unread_message_enable",
               "skip_unread_message_disable", "set_skip_unread_message_flag",
               "get_skip_unread_message_flag");
  add_bool_cfg("auto_mode", "start_auto_mode", "end_auto_mode");

  add_int_cfg_with_default("auto_mode_moji_wait", "set_auto_mode_moji_wait",
                           "set_auto_mode_moji_wait_default",
                           "get_auto_mode_moji_wait");
  add_int_cfg_with_default("auto_mode_min_wait", "set_auto_mode_min_wait",
                           "set_auto_mode_min_wait_default",
                           "get_auto_mode_min_wait");
  add_int_cfg("auto_mode_moji_cnt", "set_auto_mode_moji_cnt",
              "get_auto_mode_moji_cnt");

  add_int_cfg_with_default(
      "mouse_cursor_hide_onoff", "set_mouse_cursor_hide_onoff",
      "set_mouse_cursor_hide_onoff_default", "get_mouse_cursor_hide_onoff");
  add_int_cfg_with_default(
      "mouse_cursor_hide_time", "set_mouse_cursor_hide_time",
      "set_mouse_cursor_hide_time_default", "get_mouse_cursor_hide_time");

  add_int_cfg_with_default("message_speed", "set_message_speed",
                           "set_message_speed_default", "get_message_speed");

  add_bool_cfg("message_nowait", "message_nowait_on", "message_nowait_off",
               "set_message_nowait_flag", "get_message_nowait_flag");

  m.def("set_msg_async_mode_on",
        [cfg = lcfg.get()] { cfg->SetIntAt("msg_async_mode", 1); });
  m.def("set_msg_async_mode_on_once",
        [cfg = lcfg.get()] { cfg->SetIntAt("msg_async_mode", 2); });
  m.def("set_msg_async_mode_off",
        [cfg = lcfg.get()] { cfg->SetIntAt("msg_async_mode", 0); });
  add_bool_cfg("hide_mwnd_disable", "set_hide_mwnd_disable",
               "set_hide_mwnd_enable");
  add_bool_cfg("msg_back_disable", "set_msg_back_disable",
               "set_msg_back_enable");
  add_bool_cfg("msg_back_off", "set_msg_back_off", "set_msg_back_on");
  add_bool_cfg("msg_back_disp_off", "set_msg_back_disp_off",
               "set_msg_back_disp_on");
  add_bool_cfg("mouse_disp_off", "set_mouse_disp_off", "set_mouse_disp_on");
  add_bool_cfg("mouse_move_by_key_disable", "set_mouse_move_by_key_disable",
               "set_mouse_move_by_key_enable");

  m.def("set_key_disable", [cfg = lcfg.get()](int key) {
    auto it = (*cfg)("key_disable");
    if (!it.Exists())
      cfg->SetAt("key_disable", std::vector<GexeVal>(256, int(0)));
    it[key] = 1;
  });
  m.def("set_key_enable", [cfg = lcfg.get()](int key) {
    auto it = (*cfg)("key_disable");
    if (!it.Exists())
      cfg->SetAt("key_disable", std::vector<GexeVal>(256, int(0)));
    it[key] = 0;
  });

  add_flag_cfg("mwnd_anime_off", "set_mwnd_anime_off_flag",
               "get_mwnd_anime_off_flag");
  add_flag_cfg("mwnd_anime_on", "set_mwnd_anime_on_flag",
               "get_mwnd_anime_on_flag");
  add_flag_cfg("mwnd_disp_off", "set_mwnd_disp_off_flag",
               "get_mwnd_disp_off_flag");
  add_flag_cfg("koe_dont_stop_on", "set_koe_dont_stop_on_flag",
               "get_koe_dont_stop_on_flag");
  add_flag_cfg("koe_dont_stop_off", "set_koe_dont_stop_off_flag",
               "get_koe_dont_stop_off_flag");
  add_bool_cfg("shortcut_disable", "set_shortcut_disable",
               "set_shortcut_enable");

  add_flag_cfg("quake_stop", "set_quake_stop_flag", "get_quake_stop_flag");
  add_flag_cfg("emote_mouth_stop", "set_emote_mouth_stop_flag",
               "get_emote_mouth_stop_flag");
  add_bool_cfg("bgmfade", "start_bgmfade", "end_bgmfade");
  add_flag_cfg("vsync_wait_off", "set_vsync_wait_off_flag",
               "get_vsync_wait_off_flag");
  add_int_cfg("skip_trigger", "set_skip_trigger");
  add_bool_cfg("ignore_r", "ignore_r_on", "ignore_r_off");
  add_int_cfg("cursor_no", "set_cursor_no", "get_cursor_no");
  add_flag_cfg("time_stop", "set_time_stop_flag", "get_time_stop_flag");
  add_flag_cfg("counter_time_stop", "set_counter_time_stop_flag",
               "get_counter_time_stop_flag");
  add_flag_cfg("frame_action_time_stop", "set_frame_action_time_stop_flag",
               "get_frame_action_time_stop_flag");
  add_flag_cfg("stage_time_stop", "set_stage_time_stop_flag",
               "get_stage_time_stop_flag");

  // script excall
  add_int_cfg_with_default("font_bold", "set_font_bold",
                           "set_font_bold_default", "get_font_bold");
  add_int_cfg_with_default("font_shadow", "set_font_shadow",
                           "set_font_shadow_default", "get_font_shadow");

  m.def("get_font_name", [gc = vm.gc_.get(), cfg = lcfg.get()] {
    auto it = (*cfg)("font_name").Str();
    auto* str = gc->Allocate<String>(it.value_or(""));
    return Value(str);
  });
  m.def("set_font_name", [cfg = lcfg.get()](std::string font) {
    cfg->SetStringAt("font_name", std::move(font));
  });
  m.def("set_font_name_default",
        [cfg = lcfg.get()] { cfg->SetStringAt("font_name", ""); });

  rt.reset_local_memory.emplace_back([lcfg] {
    constexpr std::array<std::string_view, 4> kTimeStopFlags{
        "time_stop", "counter_time_stop", "frame_action_time_stop",
        "stage_time_stop"};
    for (std::string_view flag : kTimeStopFlags)
      lcfg->SetIntAt(flag, 0);
  });
}

RLVM_REGISTER(SiglusBindingRegistry, "3_script", BindScript);

}  // namespace libsiglus::binding
