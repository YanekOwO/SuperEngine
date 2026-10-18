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

#include "core/game_config.hpp"
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
  auto* local = &rt.local_config;
  auto& vm = *rt.vm;
  sb::module_ m(vm, "script");
  const auto add_int = [&](int LocalConfig::* field, std::string_view setter,
                           std::string_view getter = {}) {
    m.def(setter, [local, field](int value) { local->*field = value; });
    if (!getter.empty())
      m.def(getter, [local, field] { return local->*field; });
  };
  const auto add_override = [&](int LocalConfig::* field,
                                std::string_view setter, std::string_view reset,
                                std::string_view getter) {
    add_int(field, setter, getter);
    m.def(reset, [local, field] { local->*field = -1; });
  };
  const auto add_bool = [&](bool LocalConfig::* field, std::string_view enable,
                            std::string_view disable,
                            std::string_view setter = {},
                            std::string_view getter = {}) {
    if (!enable.empty())
      m.def(enable, [local, field] { local->*field = true; });
    if (!disable.empty())
      m.def(disable, [local, field] { local->*field = false; });
    if (!setter.empty())
      m.def(setter, [local, field](int value) { local->*field = value != 0; });
    if (!getter.empty())
      m.def(getter, [local, field] { return local->*field ? 1 : 0; });
  };
  const auto add_flag = [&](bool LocalConfig::* field, std::string_view setter,
                            std::string_view getter) {
    add_bool(field, {}, {}, setter, getter);
  };

  add_bool(&LocalConfig::dont_set_save_point, "set_auto_savepoint_off",
           "set_auto_savepoint_on");
  add_bool(&LocalConfig::disable_skip, "set_skip_disable", "set_skip_enable",
           "set_skip_disable_flag", "get_skip_disable_flag");
  add_bool(&LocalConfig::disable_ctrl, "set_ctrl_skip_disable",
           "set_ctrl_skip_enable", "set_ctrl_skip_disable_flag",
           "get_ctrl_skip_disable_flag");
  m.def("check_skip", [sys = rt.system.get()] {
    return sys && sys->ShouldFastForward() ? 1 : 0;
  });
  add_bool(&LocalConfig::not_stop_skip_by_click, "set_stop_skip_by_key_disable",
           "set_stop_skip_by_key_enable");
  add_bool(&LocalConfig::not_skip_msg_by_click, "set_end_msg_by_key_disable",
           "set_end_msg_by_key_enable");
  add_bool(&LocalConfig::skip_unread_message, "skip_unread_message_enable",
           "skip_unread_message_disable", "set_skip_unread_message_flag",
           "get_skip_unread_message_flag");
  add_bool(&LocalConfig::auto_mode, "start_auto_mode", "end_auto_mode");
  add_override(&LocalConfig::auto_mode_moji_wait, "set_auto_mode_moji_wait",
               "set_auto_mode_moji_wait_default", "get_auto_mode_moji_wait");
  add_override(&LocalConfig::auto_mode_min_wait, "set_auto_mode_min_wait",
               "set_auto_mode_min_wait_default", "get_auto_mode_min_wait");
  add_int(&LocalConfig::auto_mode_moji_cnt, "set_auto_mode_moji_cnt",
          "get_auto_mode_moji_cnt");
  add_override(
      &LocalConfig::mouse_cursor_hide_onoff, "set_mouse_cursor_hide_onoff",
      "set_mouse_cursor_hide_onoff_default", "get_mouse_cursor_hide_onoff");
  add_override(
      &LocalConfig::mouse_cursor_hide_time, "set_mouse_cursor_hide_time",
      "set_mouse_cursor_hide_time_default", "get_mouse_cursor_hide_time");
  add_override(&LocalConfig::msg_speed, "set_message_speed",
               "set_message_speed_default", "get_message_speed");
  add_bool(&LocalConfig::msg_nowait, "message_nowait_on", "message_nowait_off",
           "set_message_nowait_flag", "get_message_nowait_flag");
  m.def("set_msg_async_mode_on", [local] {
    local->async_msg_mode = true;
    local->async_msg_mode_once = false;
  });
  m.def("set_msg_async_mode_on_once", [local] {
    local->async_msg_mode = true;
    local->async_msg_mode_once = true;
  });
  m.def("set_msg_async_mode_off", [local] {
    local->async_msg_mode = false;
    local->async_msg_mode_once = false;
  });
  add_bool(&LocalConfig::disable_hide_mwnd, "set_hide_mwnd_disable",
           "set_hide_mwnd_enable");
  add_bool(&LocalConfig::disable_msgback, "set_msg_back_disable",
           "set_msg_back_enable");
  add_bool(&LocalConfig::msg_back_off, "set_msg_back_off", "set_msg_back_on");
  add_bool(&LocalConfig::msg_back_disp_off, "set_msg_back_disp_off",
           "set_msg_back_disp_on");
  add_bool(&LocalConfig::cursor_disp_off, "set_mouse_disp_off",
           "set_mouse_disp_on");
  add_bool(&LocalConfig::cursor_move_by_key_disable,
           "set_mouse_move_by_key_disable", "set_mouse_move_by_key_enable");
  m.def("set_key_disable", [local](int key) {
    if (key >= 0 && static_cast<std::size_t>(key) < local->key_disable.size())
      local->key_disable[key] = true;
  });
  m.def("set_key_enable", [local](int key) {
    if (key >= 0 && static_cast<std::size_t>(key) < local->key_disable.size())
      local->key_disable[key] = false;
  });
  add_flag(&LocalConfig::disable_mwnd_anime, "set_mwnd_anime_off_flag",
           "get_mwnd_anime_off_flag");
  add_flag(&LocalConfig::force_mwnd_anime, "set_mwnd_anime_on_flag",
           "get_mwnd_anime_on_flag");
  add_flag(&LocalConfig::disable_mwnd_disp, "set_mwnd_disp_off_flag",
           "get_mwnd_disp_off_flag");
  add_flag(&LocalConfig::koe_dont_stop_on, "set_koe_dont_stop_on_flag",
           "get_koe_dont_stop_on_flag");
  add_flag(&LocalConfig::koe_dont_stop_off, "set_koe_dont_stop_off_flag",
           "get_koe_dont_stop_off_flag");
  add_bool(&LocalConfig::disable_shortcut, "set_shortcut_disable",
           "set_shortcut_enable");
  add_flag(&LocalConfig::quake_stop, "set_quake_stop_flag",
           "get_quake_stop_flag");
  add_flag(&LocalConfig::emote_mouth_stop, "set_emote_mouth_stop_flag",
           "get_emote_mouth_stop_flag");
  add_bool(&LocalConfig::bgmfade, "start_bgmfade", "end_bgmfade");
  add_flag(&LocalConfig::wait_display_vsync_off, "set_vsync_wait_off_flag",
           "get_vsync_wait_off_flag");
  add_bool(&LocalConfig::skip_trigger, "set_skip_trigger", {});
  add_bool(&LocalConfig::ignore_r, "ignore_r_on", "ignore_r_off");
  add_int(&LocalConfig::cursor_no, "set_cursor_no", "get_cursor_no");
  add_flag(&LocalConfig::time_stop, "set_time_stop_flag",
           "get_time_stop_flag");
  add_flag(&LocalConfig::counter_time_stop, "set_counter_time_stop_flag",
           "get_counter_time_stop_flag");
  add_flag(&LocalConfig::frame_action_time_stop,
           "set_frame_action_time_stop_flag",
           "get_frame_action_time_stop_flag");
  add_flag(&LocalConfig::stage_time_stop, "set_stage_time_stop_flag",
           "get_stage_time_stop_flag");
  add_override(&LocalConfig::font_futoku, "set_font_bold",
               "set_font_bold_default", "get_font_bold");
  add_override(&LocalConfig::font_shadow, "set_font_shadow",
               "set_font_shadow_default", "get_font_shadow");
  m.def("get_font_name", [gc = vm.gc_.get(), local] {
    return Value(gc->Allocate<String>(local->font_name));
  });
  m.def("set_font_name",
        [local](std::string font) { local->font_name = std::move(font); });
  m.def("set_font_name_default", [local] { local->font_name.clear(); });

  rt.reset_local_memory.emplace_back([local, gameexe = rt.gameexe] {
    *local = gameexe ? MakeSiglusLocalConfig(*gameexe) : LocalConfig{};
  });
}

RLVM_REGISTER(SiglusBindingRegistry, "3_script", BindScript);

}  // namespace libsiglus::binding
