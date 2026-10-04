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

#include "libsiglus/element_parser.hpp"

#include "libsiglus/element.hpp"
#include "utilities/flat_map.hpp"
#include "utilities/string_utilities.hpp"

#include <algorithm>
#include <format>
#include <functional>
#include <string>
#include <variant>

namespace libsiglus::elm {

// ==============================================================================
// Helpers
class Builder {
 public:
  struct Ctx {
    ElementCode& elm;
    std::span<const Value>& elmcode;
    AccessChain& chain;
    std::function<void(std::string)> Warn;
  };

  Builder(std::function<void(Ctx&)> a) : action_(std::move(a)) {}
  void Build(Ctx& ctx) const { std::invoke(action_, ctx); }

 private:
  std::function<void(Ctx&)> action_;
};

// -----------------------------------------------------------------------
inline static auto b(Type type, Node::var_t node) {
  return Builder([t = type, n = std::move(node)](Builder::Ctx& ctx) {
    ctx.chain.nodes.emplace_back(t, n);
    ctx.elmcode = ctx.elmcode.subspan(1);
  });
}

struct CallableTarget {
  std::optional<int> overload_id;
  std::string_view name;
  Type return_type = Type::None;
  CallFlags flags = {};
};

inline static Builder b_callable(std::string_view mem,
                                 Type return_type = Type::None,
                                 CallFlags flags = {}) {
  return b(Type::Callable, Member{mem, return_type, !flags.is_explicit,
                                  !flags.is_nonsimple, flags.await_result});
}
static Builder b_callable(std::initializer_list<CallableTarget> targets) {
  return Builder([targets =
                      std::vector<CallableTarget>(targets)](Builder::Ctx& ctx) {
    const int overload_id = ctx.elm.bind_ctx.overload_id;
    auto member_from_target = [](const CallableTarget& target) {
      return Member{target.name, target.return_type, !target.flags.is_explicit,
                    !target.flags.is_nonsimple, target.flags.await_result};
    };

    const CallableTarget* fallback = nullptr;
    for (const auto& target : targets) {
      if (target.overload_id && *target.overload_id == overload_id) {
        ctx.chain.nodes.emplace_back(Type::Callable,
                                     member_from_target(target));
        ctx.elmcode = ctx.elmcode.subspan(1);
        return;
      }
      if (!target.overload_id && !fallback)
        fallback = &target;
    }

    if (fallback) {
      ctx.chain.nodes.emplace_back(Type::Callable,
                                   member_from_target(*fallback));
      ctx.elmcode = ctx.elmcode.subspan(1);
      return;
    }

    ctx.Warn(std::format("[Callable] overload {} not found while parsing {}",
                         overload_id, ctx.chain.ToDebugString()));
    ctx.chain.nodes.emplace_back(Type::Callable,
                                 member_from_target(targets.front()));
    ctx.elmcode = ctx.elmcode.subspan(1);
  });
}
inline static Builder b_index_array(Type value_type) {
  return Builder([t = value_type](Builder::Ctx& ctx) {
    Subscript subscript{Integer(-1)};
    if (ctx.elmcode.size() < 2) {
      ctx.Warn("[IndexArray] expected index");
      ctx.chain.nodes.emplace_back(t, std::move(subscript));
      ctx.elmcode = ctx.elmcode.subspan(ctx.elmcode.size());
      return;
    }

    subscript.idx = ctx.elmcode[1];
    ctx.chain.nodes.emplace_back(t, std::move(subscript));
    ctx.elmcode = ctx.elmcode.subspan(2);
  });
}
inline static Builder b_intlist_bit_access(std::string_view mem) {
  return Builder([mem](Builder::Ctx& ctx) {
    auto emit_error = [&](std::string message, std::size_t consume) {
      ctx.Warn(std::move(message));

      ctx.chain.nodes.emplace_back(Type::Callable,
                                   Member{mem, Type::Int, true});
      Call call;
      call.args.emplace_back(Integer(-1));
      call.is_simple = true;
      ctx.chain.nodes.emplace_back(Type::Int, std::move(call));
      ctx.elmcode = ctx.elmcode.subspan(std::min(consume, ctx.elmcode.size()));
    };

    if (ctx.elmcode.size() < 3) {
      emit_error(std::format("[IntListBit] {} expected immediate index", mem),
                 ctx.elmcode.size());
      return;
    }

    auto* marker = std::get_if<Integer>(&ctx.elmcode[1]);
    if (!marker || marker->val_ != -1) {
      emit_error(std::format("[IntListBit] {} expected immediate index", mem),
                 1);
      return;
    }

    ctx.chain.nodes.emplace_back(Type::Callable, Member{mem, Type::Int, true});
    Call call;
    call.args.emplace_back(ctx.elmcode[2]);
    call.is_simple = true;
    ctx.chain.nodes.emplace_back(Type::Int, std::move(call));
    ctx.elmcode = ctx.elmcode.subspan(3);
  });
}
static Builder obj_getset(std::string_view mem,
                          Type type,
                          int get_code = 0,
                          int set_code = 1) {
  return Builder([getter = std::string(mem), setter = "set_" + std::string(mem),
                  type, get_code, set_code](Builder::Ctx& ctx) {
    const int overload_id = ctx.elm.bind_ctx.overload_id;
    const std::string& name = overload_id == set_code ? setter : getter;
    const Type return_type = overload_id == set_code ? Type::None : type;

    if (overload_id != get_code && overload_id != set_code) {
      ctx.Warn(
          std::format("[ObjectGetSet] overload {} not found while parsing {}",
                      overload_id, ctx.chain.ToDebugString()));
    }

    ctx.chain.nodes.emplace_back(
        Type::Callable, Member{name, return_type, /*implicit_call=*/true,
                               /*is_simple=*/true});
    ctx.elmcode = ctx.elmcode.subspan(1);
  });
}

static Builder obj_getter(std::string_view mem, Type rettype = Type::Int) {
  return b_callable(mem, rettype);
}

// ==============================================================================
// Method map
static flat_map<Builder> const* GetMethodMap(Type type) {
  switch (type) {
    case Type::IntList: {
      static const auto mp = make_flatmap<Builder>(
          {id[-1] | b_index_array(Type::Int),
           id[3] | b_intlist_bit_access("b1"),
           id[4] | b_intlist_bit_access("b2"),
           id[5] | b_intlist_bit_access("b4"),
           id[7] | b_intlist_bit_access("b8"),
           id[6] | b_intlist_bit_access("b16"), id[10] | b_callable("init"),
           id[2] | b_callable("resize"), id[9] | b_callable("size", Type::Int),
           id[8] | b_callable("fill"), id[1] | b_callable("Set")});
      return &mp;
    }

    case Type::IntEventList: {
      static const auto mp =
          make_flatmap<Builder>({id[-1] | b_index_array(Type::IntEvent),
                                 id[1] | b_callable("resize")});
      return &mp;
    }
    case Type::IntEvent: {
      static const auto mp = make_flatmap<Builder>(
          {id[0] | b(Type::Callable, Member("set")),
           id[7] | b(Type::Callable, Member("set_real")),
           id[1] | b(Type::Callable, Member("loop")),
           id[8] | b(Type::Callable, Member("loop_real")),
           id[2] | b(Type::Callable, Member("turn")),
           id[9] | b(Type::Callable, Member("turn_real")),
           id[3] | b_callable("end"),
           id[4] | b_callable("wait", Type::None, AWAIT),
           id[10] | b_callable("wait_key", Type::None, AWAIT),
           id[5] | b_callable("check", Type::Int)});

      return &mp;
    }

    case Type::StrList: {
      static const auto mp =
          make_flatmap<Builder>({id[-1] | b_index_array(Type::String),
                                 id[3] | b_callable("init", Type::None),
                                 id[2] | b(Type::Callable, Member("resize")),
                                 id[4] | b_callable("size", Type::Int)});
      return &mp;
    }
    case Type::String: {
      static const auto mp =
          make_flatmap<Builder>({id[0] | b_callable("upper", Type::String),
                                 id[1] | b_callable("lower", Type::String),
                                 id[6] | b_callable("cnt", Type::Int),
                                 id[5] | b_callable("len", Type::Int),
                                 id[2] | b_callable("left", Type::String),
                                 id[7] | b_callable("left_len", Type::String),
                                 id[4] | b_callable("right", Type::String),
                                 id[9] | b_callable("right_len", Type::String),
                                 id[3] | b_callable("mid", Type::String),
                                 id[8] | b_callable("mid_len", Type::String),
                                 id[10] | b_callable("find", Type::Int),
                                 id[11] | b_callable("rfind", Type::Int),
                                 id[13] | b_callable("charat", Type::Int),
                                 id[12] | b_callable("tonum", Type::Int)});
      return &mp;
    }

    case Type::Math: {
      static const auto mp = make_flatmap<Builder>(
          {id[3] | b_callable("max", Type::Int),
           id[4] | b_callable("min", Type::Int),
           id[10] | b_callable("limit", Type::Int),
           id[5] | b_callable("abs", Type::Int),
           id[0] | b_callable("rand", Type::Int),
           id[14] | b_callable("sqrt", Type::Int),
           id[19] | b_callable("log", Type::Int),
           id[20] | b_callable("log2", Type::Int),
           id[21] | b_callable("log10", Type::Int),
           id[6] | b_callable("sin", Type::Int),
           id[7] | b_callable("cos", Type::Int),
           id[8] | b_callable("tan", Type::Int),
           id[16] | b_callable("asin", Type::Int),
           id[17] | b_callable("acos", Type::Int),
           id[18] | b_callable("atan", Type::Int),
           id[15] | b_callable("distance", Type::Int),
           id[22] | b_callable("angle", Type::Int),
           id[9] | b_callable("linear", Type::Int),
           id[2] | b_callable("timetable", Type::Int),
           id[1] | b_callable("tostr", Type::String),
           id[11] | b_callable("tostr_zero", Type::String),
           id[12] | b_callable("tostr_zen", Type::String),
           id[13] | b_callable("tostr_zen_zero", Type::String),
           id[23] | b_callable("tostr_code", Type::String)});
      return &mp;
    }

    case Type::System: {
      static const auto mp = make_flatmap<Builder>(
          {id[14] | b_callable("calendar", Type::None),
           id[15] | b_callable("time", Type::Int),
           id[0] | b_callable("window_active", Type::Int),
           id[13] | b_callable("is_debug", Type::Int),
           id[1] | b_callable("shell_openfile"),
           id[5] | b_callable("openurl"),
           id[6] | b_callable("check_file_exist"),
           id[12] | b_callable("check_save_file_exist"),
           id[2] | b_callable("check_dummy"),
           id[21] | b_callable("clear_dummy", Type::None),
           id[17] | b_callable("msgbox_ok"),
           id[18] | b_callable("msgbox_okcancel"),
           id[19] | b_callable("msgbox_yn"),
           id[20] | b_callable("msgbox_yncancel"),
           id[7] | b_callable("debug_msgbox_ok"),
           id[8] | b_callable("debug_msgbox_okcancel"),
           id[9] | b_callable("debug_msgbox_yn"),
           id[10] | b_callable("debug_msgbox_yncancel"),
           id[11] | b_callable("debug_write_log"),
           id[4] | b_callable("get_chihayabench", Type::String),
           id[3] | b_callable("open_chihayabench"),
           id[16] | b_callable("get_lang", Type::String)});
      return &mp;
    }

    case Type::Script: {
      static const auto mp = make_flatmap<Builder>(
          {id[38] | b_callable("set_auto_savepoint_off"),
           id[39] | b_callable("set_auto_savepoint_on"),
           id[2] | b_callable("set_skip_disable"),
           id[5] | b_callable("set_skip_enable"),
           id[85] | b_callable("set_skip_disable_flag"),
           id[83] | b_callable("get_skip_disable_flag", Type::Int),
           id[6] | b_callable("set_ctrl_skip_disable"),
           id[7] | b_callable("set_ctrl_skip_enable"),
           id[86] | b_callable("set_ctrl_skip_disable_flag"),
           id[84] | b_callable("get_ctrl_skip_disable_flag", Type::Int),
           id[18] | b_callable("check_skip"),
           id[30] | b_callable("set_stop_skip_by_key_disable"),
           id[31] | b_callable("set_stop_skip_by_key_enable"),
           id[54] | b_callable("set_end_msg_by_key_disable"),
           id[55] | b_callable("set_end_msg_by_key_enable"),
           id[74] | b_callable("set_skip_unread_message_flag"),
           id[75] | b_callable("get_skip_unread_message_flag", Type::Int),
           id[0] | b_callable("start_auto_mode"),
           id[1] | b_callable("end_auto_mode"),
           id[60] | b_callable("set_auto_mode_moji_wait"),
           id[62] | b_callable("set_auto_mode_moji_wait_default"),
           id[64] | b_callable("get_auto_mode_moji_wait", Type::Int),
           id[61] | b_callable("set_auto_mode_min_wait"),
           id[63] | b_callable("set_auto_mode_min_wait_default"),
           id[65] | b_callable("get_auto_mode_min_wait", Type::Int),
           id[76] | b_callable("set_auto_mode_moji_cnt"),
           id[77] | b_callable("set_mouse_cursor_hide_onoff"),
           id[82] | b_callable("set_mouse_cursor_hide_onoff_default"),
           id[78] | b_callable("get_mouse_cursor_hide_onoff", Type::Int),
           id[79] | b_callable("set_mouse_cursor_hide_time"),
           id[80] | b_callable("set_mouse_cursor_hide_time_default"),
           id[81] | b_callable("get_mouse_cursor_hide_time", Type::Int),
           id[25] | b_callable("set_message_speed"),
           id[27] | b_callable("set_message_speed_default"),
           id[26] | b_callable("get_message_speed", Type::Int),
           id[28] | b_callable("set_message_nowait_flag"),
           id[29] | b_callable("get_message_nowait_flag", Type::Int),
           id[21] | b_callable("set_msg_async_mode_on"),
           id[51] | b_callable("set_msg_async_mode_on_once"),
           id[22] | b_callable("set_msg_async_mode_off"),
           id[46] | b_callable("set_hide_mwnd_disable"),
           id[47] | b_callable("set_hide_mwnd_enable"),
           id[12] | b_callable("set_msg_back_disable"),
           id[13] | b_callable("set_msg_back_enable"),
           id[34] | b_callable("set_msg_back_off"),
           id[35] | b_callable("set_msg_back_on"),
           id[56] | b_callable("set_msg_back_disp_off"),
           id[57] | b_callable("set_msg_back_disp_on"),
           id[14] | b_callable("set_mouse_disp_off"),
           id[15] | b_callable("set_mouse_disp_on"),
           id[19] | b_callable("set_mouse_move_by_key_disable"),
           id[20] | b_callable("set_mouse_move_by_key_enable"),
           id[40] | b_callable("set_key_disable"),
           id[41] | b_callable("set_key_enable"),
           id[3] | b_callable("set_mwnd_anime_off_flag"),
           id[9] | b_callable("get_mwnd_anime_off_flag", Type::Int),
           id[4] | b_callable("set_mwnd_anime_on_flag"),
           id[16] | b_callable("get_mwnd_anime_on_flag", Type::Int),
           id[8] | b_callable("set_mwnd_disp_off_flag"),
           id[17] | b_callable("get_mwnd_disp_off_flag", Type::Int),
           id[42] | b_callable("set_koe_dont_stop_on_flag"),
           id[43] | b_callable("get_koe_dont_stop_on_flag", Type::Int),
           id[44] | b_callable("set_koe_dont_stop_off_flag"),
           id[45] | b_callable("get_koe_dont_stop_off_flag", Type::Int),
           id[33] | b_callable("set_shortcut_enable"),
           id[32] | b_callable("set_shortcut_disable"),
           id[11] | b_callable("set_quake_stop_flag"),
           id[23] | b_callable("get_quake_stop_flag", Type::Int),
           id[96] | b_callable("set_emote_mouth_stop_flag"),
           id[97] | b_callable("get_emote_mouth_stop_flag", Type::Int),
           id[36] | b_callable("start_bgmfade"),
           id[37] | b_callable("end_bgmfade"),
           id[10] | b_callable("set_vsync_wait_off_flag"),
           id[24] | b_callable("get_vsync_wait_off_flag", Type::Int),
           id[48] | b_callable("set_skip_trigger"),
           id[49] | b_callable("ignore_r_on"),
           id[50] | b_callable("ignore_r_off"),
           id[52] | b_callable("set_cursor_no"),
           id[53] | b_callable("get_cursor_no", Type::Int),
           id[66] | b_callable("set_time_stop_flag"),
           id[67] | b_callable("get_time_stop_flag", Type::Int),
           id[68] | b_callable("set_counter_time_stop_flag"),
           id[73] | b_callable("get_counter_time_stop_flag", Type::Int),
           id[69] | b_callable("set_frame_action_time_stop_flag"),
           id[72] | b_callable("get_frame_action_time_stop_flag", Type::Int),
           id[70] | b_callable("set_stage_time_stop_flag"),
           id[71] | b_callable("get_stage_time_stop_flag", Type::Int)});
      return &mp;
    }

    case Type::ScriptExcall: {
      static const auto mp = make_flatmap<Builder>(
          {id[87] | b_callable("set_font_name"),
           id[89] | b_callable("set_font_name_default"),
           id[88] | b_callable("get_font_name", Type::String),
           id[90] | b_callable("set_font_bold"),
           id[91] | b_callable("set_font_bold_default"),
           id[92] | b_callable("get_font_bold", Type::Int),
           id[93] | b_callable("set_font_shadow"),
           id[94] | b_callable("set_font_shadow_default"),
           id[95] | b_callable("get_font_shadow", Type::Int)});
      return &mp;
    }

    case Type::FrameActionList: {
      static const auto mp =
          make_flatmap<Builder>({id[-1] | b_index_array(Type::FrameAction),
                                 id[2] | b_callable("size", Type::Int),
                                 id[1] | b_callable("resize")});
      return &mp;
    }

    case Type::FrameAction: {
      static const auto mp = make_flatmap<Builder>(
          {id[1] | b_callable("start"), id[3] | b_callable("start_real"),
           id[2] | b_callable("end", Type::None),
           id[0] | b(Type::Counter, Member("counter")),
           id[4] | b_callable("is_end_action", Type::Int)});
      return &mp;
    }

    case Type::CounterList: {
      static const auto mp =
          make_flatmap<Builder>({id[-1] | b_index_array(Type::Counter),
                                 id[1] | b_callable("size", Type::Int)});
      return &mp;
    }

    case Type::Counter: {
      static const auto mp = make_flatmap<Builder>(
          {id[0] | b_callable("set"), id[1] | b_callable("get", Type::Int),
           id[2] | b_callable("reset"), id[3] | b_callable("start"),
           id[9] | b_callable("start_real"), id[10] | b_callable("start_frame"),
           id[11] | b_callable("start_frame_real"),
           id[12] | b_callable("start_frame_loop"),
           id[13] | b_callable("start_frame_loop_real"),
           id[4] | b_callable("stop"), id[5] | b_callable("resume"),
           id[6] | b_callable("wait", Type::None, AWAIT),
           id[8] | b_callable("wait_key", Type::Int, AWAIT),
           id[7] | b_callable("check_value", Type::Int),
           id[14] | b_callable("check_active", Type::Int)});
      return &mp;
    }

    case Type::Syscom: {
      static const auto mp = make_flatmap<Builder>(
          {id[0] | b(Type::None, Member("menu")),
           id[6] | b(Type::Callable, Member("menu_enable")),
           id[7] | b(Type::Callable, Member("menu_disable")),
           id[11] | b_callable({{0, "btn_enable_all"}, {1, "btn_enable"}}),
           id[12] | b_callable({{0, "btn_disable_all"}, {1, "btn_disable"}}),
           id[133] | b(Type::None, Member("touch_enable")),
           id[134] | b(Type::None, Member("touch_disable")),
           id[5] | b(Type::None, Member("init_flags")),
           // readskip
           id[200] | b(Type::Callable, Member("set_readskip")),
           id[201] | b(Type::Int, Member("get_readskip")),
           id[202] | b(Type::Callable, Member("set_enable_readskip")),
           id[203] | b(Type::Int, Member("get_enable_readskip")),
           id[204] | b(Type::Callable, Member("set_exist_readskip")),
           id[205] | b(Type::Int, Member("get_exist_readskip")),
           id[206] | b(Type::Int, Member("is_readskip_enable")),
           // autoskip
           id[207] | b(Type::Callable, Member("set_autoskip")),
           id[208] | b(Type::Int, Member("get_autoskip")),
           id[209] | b(Type::Callable, Member("set_enable_autoskip")),
           id[210] | b(Type::Int, Member("get_enable_autoskip")),
           id[211] | b(Type::Callable, Member("set_exist_autoskip")),
           id[212] | b(Type::Int, Member("get_exist_autoskip")),
           id[213] | b(Type::Int, Member("is_autoskip_enable")),
           // automode
           id[214] | b(Type::Callable, Member("set_automode")),
           id[215] | b(Type::Int, Member("get_automode")),
           id[216] | b(Type::Callable, Member("set_enable_automode")),
           id[217] | b(Type::Int, Member("get_enable_automode")),
           id[218] | b(Type::Callable, Member("set_exist_automode")),
           id[219] | b(Type::Int, Member("get_exist_automode")),
           id[220] | b(Type::Int, Member("is_automode_enable")),
           // hide mwnd
           id[221] | b(Type::Callable, Member("set_hidemwnd")),
           id[222] | b(Type::Int, Member("get_hidemwnd")),
           id[223] | b(Type::Callable, Member("set_enable_hidemwnd")),
           id[224] | b(Type::Int, Member("get_enable_hidemwnd")),
           id[225] | b(Type::Callable, Member("set_exist_hidemwnd")),
           id[226] | b(Type::Int, Member("get_exist_hidemwnd")),
           id[227] | b(Type::Int, Member("is_hidemwnd_enable")),
           // extra local switch
           id[300] | b(Type::Callable, Member("set_extraswitch")),
           id[301] | b(Type::Callable, Member("get_extraswitch")),
           id[302] | b(Type::Callable, Member("set_enable_extraswitch")),
           id[303] | b(Type::Callable, Member("get_enable_extraswitch")),
           id[304] | b(Type::Callable, Member("set_exist_extraswitch")),
           id[305] | b(Type::Callable, Member("get_exist_extraswitch")),
           id[306] | b(Type::Callable, Member("is_extraswitch_enable")),
           // local mode
           id[23] | b(Type::Callable, Member("set_localmode")),
           id[57] | b(Type::Int, Member("get_localmode")),
           id[58] | b(Type::Callable, Member("set_enable_localmode")),
           id[59] | b(Type::Int, Member("get_enable_localmode")),
           id[62] | b(Type::Callable, Member("set_exist_localmode")),
           id[63] | b(Type::Int, Member("get_exist_localmode")),
           id[64] | b(Type::Int, Member("is_localmode_enable")),
           // msgback
           id[192] | b(Type::None, Member("open_msgback")),
           id[193] | b(Type::None, Member("close_msgback")),
           id[194] | b(Type::Callable, Member("set_enable_msgback")),
           id[195] | b(Type::Int, Member("get_enable_msgback")),
           id[196] | b(Type::Callable, Member("set_exist_msgback")),
           id[197] | b(Type::Int, Member("get_exist_msgback")),
           id[198] | b(Type::Int, Member("is_msgback_enable")),
           id[329] | b(Type::Int, Member("is_msgback_open")),
           // return to sel
           id[228] | b(Type::Callable, Member("return_to_sel")),
           id[230] | b(Type::Callable, Member("set_enable_retsel")),
           id[231] | b(Type::Int, Member("get_enable_retsel")),
           id[232] | b(Type::Callable, Member("set_exist_retsel")),
           id[233] | b(Type::Int, Member("get_exist_retsel")),
           id[234] | b(Type::Int, Member("is_retsel_enable")),
           // return to menu
           id[235] | b(Type::Callable, Member("return_to_menu")),
           id[237] | b(Type::Callable, Member("set_enable_retmenu")),
           id[238] | b(Type::Int, Member("get_enable_retmenu")),
           id[239] | b(Type::Callable, Member("set_exist_retmenu")),
           id[240] | b(Type::Int, Member("get_exist_retmenu")),
           id[241] | b(Type::Int, Member("is_retmenu_enable")),
           // end game
           id[242] | b(Type::Callable, Member("end_game")),
           id[244] | b(Type::Callable, Member("set_enable_endgame")),
           id[245] | b(Type::Int, Member("get_enable_endgame")),
           id[246] | b(Type::Callable, Member("set_exist_endgame")),
           id[247] | b(Type::Int, Member("get_exist_endgame")),
           id[248] | b(Type::Int, Member("is_endgame_enable")),
           // replay koe
           id[288] | b(Type::None, Member("replay_koe")),
           id[292] | b(Type::Int, Member("check_koe")),
           id[289] | b(Type::Int, Member("get_cur_koe")),
           id[291] | b(Type::Int, Member("get_cur_chr")),
           id[293] | b(Type::None, Member("clear_koe_chr")),
           id[294] | b(Type::String, Member("get_scene_title")),
           id[295] | b(Type::String, Member("get_save_message")),

           id[199] | b(Type::None, Member("get_total_playtime(fixme)")),
           id[229] | b(Type::Callable, Member("set_total_playtime")),

           // save
           id[1] | b(Type::None, Member("open_save")),
           id[251] | b(Type::Callable, Member("set_enable_save")),
           id[252] | b(Type::Int, Member("get_enable_save")),
           id[253] | b(Type::Callable, Member("set_exist_save")),
           id[254] | b(Type::Int, Member("get_exist_save")),
           id[255] | b(Type::Int, Member("is_save_enable")),
           // load
           id[2] | b(Type::None, Member("open_load")),
           id[258] | b(Type::Callable, Member("set_enable_load")),
           id[259] | b(Type::Int, Member("get_enable_load")),
           id[260] | b(Type::Callable, Member("set_exist_load")),
           id[261] | b(Type::Int, Member("get_exist_load")),
           id[262] | b(Type::Int, Member("is_load_enable")),

           // save / load
           id[249] | b(Type::Callable, Member("save")),
           id[256] | b(Type::Callable, Member("load")),
           id[18] | b(Type::Callable, Member("qsave")),
           id[20] | b(Type::Callable, Member("qload")),
           id[271] | b(Type::Callable, Member("endsave")),
           id[269] | b(Type::Callable, Member("endload")),
           // inner save / load
           id[272] | b(Type::Callable, Member("inner_save")),
           id[273] | b(Type::Callable, Member("inner_load")),
           id[276] | b(Type::Callable, Member("clear_inner_save")),
           id[274] | b(Type::Callable, Member("check_inner_save")),
           // message back save / load
           id[310] | b(Type::Callable, Member("msgbk_load")),
           // save data
           id[68] | b(Type::Int, Member("get_save_count")),
           id[168] | b(Type::Int, Member("get_qsave_count")),
           id[79] | b(Type::Callable, Member("get_new_save_no")),
           id[170] | b(Type::Callable, Member("get_new_qsave_no")),
           id[69] | b(Type::Callable, Member("is_save_exist")),
           id[70] | b(Type::Callable, Member("get_save_year")),
           id[71] | b(Type::Callable, Member("get_save_month")),
           id[72] | b(Type::Callable, Member("get_save_day")),
           id[73] | b(Type::Callable, Member("get_save_weekday")),
           id[74] | b(Type::Callable, Member("get_save_hour")),
           id[75] | b(Type::Callable, Member("get_save_minute")),
           id[76] | b(Type::Callable, Member("get_save_second")),
           id[77] | b(Type::Callable, Member("get_save_millisecond")),
           id[78] | b(Type::Callable, Member("get_save_title")),
           id[129] | b(Type::Callable, Member("get_save_message")),
           id[324] | b(Type::Callable, Member("get_save_full_message")),
           id[131] | b(Type::Callable, Member("get_save_comment")),
           id[180] | b(Type::Callable, Member("set_save_comment")),
           id[183] | b_callable("get_save_value", Type::Int),
           id[182] | b_callable("set_save_value"),
           id[320] | b(Type::Callable, Member("get_save_append_dir")),
           id[321] | b(Type::Callable, Member("get_save_append_name")),
           id[169] | b(Type::Callable, Member("is_qsave_exist")),
           id[171] | b(Type::Callable, Member("get_qsave_year")),
           id[172] | b(Type::Callable, Member("get_qsave_month")),
           id[173] | b(Type::Callable, Member("get_qsave_day")),
           id[174] | b(Type::Callable, Member("get_qsave_weekday")),
           id[175] | b(Type::Callable, Member("get_qsave_hour")),
           id[176] | b(Type::Callable, Member("get_qsave_minute")),
           id[177] | b(Type::Callable, Member("get_qsave_second")),
           id[178] | b(Type::Callable, Member("get_qsave_millisecond")),
           id[179] | b(Type::Callable, Member("get_qsave_title")),
           id[130] | b(Type::Callable, Member("get_qsave_message")),
           id[325] | b(Type::Callable, Member("get_qsave_full_message")),
           id[132] | b(Type::Callable, Member("get_qsave_comment")),
           id[181] | b(Type::Callable, Member("set_qsave_comment")),
           id[184] | b_callable("get_qsave_value", Type::Int),
           id[185] | b_callable("set_qsave_value"),
           id[322] | b(Type::Callable, Member("get_qsave_append_dir")),
           id[323] | b(Type::Callable, Member("get_qsave_append_name")),
           id[270] | b(Type::Callable, Member("is_endsave_exist")),
           id[67] | b(Type::Callable, Member("copy_save")),
           id[22] | b(Type::Callable, Member("change_save")),
           id[19] | b(Type::Callable, Member("delete_save")),
           id[128] | b(Type::Callable, Member("copy_qsave")),
           id[66] | b(Type::Callable, Member("change_qsave")),
           id[65] | b(Type::Callable, Member("delete_qsave")),

           // environment settings
           id[3] | b(Type::None, Member("open_config_menu")),
           id[138] | b(Type::None, Member("open_config_windowmode_menu")),
           id[139] | b(Type::None, Member("open_config_volume_menu")),
           id[137] | b(Type::None, Member("open_config_bgm_fade_menu")),
           id[147] | b(Type::None, Member("open_config_koemode_menu")),
           id[146] | b(Type::None, Member("open_config_charakoe_menu")),
           id[151] | b(Type::None, Member("open_config_jitan_menu")),
           id[135] | b(Type::None, Member("open_config_message_speed_menu")),
           id[136] | b(Type::None, Member("open_config_filter_color_menu")),
           id[140] | b(Type::None, Member("open_config_auto_mode_menu")),
           id[142] | b(Type::None, Member("open_config_font_menu")),
           id[141] | b(Type::None, Member("open_config_system_menu")),
           id[167] | b(Type::None, Member("open_config_movie_menu")),
           // window mode
           id[4] | b(Type::Callable, Member("set_window_mode")),
           id[99] | b(Type::None, Member("set_window_mode_default")),
           id[9] | b(Type::Int, Member("get_window_mode")),
           id[13] | b(Type::Callable, Member("set_window_mode_size")),
           id[100] | b(Type::None, Member("set_window_mode_default")),
           id[16] | b(Type::Int, Member("get_window_mode_size")),
           id[309] | b(Type::Callable, Member("check_window_mode_size_enable")),

           // volume
           id[39] | b(Type::Callable, Member("set_all_volume")),
           id[21] | b(Type::Callable, Member("set_bgm_volume")),
           id[26] | b(Type::Callable, Member("set_koe_volume")),
           id[29] | b(Type::Callable, Member("set_pcm_volume")),
           id[32] | b(Type::Callable, Member("set_se_volume")),
           id[263] | b(Type::Callable, Member("set_mov_volume")),
           id[277] | b(Type::Callable, Member("set_sound_volume")),
           id[60] | b(Type::Callable, Member("set_all_onoff")),
           id[35] | b(Type::Callable, Member("set_bgm_onoff")),
           id[36] | b(Type::Callable, Member("set_koe_onoff")),
           id[37] | b(Type::Callable, Member("set_pcm_onoff")),
           id[38] | b(Type::Callable, Member("set_se_onoff")),
           id[266] | b(Type::Callable, Member("set_mov_onoff")),
           id[280] | b(Type::Callable, Member("set_sound_onoff")),
           id[40] | b(Type::None, Member("set_all_volume_default")),
           id[24] | b(Type::None, Member("set_bgm_volume_default")),
           id[27] | b(Type::None, Member("set_koe_volume_default")),
           id[30] | b(Type::None, Member("set_pcm_volume_default")),
           id[33] | b(Type::None, Member("set_se_volume_default")),
           id[264] | b(Type::None, Member("set_mov_volume_default")),
           id[278] | b(Type::Callable, Member("set_sound_volume_default")),
           id[101] | b(Type::None, Member("set_all_onoff_default")),
           id[102] | b(Type::None, Member("set_bgm_onoff_default")),
           id[103] | b(Type::None, Member("set_koe_onoff_default")),
           id[104] | b(Type::None, Member("set_pcm_onoff_default")),
           id[105] | b(Type::None, Member("set_se_onoff_default")),
           id[267] | b(Type::None, Member("set_mov_onoff_default")),
           id[281] | b(Type::Callable, Member("set_sound_onoff_default")),
           id[41] | b(Type::Int, Member("get_all_volume")),
           id[25] | b(Type::Int, Member("get_bgm_volume")),
           id[28] | b(Type::Int, Member("get_koe_volume")),
           id[31] | b(Type::Int, Member("get_pcm_volume")),
           id[34] | b(Type::Int, Member("get_se_volume")),
           id[265] | b(Type::Int, Member("get_mov_volume")),
           id[279] | b(Type::Callable, Member("get_sound_volume")),
           id[61] | b(Type::Int, Member("get_all_onoff")),
           id[42] | b(Type::Int, Member("get_bgm_onoff")),
           id[43] | b(Type::Int, Member("get_koe_onoff")),
           id[44] | b(Type::Int, Member("get_pcm_onoff")),
           id[45] | b(Type::Int, Member("get_se_onoff")),
           id[268] | b(Type::Int, Member("get_mov_onoff")),
           id[282] | b(Type::Callable, Member("get_sound_onoff")),
           // bgm fade
           id[94] | b(Type::Callable, Member("set_bgmfade_volume")),
           id[97] | b(Type::Callable, Member("set_bgmfade_onoff")),
           id[95] | b(Type::None, Member("set_bgmfade_volume_default")),
           id[106] | b(Type::None, Member("set_bgmfade_onoff_default")),
           id[96] | b(Type::Int, Member("get_bgmfade_volume")),
           id[98] | b(Type::Int, Member("get_bgmfade_onoff")),
           // koemode
           id[148] | b(Type::Callable, Member("set_koemode")),
           id[149] | b(Type::None, Member("set_koemode_default")),
           id[150] | b(Type::Int, Member("get_koemode")),
           // character koe
           id[143] | b(Type::Callable, Member("set_charakoe_onoff")),
           id[144] | b(Type::Callable, Member("set_charakoe_onoff_default")),
           id[145] | b(Type::Callable, Member("get_charakoe_onoff")),
           id[186] | b(Type::Callable, Member("set_charakoe_volume")),
           id[187] | b(Type::Callable, Member("set_charakoe_volume_default")),
           id[188] | b(Type::Callable, Member("get_charakoe_volume")),
           // jitan
           id[153] | b(Type::Callable, Member("set_jitan_normal_onoff")),
           id[154] | b(Type::None, Member("set_jitan_normal_onoff_default")),
           id[155] | b(Type::Int, Member("get_jitan_normal_onoff")),
           id[156] | b(Type::Callable, Member("set_jitan_auto_mode_onoff")),
           id[157] | b(Type::None, Member("set_jitan_auto_mode_onoff_default")),
           id[158] | b(Type::Int, Member("get_jitan_auto_mode_onoff")),
           id[159] | b(Type::Callable, Member("set_jitan_koe_replay_onoff")),
           id[160] |
               b(Type::None, Member("set_jitan_koe_replay_onoff_default")),
           id[161] | b(Type::Int, Member("get_jitan_koe_replay_onoff")),
           id[152] | b(Type::Callable, Member("set_jitan_speed")),
           id[162] | b(Type::None, Member("set_jitan_speed_default")),
           id[163] | b(Type::Int, Member("get_jitan_speed")),
           // message speed
           id[46] | b(Type::Callable, Member("set_message_speed")),
           id[47] | b(Type::None, Member("set_message_speed_default")),
           id[48] | b(Type::Int, Member("get_message_speed")),
           id[49] | b(Type::Callable, Member("set_message_nowait")),
           id[107] | b(Type::None, Member("set_message_nowait_default")),
           id[50] | b(Type::Int, Member("get_message_nowait")),
           // auto mode
           id[51] | b(Type::Callable, Member("set_auto_mode_moji_wait")),
           id[52] | b(Type::None, Member("set_auto_mode_moji_wait_default")),
           id[53] | b(Type::Int, Member("get_auto_mode_moji_wait")),
           id[54] | b(Type::Callable, Member("set_auto_mode_min_wait")),
           id[55] | b(Type::None, Member("set_auto_mode_min_wait_default")),
           id[56] | b(Type::Int, Member("get_auto_mode_min_wait")),
           // auto hide mouse cursor
           id[311] | b(Type::Callable, Member("set_mouse_cursor_hide_onoff")),
           id[312] |
               b(Type::None, Member("set_mouse_cursor_hide_onoff_default")),
           id[313] | b(Type::Int, Member("get_mouse_cursor_hide_onoff")),
           id[317] | b(Type::Callable, Member("set_mouse_cursor_hide_time")),
           id[318] |
               b(Type::None, Member("set_mouse_cursor_hide_time_default")),
           id[319] | b(Type::Int, Member("get_mouse_cursor_hide_time")),
           // window background color
           id[82] | b(Type::Callable, Member("set_filter_color_r")),
           id[85] | b(Type::Callable, Member("set_filter_color_g")),
           id[86] | b(Type::Callable, Member("set_filter_color_b")),
           id[87] | b(Type::Callable, Member("set_filter_color_a")),
           id[83] | b(Type::None, Member("set_filter_color_r_default")),
           id[88] | b(Type::None, Member("set_filter_color_g_default")),
           id[89] | b(Type::None, Member("set_filter_color_b_default")),
           id[90] | b(Type::None, Member("set_filter_color_a_default")),
           id[84] | b(Type::Int, Member("get_filter_color_r")),
           id[91] | b(Type::Int, Member("get_filter_color_g")),
           id[92] | b(Type::Int, Member("get_filter_color_b")),
           id[93] | b(Type::Int, Member("get_filter_color_a")),
           // display object
           id[189] | b(Type::Callable, Member("set_obj_disp_onoff")),
           id[190] | b(Type::Callable, Member("set_obj_disp_onoff_default")),
           id[191] | b(Type::Callable, Member("get_obj_disp_onoff")),
           // global extra switch
           id[14] | b(Type::Callable, Member("set_global_extraswitch_onoff")),
           id[15] | b(Type::Callable,
                      Member("set_global_extraswitch_onoff_default")),
           id[17] | b(Type::Callable, Member("get_global_extraswitch_onoff")),
           // global extra mode
           id[164] | b(Type::Callable, Member("set_global_extramode")),
           id[165] | b(Type::Callable, Member("set_global_extramode_default")),
           id[166] | b(Type::Callable, Member("get_global_extramode_default")),

           // system settings
           id[80] | b(Type::Callable, Member("set_saveload_alert_onoff")),
           id[108] | b(Type::None, Member("set_saveload_alert_onoff_default")),
           id[10] | b(Type::Int, Member("get_saveload_alert_onoff")),
           id[110] | b(Type::Callable, Member("set_sleep_onoff")),
           id[111] | b(Type::None, Member("set_sleep_onoff_default")),
           id[112] | b(Type::Int, Member("get_sleep_onoff")),
           id[113] | b(Type::Callable, Member("set_no_wipe_anime_onoff")),
           id[114] | b(Type::None, Member("set_no_wipe_anime_onoff_default")),
           id[115] | b(Type::Callable, Member("get_no_wipe_anime_onoff")),
           id[116] | b(Type::Callable, Member("set_skip_wipe_anime_onoff")),
           id[117] | b(Type::None, Member("set_skip_wipe_anime_onoff_default")),
           id[118] | b(Type::Int, Member("get_skip_wipe_anime_onoff")),
           id[8] | b(Type::Callable, Member("set_no_mwnd_anime_onoff")),
           id[109] | b(Type::None, Member("set_no_mwnd_anime_onoff_default")),
           id[81] | b(Type::Int, Member("get_no_mwnd_anime_onoff")),
           id[119] | b(Type::Callable, Member("set_wheel_next_message_onoff")),
           id[120] |
               b(Type::None, Member("set_wheel_next_message_onoff_default")),
           id[121] | b(Type::Int, Member("get_wheel_next_message_onoff")),
           id[122] | b(Type::Callable, Member("set_koe_dont_stop_onoff")),
           id[123] | b(Type::None, Member("set_koe_dont_stop_onoff_default")),
           id[124] | b(Type::Int, Member("get_koe_dont_stop_onoff")),
           id[125] | b(Type::Callable, Member("set_skip_unread_message_onoff")),
           id[126] |
               b(Type::None, Member("set_skip_unread_message_onoff_default")),
           id[127] | b(Type::Int, Member("get_skip_unread_message_onoff")),
           id[250] | b(Type::Callable, Member("set_play_silent_sound_onoff")),
           id[247] |
               b(Type::None, Member("set_play_silent_sound_onoff_default")),
           id[243] | b(Type::Int, Member("get_play_silent_sound_onoff")),

           // font
           id[283] | b(Type::Callable, Member("set_font_name")),
           id[326] | b(Type::None, Member("set_font_name_default")),
           id[284] | b(Type::String, Member("get_font_name")),
           id[285] | b(Type::Callable, Member("is_font_exist")),
           id[296] | b(Type::Callable, Member("set_font_bold")),
           id[298] | b(Type::None, Member("set_font_bold_default")),
           id[307] | b(Type::Int, Member("get_font_bold")),
           id[297] | b(Type::Callable, Member("set_font_decoration")),
           id[299] | b(Type::None, Member("set_font_decoration_default")),
           id[308] | b(Type::Int, Member("get_font_decoration")),

           // capture
           id[286] | b(Type::Callable, Member("create_capture_buffer")),
           id[287] | b(Type::None, Member("destroy_capture_buffer")),
           id[316] | b(Type::Callable, Member("capture_to_capture_buffer")),
           id[314] | b(Type::Callable, Member("save_capture_buffer_to_file")),
           id[315] | b(Type::Callable, Member("load_flag_from_capture_file")),
           id[290] | b(Type::Callable, Member("capture_to_png")),
           id[327] | b(Type::None, Member("twitter")),
           id[328] | b(Type::Callable, Member("set_ret_scene_once")),
           id[330] | b(Type::Callable, Member("get_sys_extra_int")),
           id[331] | b(Type::Callable, Member("get_sys_extra_str"))});
      return &mp;
    }

    case Type::Excall: {
      static const auto mp = make_flatmap<Builder>(
          {id[-1] | b_index_array(Type::Excall),
           id[4] | b(Type::None, Member("alloc")),
           id[5] | b(Type::None, Member("free")),
           id[12] | b(Type::Int, Member("is_excall")),
           id[8] | b(Type::Int, Member("check_alloc")),
           id[7] | b(Type::IntList, Member("F")),
           id[6] | b(Type::CounterList, Member("counter")),
           id[9] | b(Type::FrameAction, Member("frame_action")),
           id[10] | b(Type::FrameActionList, Member("frame_action_ch")),
           id[0] | b(Type::StageList, Member("stage")),
           id[2] | b(Type::Stage, Member("back")),
           id[1] | b(Type::Stage, Member("front")),
           id[3] | b(Type::Stage, Member("next")),
           id[13] | b(Type::ScriptExcall, Member("script"))});
      return &mp;
    }

    case Type::StageList: {
      static const auto mp =
          make_flatmap<Builder>({id[-1] | b_index_array(Type::Stage)});
      return &mp;
    }
    case Type::Stage: {
      static const auto mp = make_flatmap<Builder>(
          {id[2] | b(Type::ObjList, Member("object")),
           id[3] | b(Type::MwndList, Member("mwnd")),
           id[6] | b(Type::GroupList, Member("objgroup")),
           id[5] | b(Type::BtnselItemList, Member("btnsel")),
           id[8] | b(Type::WorldList, Member("world")),
           id[4] | b(Type::EffectList, Member("effect")),
           id[7] | b(Type::QuakeList, Member("quake")),
           id[0] | b(Type::Callable, Member("create_obj")),
           id[1] | b(Type::Callable, Member("create_mwnd"))});
      return &mp;
    }

    case Type::GroupList: {
      static const auto mp =
          make_flatmap<Builder>({id[-1] | b_index_array(Type::Group),
                                 id[1] | b_callable("alloc", Type::None),
                                 id[2] | b_callable("free", Type::None)});
      return &mp;
    }

    case Type::Group: {
      static const auto mp = make_flatmap<Builder>(
          {id[0] | b_callable("sel", Type::None),
           id[1] | b_callable("sel_cancel", Type::None),
           id[4] | b_callable("init", Type::None),
           id[2] | b_callable("start", Type::None),
           id[10] | b_callable("start_cancel", Type::None),
           id[5] | b_callable("end", Type::None),
           id[6] | b_callable("get_hit_no", Type::Int),
           id[7] | b_callable("get_pushed_no", Type::Int),
           id[3] | b_callable("get_decided_no", Type::Int),
           id[15] | b_callable("get_result", Type::Int),
           id[16] | b_callable("get_result_button_no", Type::Int),
           id[8] | obj_getset("order", Type::Int),
           id[13] | obj_getset("layer", Type::Int),
           id[14] | obj_getset("cancel_priority", Type::Int)});
      return &mp;
    }

    case Type::Screen: {
      static const auto mp =
          make_flatmap<Builder>({id[31] | b(Type::EffectList, Member("effect")),
                                 id[25] | b(Type::QuakeList, Member("quake"))});
      return &mp;
    }

    case Type::EffectList: {
      static const auto mp = make_flatmap<Builder>(
          {id[-1] | b_index_array(Type::Effect), id[1] | b_callable("resize"),
           id[2] | b_callable("size", Type::Int)});
      return &mp;
    }

    case Type::Effect: {
      static const auto mp = make_flatmap<Builder>(
          {id[30] | b_callable("init"),
           id[31] | obj_getset("wipe_copy", Type::Int),
           id[32] | obj_getset("wipe_erase", Type::Int),
           id[0] | obj_getset("x", Type::Int),
           id[1] | obj_getset("y", Type::Int),
           id[2] | obj_getset("z", Type::Int),
           id[3] | obj_getset("mono", Type::Int),
           id[4] | obj_getset("reverse", Type::Int),
           id[5] | obj_getset("bright", Type::Int),
           id[6] | obj_getset("dark", Type::Int),
           id[7] | obj_getset("color_r", Type::Int),
           id[8] | obj_getset("color_g", Type::Int),
           id[9] | obj_getset("color_b", Type::Int),
           id[10] | obj_getset("color_rate", Type::Int),
           id[11] | obj_getset("color_add_r", Type::Int),
           id[12] | obj_getset("color_add_g", Type::Int),
           id[13] | obj_getset("color_add_b", Type::Int),
           id[14] | b(Type::IntEvent, Member("x_eve")),
           id[15] | b(Type::IntEvent, Member("y_eve")),
           id[16] | b(Type::IntEvent, Member("z_eve")),
           id[17] | b(Type::IntEvent, Member("mono_eve")),
           id[18] | b(Type::IntEvent, Member("reverse_eve")),
           id[19] | b(Type::IntEvent, Member("bright_eve")),
           id[20] | b(Type::IntEvent, Member("dark_eve")),
           id[21] | b(Type::IntEvent, Member("color_r_eve")),
           id[22] | b(Type::IntEvent, Member("color_g_eve")),
           id[23] | b(Type::IntEvent, Member("color_b_eve")),
           id[24] | b(Type::IntEvent, Member("color_rate_eve")),
           id[25] | b(Type::IntEvent, Member("color_add_r_eve")),
           id[26] | b(Type::IntEvent, Member("color_add_g_eve")),
           id[27] | b(Type::IntEvent, Member("color_add_b_eve")),
           id[28] | obj_getset("begin_order", Type::Int),
           id[29] | obj_getset("end_order", Type::Int),
           id[33] | obj_getset("begin_layer", Type::Int),
           id[34] | obj_getset("end_layer", Type::Int)});
      return &mp;
    }

    case Type::QuakeList: {
      static const auto mp =
          make_flatmap<Builder>({id[-1] | b_index_array(Type::Quake)});
      return &mp;
    }

    case Type::Quake: {
      static const auto mp = make_flatmap<Builder>(
          {id[0] | b_callable("start", Type::None, NONSIMPLE),
           id[1] | b_callable("start_wait", Type::None, NONSIMPLE | AWAIT),
           id[2] | b_callable("start_wait_key", Type::None, NONSIMPLE | AWAIT),
           id[3] | b_callable("start_nowait", Type::None, NONSIMPLE),
           id[4] | b_callable("start_all", Type::None, NONSIMPLE),
           id[5] | b_callable("start_all_wait", Type::None, NONSIMPLE | AWAIT),
           id[6] |
               b_callable("start_all_wait_key", Type::None, NONSIMPLE | AWAIT),
           id[7] | b_callable("start_all_nowait", Type::None, NONSIMPLE),
           id[8] | b_callable("end", Type::None, NONSIMPLE),
           id[10] | b_callable("wait", Type::None, AWAIT),
           id[11] | b_callable("wait_key", Type::None, AWAIT),
           id[9] | b_callable("check", Type::Int)});
      return &mp;
    }

    case Type::Input: {
      static const auto mp =
          make_flatmap<Builder>({id[2] | b_callable("clear", Type::None),
                                 id[3] | b_callable("next", Type::None),
                                 id[0] | b(Type::Key, Member("decide")),
                                 id[1] | b(Type::Key, Member("cancel"))});
      return &mp;
    }

    case Type::KeyList: {
      static const auto mp = make_flatmap<Builder>(
          {id[-1] | b_index_array(Type::Key),
           id[0] | b_callable("wait", Type::None, AWAIT),
           id[1] | b_callable("wait_force", Type::None, AWAIT),
           id[3] | b_callable("clear", Type::None),
           id[5] | b_callable("next", Type::None)});
      return &mp;
    }

    case Type::Key: {
      static const auto mp = make_flatmap<Builder>(
          {id[1] | b_callable("on_down", Type::Int),
           id[4] | b_callable("on_up", Type::Int),
           id[5] | b_callable("on_down_up", Type::Int),
           id[6] | b_callable("is_down", Type::Int),
           id[7] | b_callable("is_up", Type::Int),
           id[10] | b_callable("on_flick", Type::Int),
           id[0] | b_callable("get_flick_angle", Type::Int),
           id[14] | b_callable("get_flick_pixel", Type::Int),
           id[15] | b_callable("get_flick_mm", Type::Int)});
      return &mp;
    }

    case Type::Mouse: {
      static const auto mp =
          make_flatmap<Builder>({id[4] | b_callable("clear", Type::None),
                                 id[8] | b_callable("next", Type::None),
                                 id[0] | b_callable("pos_x", Type::Int),
                                 id[1] | b_callable("pos_y", Type::Int),
                                 id[2] | b_callable("get_pos_x", Type::Int),
                                 id[3] | b_callable("get_pos_y", Type::Int),
                                 id[9] | b_callable("get_pos", Type::None),
                                 id[10] | b_callable("set_pos", Type::None),
                                 id[5] | b_callable("wheel", Type::Int),
                                 id[7] | b(Type::Key, Member("left")),
                                 id[6] | b(Type::Key, Member("right"))});
      return &mp;
    }

    case Type::ObjList: {
      static const auto mp =
          make_flatmap<Builder>({id[-1] | b_index_array(Type::Object),
                                 id[4] | b(Type::Callable, Member("resize")),
                                 id[3] | b(Type::Callable, Member("size"))});
      return &mp;
    }
    case Type::Object: {
      auto obj_createmov = [](std::string_view mem, Type ret = Type::None,
                              CallFlags flags = {}) -> Builder {
        return b_callable(mem, ret, NONSIMPLE | flags);
      };

      static const auto mp = make_flatmap<Builder>({
          id[56] | obj_getset("wipe_copy", Type::Int),
          id[92] | obj_getset("wipe_erase", Type::Int),
          id[139] | obj_getset("click_disable", Type::Int),
          id[0] | obj_getset("disp", Type::Int),
          id[1] | obj_getset("patno", Type::Int),
          id[44] | obj_getset("world", Type::Int),
          id[55] | obj_getset("order", Type::Int),
          id[2] | obj_getset("layer", Type::Int),
          id[3] | obj_getset("x", Type::Int),
          id[4] | obj_getset("y", Type::Int),
          id[5] | obj_getset("z", Type::Int),
          id[54] | b(Type::IntList, Member("x_rep")),
          id[63] | b(Type::IntList, Member("y_rep")),
          id[110] | b(Type::IntList, Member("z_rep")),
          id[48] | b(Type::Callable, Member("set_pos")),
          id[6] | obj_getset("center_x", Type::Int),
          id[7] | obj_getset("center_y", Type::Int),
          id[8] | obj_getset("center_z", Type::Int),
          id[158] | b(Type::Callable, Member("set_center")),
          id[9] | obj_getset("center_rep_x", Type::Int),
          id[10] | obj_getset("center_rep_y", Type::Int),
          id[11] | obj_getset("center_rep_z", Type::Int),
          id[159] | b(Type::Callable, Member("set_center_rep")),
          id[12] | obj_getset("scale_x", Type::Int),
          id[13] | obj_getset("scale_y", Type::Int),
          id[14] | obj_getset("scale_z", Type::Int),
          id[49] | b(Type::Callable, Member("set_scale")),
          id[15] | obj_getset("rotate_x", Type::Int),
          id[16] | obj_getset("rotate_y", Type::Int),
          id[17] | obj_getset("rotate_z", Type::Int),
          id[50] | b(Type::Callable, Member("set_rotate")),
          id[18] | obj_getset("clip_use", Type::Int),
          id[19] | obj_getset("clip_left", Type::Int),
          id[20] | obj_getset("clip_top", Type::Int),
          id[21] | obj_getset("clip_right", Type::Int),
          id[22] | obj_getset("clip_bottom", Type::Int),
          id[160] | b(Type::Callable, Member("set_clip")),
          id[149] | obj_getset("src_clip_use", Type::Int),
          id[150] | obj_getset("src_clip_left", Type::Int),
          id[151] | obj_getset("src_clip_top", Type::Int),
          id[152] | obj_getset("src_clip_right", Type::Int),
          id[153] | obj_getset("src_clip_bottom", Type::Int),
          id[161] | b(Type::Callable, Member("set_src_clip")),
          id[27] | obj_getset("tr", Type::Int),
          id[141] | b(Type::IntList, Member("tr_rep")),
          id[28] | obj_getset("mono", Type::Int),
          id[29] | obj_getset("reverse", Type::Int),
          id[30] | obj_getset("bright", Type::Int),
          id[31] | obj_getset("dark", Type::Int),
          id[32] | obj_getset("color_r", Type::Int),
          id[33] | obj_getset("color_g", Type::Int),
          id[34] | obj_getset("color_b", Type::Int),
          id[23] | obj_getset("color_rate", Type::Int),
          id[57] | obj_getset("color_add_r", Type::Int),
          id[58] | obj_getset("color_add_g", Type::Int),
          id[59] | obj_getset("color_add_b", Type::Int),
          id[145] | obj_getset("mask_no", Type::Int),
          id[109] | obj_getset("tonecurve_no", Type::Int),
          id[146] | obj_getset("culling", Type::Int),
          id[147] | obj_getset("alpha_test", Type::Int),
          id[148] | obj_getset("alpha_blend", Type::Int),
          id[46] | obj_getset("blend", Type::Int),
          id[168] | obj_getset("light_no", Type::Int),
          id[144] | obj_getset("fog_use", Type::Int),
          id[90] | b(Type::IntEvent, Member("patno_eve")),
          id[51] | b(Type::IntEvent, Member("x_eve")),
          id[64] | b(Type::IntEvent, Member("y_eve")),
          id[65] | b(Type::IntEvent, Member("z_eve")),
          id[112] | b(Type::IntEventList, Member("x_rep_eve")),
          id[113] | b(Type::IntEventList, Member("y_rep_eve")),
          id[114] | b(Type::IntEventList, Member("z_rep_eve")),
          id[77] | b(Type::IntEvent, Member("center_x_eve")),
          id[78] | b(Type::IntEvent, Member("center_y_eve")),
          id[79] | b(Type::IntEvent, Member("center_z_eve")),
          id[80] | b(Type::IntEvent, Member("center_rep_x_eve")),
          id[81] | b(Type::IntEvent, Member("center_rep_y_eve")),
          id[82] | b(Type::IntEvent, Member("center_rep_z_eve")),
          id[67] | b(Type::IntEvent, Member("scale_x_eve")),
          id[68] | b(Type::IntEvent, Member("scale_y_eve")),
          id[66] | b(Type::IntEvent, Member("scale_z_eve")),
          id[69] | b(Type::IntEvent, Member("rotate_x_eve")),
          id[70] | b(Type::IntEvent, Member("rotate_y_eve")),
          id[71] | b(Type::IntEvent, Member("rotate_z_eve")),
          id[105] | b(Type::IntEvent, Member("clip_left_eve")),
          id[106] | b(Type::IntEvent, Member("clip_top_eve")),
          id[107] | b(Type::IntEvent, Member("clip_right_eve")),
          id[108] | b(Type::IntEvent, Member("clip_bottom_eve")),
          id[154] | b(Type::IntEvent, Member("src_clip_left_eve")),
          id[155] | b(Type::IntEvent, Member("src_clip_top_eve")),
          id[156] | b(Type::IntEvent, Member("src_clip_right_eve")),
          id[157] | b(Type::IntEvent, Member("src_clip_bottom_eve")),
          id[72] | b(Type::IntEvent, Member("tr_eve")),
          id[140] | b(Type::IntEventList, Member("tr_rep_eve")),
          id[73] | b(Type::IntEvent, Member("mono_eve")),
          id[74] | b(Type::IntEvent, Member("reverse_eve")),
          id[75] | b(Type::IntEvent, Member("bright_eve")),
          id[76] | b(Type::IntEvent, Member("dark_eve")),
          id[87] | b(Type::IntEvent, Member("color_r_eve")),
          id[88] | b(Type::IntEvent, Member("color_g_eve")),
          id[89] | b(Type::IntEvent, Member("color_b_eve")),
          id[83] | b(Type::IntEvent, Member("color_rate_eve")),
          id[84] | b(Type::IntEvent, Member("color_add_r_eve")),
          id[85] | b(Type::IntEvent, Member("color_add_g_eve")),
          id[86] | b(Type::IntEvent, Member("color_add_b_eve")),
          id[91] | Builder([](Builder::Ctx& ctx) {
            const int peek = AsInt(ctx.elmcode[1]);
            switch (peek) {
              case 0:  // ALLEVENT_END
                ctx.chain.nodes.emplace_back(
                    Type::Callable, Member("alleve_end", Type::None, true));
                ctx.elmcode = ctx.elmcode.subspan(2);
                return;
              case 1:
                ctx.chain.nodes.emplace_back(
                    Type::Callable,
                    Member("alleve_wait", Type::None, true, true, true));
                ctx.elmcode = ctx.elmcode.subspan(2);
                return;
              case 2:
                ctx.chain.nodes.emplace_back(
                    Type::Callable, Member("alleve_check", Type::Int, true));
                ctx.elmcode = ctx.elmcode.subspan(2);
                return;
              default:
                ctx.chain.nodes.emplace_back(Type::Invalid,
                                             Member("alleve_???"));
                ctx.elmcode = ctx.elmcode.subspan(2);
                return;
            }
          }),
          id[169] | b(Type::Int, Member("pat_cnt")),

          id[24] | obj_getter("get_size_x"), id[25] | obj_getter("get_size_y"),
          id[47] | obj_getter("get_size_z"),
          id[100] | obj_getter("get_pixel_color_a"),
          id[101] | obj_getter("get_pixel_color_r"),
          id[102] | obj_getter("get_pixel_color_g"),
          id[103] | obj_getter("get_pixel_color_b"),

          id[62] | obj_getter("get_file_path", Type::String),
          id[111] | b(Type::IntList, Member("F")),

          id[93] | b(Type::ObjList, Member("child")),
          id[35] | b_callable("init", Type::None),
          id[36] | b_callable("free", Type::None),
          id[37] | b_callable("init_param", Type::None),
          id[38] | b(Type::Callable, Member("create")),
          id[40] | b(Type::Callable, Member("create_rect")),
          id[39] | b(Type::Callable, Member("create_string")),
          id[132] | b(Type::Callable, Member("create_number")),
          id[129] | b(Type::Callable, Member("create_weather")),
          id[43] | b(Type::Callable, Member("create_mesh")),
          id[45] | b(Type::Callable, Member("create_billboard")),
          id[116] | b(Type::Callable, Member("create_save_thumb")),
          id[170] | b(Type::Callable, Member("create_capture_thumb")),

          id[165] | b(Type::Callable, Member("create_capture")),
          id[120] | obj_createmov("create_movie"),
          id[121] | obj_createmov("create_movie_loop"),
          id[122] | obj_createmov("create_movie_wait", Type::None, AWAIT),
          id[143] | obj_createmov("create_movie_waitkey", Type::Int, AWAIT),
          id[177] | b(Type::Callable, Member("create_emote")),
          id[0x01000000] | b_callable("load_gan", Type::None),
          id[0x01000001] | b_callable("start_gan", Type::None),

          id[41] | b(Type::Callable, Member("copy_from")),
          id[53] | b(Type::Callable, Member("change_file")),
          id[174] | b_callable("exist_type", Type::Int),

          // String
          id[99] | b(Type::Callable, Member("set_string")),
          id[135] | b(Type::String, Member("get_string")),
          id[104] | b(Type::Callable, Member("set_string_param")),
          id[133] | b(Type::Callable, Member("set_number")),
          id[136] | b(Type::Int, Member("set_number")),
          id[134] | b(Type::Callable, Member("set_number_param")),

          // environment
          id[130] | b(Type::Callable, Member("set_weather_param_type_a")),
          id[131] | b(Type::Callable, Member("set_weather_param_type_a")),

          // movie
          id[125] | b_callable("pause_movie", Type::None),
          id[126] | b_callable("resume_movie", Type::None),
          id[137] | b(Type::Callable, Member("seek_movie")),
          id[138] | b_callable("get_movie_seek_time", Type::Int),
          id[127] | b_callable("check_movie", Type::Int),
          id[128] | b_callable("wait_movie", Type::None, AWAIT),
          id[142] | b_callable("wait_movie_key", Type::Int, AWAIT),
          id[171] | b_callable("end_movie_loop", Type::None),
          id[172] | b(Type::Callable, Member("set_movie_auto_free")),

          // frame action
          id[52] | b(Type::FrameAction, Member("frame_action")),
          id[115] | b(Type::FrameActionList, Member("frame_action_ch")),

          // button
          id[61] | b_callable("clear_button", Type::None),
          id[42] | b(Type::Callable, Member("set_button")),
          id[164] | b(Type::Callable, Member("set_button_group")),
          id[98] | b(Type::Callable, Member("set_button_pushkeep")),
          id[119] | b(Type::Int, Member("get_button_pushkeep")),
          id[175] | b(Type::Callable, Member("set_button_alpha_test")),
          id[176] | b(Type::Int, Member("get_button_alpha_test")),

          id[95] | b_callable("set_button_state_normal", Type::None),
          id[96] | b_callable("set_button_state_select", Type::None),
          id[97] | b_callable("set_button_state_disable", Type::None),
          id[118] | b_callable("get_button_state", Type::Int),
          id[123] | b_callable("get_button_hit_state", Type::Int),
          id[124] | b_callable("get_button_real_state", Type::Int),
          id[26] | b(Type::Callable, Member("set_button_call")),
          id[60] | b_callable("clear_button_call", Type::None)

          // GAN

      });
      return &mp;
    }

    case Type::MaskList: {
      static const auto mp =
          make_flatmap<Builder>({id[-1] | b_index_array(Type::Mask),
                                 id[1] | b_callable("size", Type::Int)});
      return &mp;
    }

    case Type::Mask: {
      static const auto mp = make_flatmap<Builder>(
          {id[1] | b_callable("init"), id[0] | b_callable("create", Type::None),
           id[4] | obj_getset("x", Type::Int),
           id[5] | obj_getset("y", Type::Int),
           id[2] | b(Type::IntEvent, Member("x_eve")),
           id[3] | b(Type::IntEvent, Member("y_eve"))});
      return &mp;
    }

    case Type::Movie: {
      static const auto mp = make_flatmap<Builder>(
          {id[0] | b_callable("play", Type::None),
           id[2] | b_callable("play_wait", Type::None, AWAIT),
           id[3] | b_callable("play_waitkey", Type::Int, AWAIT),
           id[1] | b_callable("stop", Type::None)});
      return &mp;
    }

    case Type::BgmTable: {
      static const auto mp = make_flatmap<Builder>(
          {id[0] | b(Type::Callable, Member("cnt")),
           id[2] | b(Type::Callable, Member("set_listen")),
           id[4] | b(Type::Callable, Member("set_listen_all")),
           id[1] | b(Type::Callable, Member("get_listen"))});
      return &mp;
    }

    case Type::Bgm: {
      static const auto mp = make_flatmap<Builder>(
          {id[0] | b_callable("play"),
           id[1] | b_callable("play_oneshot"),
           id[2] | b_callable("play_wait", Type::None, AWAIT),
           id[16] | b_callable("ready"),
           id[17] | b_callable("ready_oneshot"),
           id[4] | b_callable("stop"),
           id[10] | b_callable("pause"),
           id[11] | b_callable("resume"),
           id[12] | b_callable("resume_wait", Type::None, AWAIT),
           id[3] | b_callable("wait", Type::None, AWAIT),
           id[14] | b_callable("wait_key", Type::Int, AWAIT),
           id[5] | b_callable("wait_fade", Type::None, AWAIT),
           id[15] | b_callable("wait_fade_key", Type::Int, AWAIT),
           id[18] | b_callable("check", Type::Int),
           id[6] | b_callable("set_volume"),
           id[7] | b_callable("set_volume_max"),
           id[8] | b_callable("set_volume_min"),
           id[9] | b_callable("get_volume", Type::Int),
           id[19] | b_callable("get_regist_name", Type::String),
           id[13] | b_callable("get_play_pos", Type::Int)});
      return &mp;
    }

    case Type::Se: {
      static const auto mp = make_flatmap<Builder>(
          {id[0] | b_callable("play"), id[5] | b_callable("play_by_file_name"),
           id[6] | b_callable("play_by_koe_no"),
           id[9] | b_callable("play_by_se_no"), id[7] | b_callable("stop"),
           id[8] | b_callable("wait", Type::None, AWAIT),
           id[10] | b_callable("wait_key", Type::Int, AWAIT),
           id[11] | b_callable("check", Type::Int),
           id[1] | b_callable("set_volume"),
           id[2] | b_callable("set_volume_max"),
           id[3] | b_callable("set_volume_min"),
           id[4] | b_callable("get_volume", Type::Int)});
      return &mp;
    }

    case Type::MwndList: {
      static const auto mp = make_flatmap<Builder>(
          {id[-1] | b_index_array(Type::Mwnd),
           id[1] | b_callable("close", Type::None),
           id[2] | b_callable("close_wait", Type::None),
           id[3] | b_callable("close_nowait", Type::None)});
      return &mp;
    }

    case Type::Mwnd: {
      static const auto mp = make_flatmap<Builder>(
          {id[0] | b_callable("set_waku"),
           id[79] | b_callable("init_waku_file", Type::None),
           id[78] | b_callable("set_waku_file"),
           id[80] | b_callable("get_waku_file", Type::String),
           id[82] | b_callable("init_filter_file", Type::None),
           id[81] | b_callable("set_filter_file"),
           id[83] | b_callable("get_filter_file", Type::String),
           id[1] | b_callable("open", Type::None),
           id[15] | b_callable("open_wait", Type::None),
           id[16] | b_callable("open_nowait", Type::None),
           id[65] | b_callable("check_open", Type::Int),
           id[2] | b_callable("close", Type::None),
           id[13] | b_callable("close_wait", Type::None),
           id[14] | b_callable("close_nowait", Type::None),
           id[64] | b_callable("end_close", Type::None),
           id[49] | b_callable("msg_block", Type::None),
           id[59] | b_callable("msg_pp_block", Type::None),
           id[3] | b_callable("clear", Type::None),
           id[55] | b_callable("novel_clear", Type::None),
           id[4] | b_callable("print"),
           id[57] | b_callable({{0, "overflow_print"}, {1, "print"}}),
           id[63] | b_callable("overflow_name"),
           id[12] | b_callable({{0, "ruby_end"}, {std::nullopt, "ruby_start"}}),
           id[18] | b_callable("msg_wait", Type::None, AWAIT),
           id[19] | b_callable("pp", Type::None, AWAIT),
           id[20] | b_callable("r", Type::None, AWAIT),
           id[54] | b_callable("page", Type::None, AWAIT),
           id[6] | b_callable("nl", Type::None),
           id[17] | b_callable("nil", Type::None),
           id[56] | b_callable("indent", Type::None),
           id[28] | b_callable("clear_indent", Type::None),
           id[31] | b_callable("enable_multi_msg", Type::None),
           id[29] | b_callable("next_msg", Type::None),
           id[58] | b_callable("set_slide_msg"),
           id[60] | b_callable("set_slide_msg", Type::None),
           id[61] | b_callable("slide_msg", Type::None),

           // SEL
           id[5] | b(Type::Callable, Member("sel")),
           id[51] | b(Type::Callable, Member("sel_cancel")),
           id[50] | b(Type::Callable, Member("selmsg")),
           id[52] | b(Type::Callable, Member("selmsg_cancel")),
           id[84] | b_callable({{0, "rep_pos_default"}, {1, "rep_pos"}}),
           id[7] | b_callable({{0, "size_default"}, {1, "size"}}),
           id[8] | b_callable({{0, "color_default"}, {1, "color"}}),
           id[86] | b(Type::Callable, Member("msgbtn")),
           id[85] | b(Type::Callable, Member("set_namae")),
           id[9] | b_callable("koe", Type::None, NONSIMPLE),
           id[26] | b_callable("koe_play_wait", Type::None, NONSIMPLE | AWAIT),
           id[27] |
               b_callable("koe_play_wait_key", Type::Int, NONSIMPLE | AWAIT),
           id[22] | b(Type::None, Member("clear_face")),
           id[21] | b(Type::Callable, Member("set_face")),
           id[10] | b_callable({{0, "get_layer"}, {1, "set_layer"}}),
           id[11] | b_callable({{0, "get_world"}, {1, "set_world"}}),
           id[32] | b(Type::ObjList, Member("button")),
           id[53] | b(Type::ObjList, Member("face")),
           id[30] | b(Type::ObjList, Member("object")),

           // parameter
           id[66] | b(Type::None, Member("init_window_pos")),
           id[67] | b(Type::None, Member("init_window_size")),
           id[74] | b(Type::None, Member("init_window_moji_cnt")),
           id[68] | b(Type::Callable, Member("set_window_pos")),
           id[69] | b(Type::Callable, Member("set_window_size")),
           id[75] | b(Type::Callable, Member("set_window_moji_cnt")),
           id[70] | b(Type::Int, Member("get_window_pos_x")),
           id[71] | b(Type::Int, Member("get_window_pos_y")),
           id[72] | b(Type::Int, Member("get_window_size_x")),
           id[73] | b(Type::Int, Member("get_window_size_y")),
           id[77] | b(Type::Int, Member("get_window_moji_cnt_x")),
           id[76] | b(Type::Int, Member("get_window_moji_cnt_y")),
           id[41] | b(Type::None, Member("init_open_anime_type")),
           id[42] | b(Type::None, Member("init_open_anime_time")),
           id[43] | b(Type::None, Member("init_close_anime_type")),
           id[44] | b(Type::None, Member("init_close_anime_time")),
           id[37] | b(Type::Callable, Member("set_open_anime_type")),
           id[34] | b(Type::Callable, Member("set_open_anime_time")),
           id[45] | b(Type::Callable, Member("set_close_anime_type")),
           id[35] | b(Type::Callable, Member("set_close_anime_time")),
           id[48] | b(Type::Int, Member("get_open_anime_type")),
           id[47] | b(Type::Int, Member("get_open_anime_time")),
           id[38] | b(Type::Int, Member("get_close_anime_type")),
           id[46] | b(Type::Int, Member("get_close_anime_time")),
           id[36] | b(Type::Int, Member("get_default_open_anime_type")),
           id[33] | b(Type::Int, Member("get_default_open_anime_time")),
           id[40] | b(Type::Int, Member("get_default_close_anime_type")),
           id[39] | b(Type::Int, Member("get_default_close_anime_time"))});
      return &mp;
    }

    case Type::Msgbk: {
      static const auto mp = make_flatmap<Builder>(
          {id[1] | b_callable("insert_msg", Type::None),
           id[4] | b_callable("add_koe", Type::None),
           id[5] | b_callable("add_namae", Type::None),
           id[3] | b_callable("add_msg", Type::None),
           id[2] | b_callable("go_next_msg", Type::None)});
      return &mp;
    }

    case Type::Pcm: {
      static const auto mp =
          make_flatmap<Builder>({id[0] | b(Type::Callable, Member("play")),
                                 id[1] | b(Type::None, Member("stop"))});
      return &mp;
    }

    case Type::PcmchList: {
      static const auto mp =
          make_flatmap<Builder>({id[-1] | b_index_array(Type::Pcmch)});
      return &mp;
    }

    case Type::Pcmch: {
      static const auto mp = make_flatmap<Builder>(
          {id[0] | b_callable("play"), id[2] | b_callable("play_loop"),
           id[1] | b_callable("play_wait", Type::None, AWAIT),
           id[11] | b_callable("ready"), id[16] | b_callable("ready_loop"),
           id[5] | b_callable("stop"), id[10] | b_callable("pause"),
           id[9] | b_callable("resume"),
           id[17] | b_callable("resume_wait", Type::None, AWAIT),
           id[3] | b_callable("wait", Type::None, AWAIT),
           id[6] | b_callable("wait_key", Type::Int, AWAIT),
           id[8] | b_callable("wait_fade", Type::None, AWAIT),
           id[7] | b_callable("wait_fade_key", Type::Int, AWAIT),
           id[4] | b_callable("check", Type::Int),
           id[13] | b_callable("set_volume"),
           id[14] | b_callable("set_vol_max"),
           id[15] | b_callable("set_vol_min"),
           id[12] | b_callable("get_volume", Type::Int)});
      return &mp;
    }

    case Type::Wipe: {
      static const auto mp = make_flatmap<Builder>(
          {id[7] | b_callable("wipe", Type::None, AWAIT | NONSIMPLE),
           id[23] | b_callable("wipe_all", Type::None, NONSIMPLE | AWAIT),
           id[51] | b_callable("wipe_mask", Type::None, AWAIT | NONSIMPLE),
           id[50] | b_callable("wipe_mask_all", Type::None, AWAIT | NONSIMPLE),
           id[33] | b_callable("end", Type::None, NONSIMPLE),
           id[103] | b_callable("wait", Type::Int, AWAIT),
           id[109] | b_callable("check", Type::Int)});
      return &mp;
    }

    [[unlikely]]
    default:
      return nullptr;
  }
}

// ------------------------------------------------------------------------------
// ==============================================================================
// ElementParser public interface
ElementParser::ElementParser(std::span<const Property> scene_properties,
                             std::span<const Property> global_properties,
                             std::span<const Command> scene_commands,
                             std::span<const Command> global_commands,
                             const std::vector<Type>& curcall_args,
                             int scene_id,
                             std::function<int()> read_kidoku,
                             std::function<void(std::string)> warn)
    : scene_properties_(scene_properties),
      global_properties_(global_properties),
      scene_commands_(scene_commands),
      global_commands_(global_commands),
      curcall_args_(curcall_args),
      scene_id_(scene_id),
      read_kidoku_(std::move(read_kidoku)),
      warn_(std::move(warn)) {
  if (!read_kidoku_)
    read_kidoku_ = [] { return 0; };
  if (!warn_)
    warn_ = [](std::string) {};
}
ElementParser::~ElementParser() = default;

AccessChain ElementParser::Parse(ElementCode& elm) {
  const int first = elm.At<int>(0);
  const int flag = first >> 24;
  const size_t idx = first ^ (flag << 24);

  AccessChain result;
  if (flag == USER_COMMAND_FLAG)
    result = resolve_usrcmd(elm, idx);
  else if (flag == USER_PROPERTY_FLAG)
    result = resolve_usrprop(elm, idx);
  else
    result = resolve_element(elm);

  const bool explicit_bind = elm.force_bind;
  const Member* call_member = nullptr;
  if (!result.nodes.empty()) {
    const auto* member = std::get_if<Member>(&result.nodes.back().var);
    if (member)
      call_member = member;
  }
  const bool can_implicit_call = call_member && call_member->implicit_call;
  const bool is_implicit_call = !explicit_bind && can_implicit_call;

  if (elm.bind_ctx.Empty())
    elm.force_bind = false;
  if (elm.force_bind || result.GetType() == Type::Callable ||
      can_implicit_call) {
    if (result.GetType() != Type::Callable && !can_implicit_call)
      warn_(std::format("[ElementParser] cannot bind {} to {}",
                        elm.bind_ctx.ToDebugString(), result.ToDebugString()));
    else {
      if (can_implicit_call && elm.bind_ctx.return_type == Type::None)
        elm.bind_ctx.return_type = call_member->call_return_type;
      CallFlags flags;
      if (call_member) {
        flags.is_explicit = !is_implicit_call;
        // simple callable should be the default
        flags.is_nonsimple = !call_member->is_simple;
        flags.await_result = call_member->await_result;
      }
      result.nodes.emplace_back(
          Node::BuildCall(std::move(elm.bind_ctx), flags));
    }
  }

  return result;
}

AccessChain ElementParser::resolve_usrcmd(ElementCode& elm, size_t idx) {
  Usrcmd usrcmd;

  const libsiglus::Command* cmd = nullptr;
  if (idx < global_commands_.size())
    cmd = &global_commands_[idx];
  else
    cmd = &scene_commands_[idx - global_commands_.size()];

  usrcmd.scene = cmd->scene_id;
  usrcmd.entry = cmd->offset;
  usrcmd.name = cmd->name;
  usrcmd.arguments = std::move(elm.bind_ctx);

  elm.force_bind = false;
  return AccessChain{.root = std::move(usrcmd), .nodes = {}};
}

AccessChain ElementParser::resolve_usrprop(ElementCode& elm, size_t idx) {
  elm::Usrprop root;
  Type root_type;

  if (idx < global_properties_.size()) {
    const auto& incprop = global_properties_[idx];
    root.name = incprop.name;
    root_type = incprop.form;
    root.scene = -1;  // global
    root.idx = idx;
  } else {
    idx -= global_properties_.size();
    const auto& usrprop = scene_properties_[idx];
    root.name = usrprop.name;
    root_type = usrprop.form;
    root.scene = scene_id_;
    root.idx = idx;
  }

  return make_chain(root_type, std::move(root), elm, 1);
}

AccessChain ElementParser::resolve_element(ElementCode& elm) {
  auto elm_iv = elm.IntegerView();
  int root = elm_iv.front();

  switch (root) {
      // ====== Memory Banks ======
    case 25:  // A
      return make_sym_chain(Type::IntList, "A", elm, 1);
    case 26:  // B
      return make_sym_chain(Type::IntList, "B", elm, 1);
    case 27:  // C
      return make_sym_chain(Type::IntList, "C", elm, 1);
    case 28:  // D
      return make_sym_chain(Type::IntList, "D", elm, 1);
    case 29:  // E
      return make_sym_chain(Type::IntList, "E", elm, 1);
    case 30:  // F
      return make_sym_chain(Type::IntList, "F", elm, 1);
    case 137:  // X
      return make_sym_chain(Type::IntList, "X", elm, 1);
    case 31:  // G
      return make_sym_chain(Type::IntList, "G", elm, 1);
    case 32:  // Z
      return make_sym_chain(Type::IntList, "Z", elm, 1);

    case 34:  // S
      return make_sym_chain(Type::StrList, "S", elm, 1);
    case 35:  // M
      return make_sym_chain(Type::StrList, "M", elm, 1);
    case 106:  // NAMAE_LOCAL
      return make_sym_chain(Type::StrList, "LN", elm, 1);
    case 107:  // NAMAE_GLOBAL
      return make_sym_chain(Type::StrList, "GN", elm, 1);

      // ====== CUR_CALL (Special Case) ======
    case 83: {  // CUR_CALL
      const int elmcall = elm_iv[1];
      if ((elmcall >> 24) == 0x7d) {
        auto id = (elmcall ^ (0x7d << 24));
        return make_chain(curcall_args_[id], elm::Arg(id), elm, 2);
      }

      else if (elmcall == 0)
        return make_sym_chain(Type::IntList, "L", elm, 2);

      else if (elmcall == 1)
        return make_sym_chain(Type::StrList, "K", elm, 2);
    } break;

      // ====== Sound ======
    case 42:  // BGM
      return make_sym_chain(Type::Bgm, "bgm", elm, 1);
    case 123:  // BGMTABLE
      return make_sym_chain(Type::BgmTable, "bgm_table", elm, 1);
    case 43:
      return make_sym_chain(Type::Pcm, "pcm", elm, 1);
    case 44:
      return make_sym_chain(Type::PcmchList, "pcmch_list", elm, 1);
    case 45:
      return make_sym_chain(Type::Se, "se", elm, 1);
    case 24:
      return make_sym_chain(Type::KeyList, "key", elm, 1);
    case 46:
      return make_sym_chain(Type::Mouse, "mouse", elm, 1);
    case 86:
      return make_sym_chain(Type::Input, "input", elm, 1);

      // ====== SEL ======
      // some needs kidoku flag
    case 19:   // SEL
    case 101:  // SEL_CANCEL
      read_kidoku_();
      break;

    case 100:  // SELMSG
    case 102:  // SELMSG_CANCEL
      read_kidoku_();
      break;

    case 76:   // SELBTN
    case 77:   // SELBTN_READY
    case 126:  // SELBTN_CANCEL
    case 128:  // SELBTN_CANCEL_READY
    case 127:  // SELBTN_START
    {
      if (root == 76 || root == 126 || root == 127)
        read_kidoku_();
      break;
    }

      // ====== Title ======
    case 74: {  // SET_TITLE
      Member set_title("set_title");
      auto call = Node::BuildCall(elm.bind_ctx);
      elm.force_bind = false;
      return AccessChain{.root = std::monostate(),
                         .nodes = {Node(Type::Callable, std::move(set_title)),
                                   std::move(call)}};
    }
    case 75: {  // GET_TITLE
      Member get_title{"get_title", Type::String, true};
      AccessChain chain{.root = std::monostate(),
                        .nodes = {Node(Type::Callable, std::move(get_title))}};
      return make_chain(std::move(chain), elm,
                        std::span(elm.code.begin() + 1, elm.code.end()));
    }

    case 36:   // SAVEPOINT
    case 112:  // CHECK_SAVEPOINT
    case 113:  // CLEAR_SAVEPOINT
    {
      std::string_view name = root == 36    ? "savepoint"
                              : root == 112 ? "check_savepoint"
                                            : "clear_savepoint";
      Type return_type = root == 113 ? Type::None : Type::Int;
      elm.bind_ctx.return_type = return_type;
      auto call = Node::BuildCall(std::move(elm.bind_ctx));
      elm.force_bind = false;
      return AccessChain{
          .root = std::monostate(),
          .nodes = {Node(Type::Callable, Member(name)), std::move(call)}};
    }

      // ====== MWND ======
    case 22: {  // SET_WAKU
      elm.code.front() = Value(Integer(0));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 9: {  // OPEN
      elm.code.front() = Value(Integer(1));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 58: {  // OPEN_WAIT
      elm.code.front() = Value(Integer(15));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 59: {  // OPEN_NOWAIT
      elm.code.front() = Value(Integer(16));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 10: {  // CLOSE
      elm.code.front() = Value(Integer(2));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 56: {  // CLOSE_WAIT
      elm.code.front() = Value(Integer(13));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 57: {  // CLOSE_NOWAIT
      elm.code.front() = Value(Integer(14));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 125: {  // END_CLOSE
      elm.code.front() = Value(Integer(64));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 84: {  // MSG_BLOCK
      elm.code.front() = Value(Integer(49));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 121: {  // MSG_PP_BLOCK
      elm.code.front() = Value(Integer(59));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 11: {  // CLEAR
      elm.code.front() = Value(Integer(3));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 156: {  // SET_NAMAE
      elm.code.front() = Value(Integer(85));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 12: {  // PRINT
      elm.code.front() = Value(Integer(4));
      auto result = make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
      result.kidoku = read_kidoku_();
      return result;
    }
    case 61: {  // RUBY
      elm.code.front() = Value(Integer(12));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 47: {  // MSGBTN
      elm.code.front() = Value(Integer(86));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 15: {  // NL
      elm.code.front() = Value(Integer(6));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 62: {  // NLI
      elm.code.front() = Value(Integer(17));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 119: {  // INDENT
      elm.code.front() = Value(Integer(56));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 94: {  // CLEAR_INDENT
      elm.code.front() = Value(Integer(28));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 21: {  // WAIT_MSG
      elm.code.front() = Value(Integer(18));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 13: {  // PP
      elm.code.front() = Value(Integer(19));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 14: {  // R
      elm.code.front() = Value(Integer(20));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 115: {  // PAGE
      elm.code.front() = Value(Integer(54));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 151: {  // REP_POS
      elm.code.front() = Value(Integer(84));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 16: {  // SIZE
      elm.code.front() = Value(Integer(7));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 17: {  // COLOR
      elm.code.front() = Value(Integer(8));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 95: {  // MULTI_MSG
      elm.code.front() = Value(Integer(31));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 93: {  // NEXT_MSG
      elm.code.front() = Value(Integer(29));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 120: {  // START_SLIDE_MSG
      elm.code.front() = Value(Integer(58));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 122: {  // END_SLIDE_MSG
      elm.code.front() = Value(Integer(60));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 18:  // KOE
      elm.code.front() = Value(Integer(9));
      goto KOE;

    case 90:  // KOE_PLAY_WAIT
      elm.code.front() = Value(Integer(26));
      goto KOE;

    case 91:  // KOE_PLAY_WAIT_KEY
      elm.code.front() = Value(Integer(27));
      goto KOE;
    KOE: {
      auto result = make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
      result.kidoku = read_kidoku_();
      return result;
    }

    case 87:  // EXKOE
    case 88:  // EXKOE_PLAY_WAIT
    case 89:  // EXKOE_PLAY_WAIT_KEY
    {
      std::string_view name = root == 87   ? "exkoe"
                              : root == 88 ? "exkoe_play_wait"
                                           : "exkoe_play_wait_key";
      CallFlags flags = root == 87 ? CallFlags{} : AWAIT;
      elm.bind_ctx.return_type = Type::Int;
      auto call = Node::BuildCall(std::move(elm.bind_ctx), flags);
      elm.force_bind = false;
      return AccessChain{
          .root = std::monostate(),
          .nodes = {Node(Type::Callable, Member(name)), std::move(call)}};
    }

    case 68: {  // KOE_STOP
      Member koe_stop("koe_stop");
      auto call = Node::BuildCall(std::move(elm.bind_ctx));
      elm.force_bind = false;
      return AccessChain{.root = std::monostate(),
                         .nodes = {Node(Type::Callable, std::move(koe_stop)),
                                   std::move(call)}};
    }

    case 4: {  // JUMP
      Member jump("jump");
      elm.bind_ctx.return_type = Type::None;
      auto call = Node::BuildCall(std::move(elm.bind_ctx));
      elm.force_bind = false;
      return AccessChain{
          .root = std::monostate(),
          .nodes = {Node(Type::Callable, std::move(jump)), std::move(call)}};
    }

    case 116: {  // GET_MWND
      Member get_mwnd("get_mwnd", Type::Int, true);
      elm.bind_ctx.return_type = Type::Int;
      auto call = Node::BuildCall(std::move(elm.bind_ctx));
      elm.force_bind = false;
      return AccessChain{.root = std::monostate(),
                         .nodes = {Node(Type::Callable, std::move(get_mwnd)),
                                   std::move(call)}};
    }

    case 6: {  // FRAME
      Member frame("frame");
      auto call = Node::BuildCall(std::move(elm.bind_ctx));
      elm.force_bind = false;
      return AccessChain{
          .root = std::monostate(),
          .nodes = {Node(Type::Callable, std::move(frame)), std::move(call)}};
    }

    case 80:   // CAPTURE
    case 163:  // CAPTURE_FROM_FILE
    case 81:   // CAPTURE_FREE
    case 130:  // CAPTURE_FOR_OBJECT
    case 136:  // CAPTURE_FOR_OBJECT_FREE
    case 164:  // CAPTURE_FOR_TWEET
    case 165:  // CAPTURE_FREE_FOR_TWEET
    {
      std::string_view name = root == 80    ? "capture"
                              : root == 163 ? "capture_from_file"
                              : root == 81  ? "capture_free"
                              : root == 130 ? "capture_for_object"
                              : root == 136 ? "capture_for_object_free"
                              : root == 164 ? "capture_for_tweet"
                                            : "capture_free_for_tweet";
      elm.bind_ctx.return_type = Type::None;
      auto call = Node::BuildCall(std::move(elm.bind_ctx));
      elm.force_bind = false;
      return AccessChain{
          .root = std::monostate(),
          .nodes = {Node(Type::Callable, Member(name)), std::move(call)}};
    }

      // ====== Uncategorized ======
    case 5: {  // FARCALL
      auto& bind = elm.bind_ctx;

      Farcall farcall;
      if (Typeof(bind.arg[0]) != Type::String)
        warn_("[Farcall] expected string, but got: " + ToString(bind.arg[0]));
      farcall.scn_name = bind.arg[0];

      if (bind.overload_id == 1) {  // additionally has zlabel and arguments
        if (Typeof(bind.arg[1]) != Type::Int)
          warn_("[Farcall] expected int, but got: " + ToString(bind.arg[1]));

        farcall.zlabel = bind.arg[1];
        for (auto& arg : std::views::drop(bind.arg, 2)) {
          switch (Typeof(arg)) {
            case Type::Int:
              farcall.intargs.emplace_back(std::move(arg));
              break;
            case Type::String:
              farcall.strargs.emplace_back(std::move(arg));
              break;
            default:
              throw std::runtime_error(
                  "Farcall: Expected int or str, but got " + ToString(arg));
          }
        }
      }

      elm.force_bind = false;
      return AccessChain{.root = std::move(farcall)};
    }

    case 7:    // WIPE
    case 23:   // WIPE_ALL
    case 51:   // MASK_WIPE
    case 50:   // MASK_WIPE_ALL
    case 33:   // WIPE_END
    case 103:  // WAIT_WIPE
    case 109:  // CHECK_WIPE
      return make_sym_chain(Type::Wipe, "wipe", elm, 0);

    case 49:  // STAGE
      return make_sym_chain(Type::StageList, "stage", elm, 1);
    case 37:  // BACK
      return make_stage_member_chain("back", elm, 1);
    case 38:  // FRONT
      return make_stage_member_chain("front", elm, 1);
    case 73:  // NEXT
      return make_stage_member_chain("next", elm, 1);

    case 70:  // SCREEN
      return make_sym_chain(Type::Screen, "screen", elm, 1);

    case 65:  // EXCALL
      return make_sym_chain(Type::Excall, "excall", elm, 1);

    case 135:  // MASK
      return make_sym_chain(Type::MaskList, "mask", elm, 1);

    case 63:  // SYSCOM
      return make_sym_chain(Type::Syscom, "syscom", elm, 1);
    case 64:  // SCRIPT
      return make_sym_chain(Type::Script, "script", elm, 1);

    case 54: {  // WAIT
      Member wait("wait");
      wait.call_return_type = Type::None;
      wait.implicit_call = true;
      wait.await_result = true;
      elm.bind_ctx.return_type = Type::None;
      auto call = Node::BuildCall(std::move(elm.bind_ctx), AWAIT);
      return AccessChain{
          .root = std::monostate(),
          .nodes = {Node(Type::Callable, std::move(wait)), std::move(call)}};
    }
    case 55: {  // WAIT_KEY
      Member wait("wait_key");
      wait.call_return_type = Type::Int;
      wait.implicit_call = true;
      wait.await_result = true;
      elm.bind_ctx.return_type = Type::Int;
      auto call = Node::BuildCall(std::move(elm.bind_ctx), AWAIT);
      return AccessChain{
          .root = std::monostate(),
          .nodes = {Node(Type::Callable, std::move(wait)), std::move(call)}};
    }

    case 92:  // SYSTEM
      return make_sym_chain(Type::System, "system", elm, 1);

    case 39:  // MATH
      return make_sym_chain(Type::Math, "math", elm, 1);

    case 40:  // COUNTER
      return make_sym_chain(Type::CounterList, "counter", elm, 1);

    case 79:  // FRAME_ACTION
      return make_sym_chain(Type::FrameAction, "frame_action", elm, 1);
    case 53:  // FRAME_ACTION_CH
      return make_sym_chain(Type::FrameActionList, "frame_action_ch", elm, 1);

    case 145:  // MSGBK
      return make_sym_chain(Type::Msgbk, "msgbk", elm, 1);

    case 20:  // MOVIE
      return make_sym_chain(Type::Movie, "mov", elm, 1);

    [[unlikely]]
    default: {
      std::string msg = "[ElementParser] Unable to parse element: ";
      for (const Value& it : elm.code)
        msg += '<' + ToString(it) + '>';
      warn_(std::move(msg));
    } break;
  }

  return AccessChain();
}

AccessChain ElementParser::make_chain(AccessChain result,
                                      ElementCode& elm,
                                      std::span<const Value> elmcode) {
  result.nodes.reserve(elmcode.size());

  flat_map<Builder> const* mp;
  Builder::Ctx ctx{
      .elm = elm,
      .elmcode = elmcode,
      .chain = result,
      .Warn = [&](std::string msg) { this->warn_(std::move(msg)); }};
  while ((mp = elm::GetMethodMap(result.GetType())) != nullptr) {
    if (!mp || elmcode.empty())
      break;
    if (!std::holds_alternative<Integer>(elmcode.front()))
      break;
    const int key = AsInt(elmcode.front());
    if (!mp->contains(key))
      break;

    mp->at(key).Build(ctx);
  }

  if (!elmcode.empty()) {
    std::string msg = "[ElementParser] leftovers: ";
    msg += Join(",", std::views::all(elmcode) |
                         std::views::transform(
                             [](const Value& v) { return ToString(v); }));
    warn_(std::move(msg));
  }

  return result;
}

AccessChain ElementParser::make_chain(Type root_type,
                                      Root::var_t root_node,
                                      ElementCode& elm,
                                      size_t subidx) {
  Root root(root_type, std::move(root_node));
  AccessChain result{.root = std::move(root), .nodes = {}};
  return make_chain(std::move(result), elm,
                    std::span{elm.code}.subspan(subidx));
}

AccessChain ElementParser::make_sym_chain(Type type,
                                          std::string_view top_id,
                                          ElementCode& elm,
                                          size_t subidx) {
  Root root(Type::None, std::monostate());
  Node nd(type, Member(top_id));
  AccessChain result{.root = std::move(root), .nodes = {std::move(nd)}};
  return make_chain(std::move(result), elm,
                    std::span{elm.code}.subspan(subidx));
}

AccessChain ElementParser::make_stage_member_chain(std::string_view member,
                                                   ElementCode& elm,
                                                   size_t subidx) {
  Root root(Type::None, std::monostate());
  AccessChain result{.root = std::move(root),
                     .nodes = {Node(Type::StageList, Member("stage")),
                               Node(Type::Stage, Member(member))}};
  return make_chain(std::move(result), elm,
                    std::span{elm.code}.subspan(subidx));
}

}  // namespace libsiglus::elm
