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
#include "utilities/string_utilities.hpp"

#include <algorithm>
#include <format>
#include <functional>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <variant>

namespace libsiglus::elm {

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
  if (elm.code.empty())
    throw std::runtime_error("empty element access chain");

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
        flags.context = call_member->selection_context;
      }
      result.nodes.emplace_back(
          Node::BuildCall(std::move(elm.bind_ctx), flags));
    }
  }

  if (!result.nodes.empty()) {
    if (auto* call = std::get_if<Call>(&result.nodes.back().var);
        call && call->context) {
      result.kidoku = read_kidoku_();
      call->kidoku = result.kidoku;
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
    case 19:
    case 101:
    case 100:
    case 102:
    case 76:
    case 77:
    case 126:
    case 128:
    case 127:
    case 162: {
      const std::string_view name = root == 19    ? "sel"
                                    : root == 101 ? "sel_cancel"
                                    : root == 100 ? "selmsg"
                                    : root == 102 ? "selmsg_cancel"
                                    : root == 76  ? "selbtn"
                                    : root == 77  ? "selbtn_cancel"
                                    : root == 126 ? "selbtn_ready"
                                    : root == 128 ? "selbtn_cancel_ready"
                                    : root == 127 ? "selbtn_start"
                                                  : "get_last_sel_msg";
      const bool ready = root == 126 || root == 128;
      const bool getter = root == 162;
      const bool blocking = !ready && !getter;
      const Type type = getter ? Type::String : ready ? Type::None : Type::Int;
      Member member{name, type, true, getter, blocking, blocking};
      return AccessChain{.root = std::monostate(),
                         .nodes = {Node(Type::Callable, member)}};
    }
    case 114: {
      return AccessChain{
          .root = std::monostate(),
          .nodes = {
              Node(Type::Msgbk, Member{"msgbk"}),
              Node(Type::Callable, Member{"insert_img", Type::None, true})}};
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

    case 1:    // SELPOINT
    case 110:  // CHECK_SELPOINT
    case 111:  // CLEAR_SELPOINT
    case 149:  // STACK_SELPOINT
    case 150:  // DROP_SELPOINT
    {
      std::string_view name = root == 1     ? "selpoint"
                              : root == 110 ? "check_selpoint"
                              : root == 111 ? "clear_selpoint"
                              : root == 149 ? "stack_selpoint"
                                            : "drop_selpoint";
      // The existence check produces an integer despite the legacy element
      // declaration marking all selection-point commands as void.
      elm.bind_ctx.return_type = root == 110 ? Type::Int : Type::None;
      auto call = Node::BuildCall(std::move(elm.bind_ctx));
      elm.force_bind = false;
      return AccessChain{
          .root = std::monostate(),
          .nodes = {Node(Type::Callable, Member(name)), std::move(call)}};
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
    case 41: {  // SET_FACE
      elm.code.front() = Value(Integer(21));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
    case 72: {  // CLEAR_FACE
      elm.code.front() = Value(Integer(22));
      return make_sym_chain(Type::Mwnd, "mwnd", elm, 0);
    }
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

    case 2:    // OWARI
    case 3: {  // RETURNMENU
      elm.bind_ctx.return_type = Type::None;
      auto call = Node::BuildCall(std::move(elm.bind_ctx));
      elm.force_bind = false;
      return AccessChain{
          .root = std::monostate(),
          .nodes = {
              Node(Type::Callable, Member(root == 2 ? "owari" : "returnmenu")),
              std::move(call)}};
    }
    case 4: {  // JUMP
      const auto& bind = elm.bind_ctx;
      if (bind.arg.empty() || bind.arg.size() > 2 || !bind.named_arg.empty())
        throw std::runtime_error("invalid jump arguments");
      Jump jump{.scene = bind.arg[0]};
      if (bind.arg.size() == 2)
        jump.zlabel = bind.arg[1];
      elm.force_bind = false;
      return AccessChain{.root = Root(Type::None, std::move(jump))};
    }

    case 8: {  // SET_MWND
      Member set_mwnd("set_mwnd");
      set_mwnd.is_simple = false;
      elm.bind_ctx.return_type = Type::None;
      return AccessChain{.root = std::monostate(),
                         .nodes = {Node(Type::Callable, std::move(set_mwnd))}};
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

    case 6:   // FRAME
    case 96:  // DISP
    {
      Member frame(root == 6 ? "frame" : "disp");
      elm.bind_ctx.return_type = Type::None;
      auto call = Node::BuildCall(std::move(elm.bind_ctx), AWAIT);
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
    case 144:  // CAPTURE_FOR_LOCAL_SAVE
    {
      std::string_view name = root == 80    ? "capture"
                              : root == 163 ? "capture_from_file"
                              : root == 81  ? "capture_free"
                              : root == 130 ? "capture_for_object"
                              : root == 136 ? "capture_for_object_free"
                              : root == 164 ? "capture_for_tweet"
                              : root == 144 ? "capture_for_local_save"
                                            : "capture_free_for_tweet";
      elm.bind_ctx.return_type = root == 144 ? Type::Int : Type::None;
      const bool waits =
          root == 80 || root == 130 || root == 164 || root == 144;
      auto flags = waits ? AWAIT : CallFlags{};
      if (root == 130 || root == 144)
        flags = flags | NONSIMPLE;
      auto call = Node::BuildCall(std::move(elm.bind_ctx), flags);
      elm.force_bind = false;
      return AccessChain{
          .root = std::monostate(),
          .nodes = {Node(Type::Callable, Member(name)), std::move(call)}};
    }

      // ====== Uncategorized ======
    case 5: {  // FARCALL
      auto& bind = elm.bind_ctx;

      if ((bind.overload_id == 0 && bind.arg.size() != 1) ||
          (bind.overload_id == 1 && bind.arg.size() < 2) ||
          (bind.overload_id != 0 && bind.overload_id != 1))
        throw std::runtime_error("invalid farcall arguments");
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

    case 124:  // G00BUF
      return make_sym_chain(Type::G00bufList, "g00buf", elm, 1);

    case 63:  // SYSCOM
      if (elm.code.size() == 2 && elm.At<int>(1) == 236) {
        elm.code.front() = Integer{5};  // FARCALL
        auto chain = resolve_element(elm);
        elm.code.front() = Integer{63};
        std::get<Farcall>(chain.root.var).extended = true;
        return chain;
      }
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

    case 105:  // DATABASE
      return make_sym_chain(Type::DatabaseList, "database", elm, 1);

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

  auto append_node = [&](Type type, Node::var_t node) {
    result.nodes.emplace_back(type, std::move(node));
    elmcode = elmcode.subspan(1);
  };
  auto append_callable = [&](std::string_view member,
                             Type return_type = Type::None,
                             CallFlags flags = {}) {
    append_node(Type::Callable,
                Member{member, return_type, !flags.is_explicit,
                       !flags.is_nonsimple, flags.await_result, flags.context});
  };
  auto parse_database_call = [&](std::string_view member, Type return_type,
                                 std::initializer_list<Type> args) {
    auto database_error = [&](std::string_view reason) {
      std::string code;
      for (const auto& value : elm.code)
        code += '<' + ToString(value) + '>';
      throw std::runtime_error(
          std::format("database element {}: {}", code, reason));
    };

    if (elmcode.size() != 1)
      database_error("unexpected trailing members");
    auto& bind = elm.bind_ctx;
    if (bind.overload_id != 0 || !bind.named_arg.empty() ||
        bind.arg.size() != args.size())
      database_error("invalid database call signature");
    size_t index = 0;
    for (Type type : args)
      if (Typeof(bind.arg[index++]) != type)
        database_error("incorrect database argument type");
    if (bind.return_type != Type::None && bind.return_type != return_type)
      database_error("incorrect database result type");
    bind.return_type = return_type;
    append_callable(member, return_type);
  };
  auto parse_property = [&](std::string_view member, Type type,
                            bool writable = false) {
    // A bind on a longer chain belongs to its final command, not this member.
    if (elmcode.size() == 1 && elm.force_bind) {
      const auto& bind = elm.bind_ctx;
      if (!bind.named_arg.empty())
        throw std::runtime_error(std::format(
            "property '{}' does not accept tagged arguments", member));
      if (bind.overload_id == 0) {
        if (!bind.arg.empty())
          throw std::runtime_error(
              std::format("property '{}' getter expects no arguments", member));
      } else if (bind.overload_id == 1 && writable) {
        if (bind.arg.size() != 1)
          throw std::runtime_error(
              std::format("property '{}' setter expects one argument", member));
        result.nodes.emplace_back(Type::None,
                                  SetMember{member, bind.arg.front()});
        elm.force_bind = false;
        elmcode = elmcode.subspan(1);
        return;
      } else {
        throw std::runtime_error(std::format("property '{}' has no overload {}",
                                             member, bind.overload_id));
      }
      elm.force_bind = false;
    }
    result.nodes.emplace_back(type, Member{member});
    elmcode = elmcode.subspan(1);
  };
  auto parse_index = [&](Type t) {
    Subscript subscript{Integer(-1)};
    if (elmcode.size() < 2) {
      warn_("[IndexArray] expected index");
      result.nodes.emplace_back(t, std::move(subscript));
      elmcode = elmcode.subspan(elmcode.size());
      return;
    }

    subscript.idx = elmcode[1];
    result.nodes.emplace_back(t, std::move(subscript));
    elmcode = elmcode.subspan(2);
  };
  auto parse_bit_index = [&](std::string_view mem) {
    result.nodes.emplace_back(Type::IntList, Member{mem});
    auto emit_error = [&](std::string message, std::size_t consume) {
      warn_(std::move(message));
      result.nodes.emplace_back(Type::Int, Subscript{Integer(-1)});
      elmcode = elmcode.subspan(std::min(consume, elmcode.size()));
    };

    if (elmcode.size() < 3) {
      emit_error(std::format("[IntListBit] {} expected immediate index", mem),
                 elmcode.size());
      return;
    }

    auto* marker = std::get_if<Integer>(&elmcode[1]);
    if (!marker || marker->val_ != -1) {
      emit_error(std::format("[IntListBit] {} expected immediate index", mem),
                 1);
      return;
    }

    result.nodes.emplace_back(Type::Int, Subscript{elmcode[2]});
    elmcode = elmcode.subspan(3);
  };

  while (!elmcode.empty()) {
    const auto* token = std::get_if<Integer>(&elmcode.front());
    if (!token)
      break;

    const int key = token->val_;
    switch (result.GetType()) {
      case Type::DatabaseList:
        switch (key) {
          case -1:
            if (elmcode.size() < 2)
              throw std::runtime_error("missing database index");
            if (Typeof(elmcode[1]) != Type::Int)
              throw std::runtime_error("database index must be an integer");
            parse_index(Type::Database);
            continue;
          case 1:
            parse_database_call("size", Type::Int, {});
            continue;
          default:
            throw std::runtime_error("unsupported database list operation");
        }
        break;
      case Type::Database:
        switch (key) {
          case 0:
            parse_database_call("get_num", Type::Int, {Type::Int, Type::Int});
            continue;
          case 1:
            parse_database_call("get_str", Type::String,
                                {Type::Int, Type::Int});
            continue;
          case 3:
            parse_database_call("check_item", Type::Int, {Type::Int});
            continue;
          case 4:
            parse_database_call("check_column", Type::Int, {Type::Int});
            continue;
          case 5:
            parse_database_call("find_num", Type::Int, {Type::Int, Type::Int});
            continue;
          case 6:
            parse_database_call("find_str", Type::Int,
                                {Type::Int, Type::String});
            continue;
          case 7:
            parse_database_call("find_str_real", Type::Int,
                                {Type::Int, Type::String});
            continue;
          default:
            throw std::runtime_error("unsupported database operation");
        }
        break;
      case Type::IntList:
        switch (key) {
          case -1:
            parse_index(Type::Int);
            continue;
          case 3:
            parse_bit_index("b1");
            continue;
          case 4:
            parse_bit_index("b2");
            continue;
          case 5:
            parse_bit_index("b4");
            continue;
          case 7:
            parse_bit_index("b8");
            continue;
          case 6:
            parse_bit_index("b16");
            continue;
          case 10:
            append_callable("init");
            continue;
          case 2:
            append_callable("resize");
            continue;
          case 9:
            append_callable("size", Type::Int);
            continue;
          case 8:
            append_callable("fill");
            continue;
          case 1:
            append_callable("Set");
            continue;
          default:
            break;
        }
        break;
      case Type::IntEventList:
        switch (key) {
          case -1:
            parse_index(Type::IntEvent);
            continue;
          case 1:
            append_callable("resize");
            continue;
          default:
            break;
        }
        break;
      case Type::IntEvent:
        switch (key) {
          case 0:
            append_node(Type::Callable, Member("set"));
            continue;
          case 7:
            append_node(Type::Callable, Member("set_real"));
            continue;
          case 1:
            append_node(Type::Callable, Member("loop"));
            continue;
          case 8:
            append_node(Type::Callable, Member("loop_real"));
            continue;
          case 2:
            append_node(Type::Callable, Member("turn"));
            continue;
          case 9:
            append_node(Type::Callable, Member("turn_real"));
            continue;
          case 3:
            append_callable("end");
            continue;
          case 4:
            append_callable("wait", Type::None, AWAIT);
            continue;
          case 10:
            append_callable("wait_key", Type::None, AWAIT);
            continue;
          case 5:
            append_callable("check", Type::Int);
            continue;
          default:
            break;
        }
        break;
      case Type::StrList:
        switch (key) {
          case -1:
            parse_index(Type::String);
            continue;
          case 3:
            append_callable("init", Type::None);
            continue;
          case 2:
            append_node(Type::Callable, Member("resize"));
            continue;
          case 4:
            append_callable("size", Type::Int);
            continue;
          default:
            break;
        }
        break;
      case Type::String:
        switch (key) {
          case 0:
            append_callable("upper", Type::String);
            continue;
          case 1:
            append_callable("lower", Type::String);
            continue;
          case 6:
            append_callable("cnt", Type::Int);
            continue;
          case 5:
            append_callable("len", Type::Int);
            continue;
          case 2:
            append_callable("left", Type::String);
            continue;
          case 7:
            append_callable("left_len", Type::String);
            continue;
          case 4:
            append_callable("right", Type::String);
            continue;
          case 9:
            append_callable("right_len", Type::String);
            continue;
          case 3:
            append_callable("mid", Type::String);
            continue;
          case 8:
            append_callable("mid_len", Type::String);
            continue;
          case 10:
            append_callable("find", Type::Int);
            continue;
          case 11:
            append_callable("rfind", Type::Int);
            continue;
          case 13:
            append_callable("charat", Type::Int);
            continue;
          case 12:
            append_callable("tonum", Type::Int);
            continue;
          default:
            break;
        }
        break;
      case Type::Math:
        switch (key) {
          case 3:
            append_callable("max", Type::Int);
            continue;
          case 4:
            append_callable("min", Type::Int);
            continue;
          case 10:
            append_callable("limit", Type::Int);
            continue;
          case 5:
            append_callable("abs", Type::Int);
            continue;
          case 0:
            append_callable("rand", Type::Int);
            continue;
          case 14:
            append_callable("sqrt", Type::Int);
            continue;
          case 19:
            append_callable("log", Type::Int);
            continue;
          case 20:
            append_callable("log2", Type::Int);
            continue;
          case 21:
            append_callable("log10", Type::Int);
            continue;
          case 6:
            append_callable("sin", Type::Int);
            continue;
          case 7:
            append_callable("cos", Type::Int);
            continue;
          case 8:
            append_callable("tan", Type::Int);
            continue;
          case 16:
            append_callable("asin", Type::Int);
            continue;
          case 17:
            append_callable("acos", Type::Int);
            continue;
          case 18:
            append_callable("atan", Type::Int);
            continue;
          case 15:
            append_callable("distance", Type::Int);
            continue;
          case 22:
            append_callable("angle", Type::Int);
            continue;
          case 9:
            append_callable("linear", Type::Int);
            continue;
          case 2:
            append_callable("timetable", Type::Int);
            continue;
          case 1:
            append_callable("tostr", Type::String);
            continue;
          case 11:
            append_callable("tostr_zero", Type::String);
            continue;
          case 12:
            append_callable("tostr_zen", Type::String);
            continue;
          case 13:
            append_callable("tostr_zen_zero", Type::String);
            continue;
          case 23:
            append_callable("tostr_code", Type::String);
            continue;
          default:
            break;
        }
        break;
      case Type::System:
        switch (key) {
          case 14:
            append_callable("calendar", Type::None);
            continue;
          case 15:
            append_callable("time", Type::Int);
            continue;
          case 0:
            append_callable("window_active", Type::Int);
            continue;
          case 13:
            append_callable("is_debug", Type::Int);
            continue;
          case 1:
            append_callable("shell_openfile");
            continue;
          case 5:
            append_callable("openurl");
            continue;
          case 6:
            append_callable("check_file_exist");
            continue;
          case 12:
            append_callable("check_save_file_exist", Type::Int);
            continue;
          case 2:
            append_callable("check_dummy");
            continue;
          case 21:
            append_callable("clear_dummy", Type::None);
            continue;
          case 17:
            append_callable("msgbox_ok");
            continue;
          case 18:
            append_callable("msgbox_okcancel");
            continue;
          case 19:
            append_callable("msgbox_yn");
            continue;
          case 20:
            append_callable("msgbox_yncancel");
            continue;
          case 7:
            append_callable("debug_msgbox_ok");
            continue;
          case 8:
            append_callable("debug_msgbox_okcancel");
            continue;
          case 9:
            append_callable("debug_msgbox_yn");
            continue;
          case 10:
            append_callable("debug_msgbox_yncancel");
            continue;
          case 11:
            append_callable("debug_write_log");
            continue;
          case 4:
            append_callable("get_chihayabench", Type::String);
            continue;
          case 3:
            append_callable("open_chihayabench");
            continue;
          case 16:
            append_callable("get_lang", Type::String);
            continue;
          default:
            break;
        }
        break;
      case Type::Script:
        switch (key) {
          case 38:
            append_callable("set_auto_savepoint_off");
            continue;
          case 39:
            append_callable("set_auto_savepoint_on");
            continue;
          case 2:
            append_callable("set_skip_disable");
            continue;
          case 5:
            append_callable("set_skip_enable");
            continue;
          case 85:
            append_callable("set_skip_disable_flag");
            continue;
          case 83:
            append_callable("get_skip_disable_flag", Type::Int);
            continue;
          case 6:
            append_callable("set_ctrl_skip_disable");
            continue;
          case 7:
            append_callable("set_ctrl_skip_enable");
            continue;
          case 86:
            append_callable("set_ctrl_skip_disable_flag");
            continue;
          case 84:
            append_callable("get_ctrl_skip_disable_flag", Type::Int);
            continue;
          case 18:
            append_callable("check_skip");
            continue;
          case 30:
            append_callable("set_stop_skip_by_key_disable");
            continue;
          case 31:
            append_callable("set_stop_skip_by_key_enable");
            continue;
          case 54:
            append_callable("set_end_msg_by_key_disable");
            continue;
          case 55:
            append_callable("set_end_msg_by_key_enable");
            continue;
          case 74:
            append_callable("set_skip_unread_message_flag");
            continue;
          case 75:
            append_callable("get_skip_unread_message_flag", Type::Int);
            continue;
          case 0:
            append_callable("start_auto_mode");
            continue;
          case 1:
            append_callable("end_auto_mode");
            continue;
          case 60:
            append_callable("set_auto_mode_moji_wait");
            continue;
          case 62:
            append_callable("set_auto_mode_moji_wait_default");
            continue;
          case 64:
            append_callable("get_auto_mode_moji_wait", Type::Int);
            continue;
          case 61:
            append_callable("set_auto_mode_min_wait");
            continue;
          case 63:
            append_callable("set_auto_mode_min_wait_default");
            continue;
          case 65:
            append_callable("get_auto_mode_min_wait", Type::Int);
            continue;
          case 76:
            append_callable("set_auto_mode_moji_cnt");
            continue;
          case 77:
            append_callable("set_mouse_cursor_hide_onoff");
            continue;
          case 82:
            append_callable("set_mouse_cursor_hide_onoff_default");
            continue;
          case 78:
            append_callable("get_mouse_cursor_hide_onoff", Type::Int);
            continue;
          case 79:
            append_callable("set_mouse_cursor_hide_time");
            continue;
          case 80:
            append_callable("set_mouse_cursor_hide_time_default");
            continue;
          case 81:
            append_callable("get_mouse_cursor_hide_time", Type::Int);
            continue;
          case 25:
            append_callable("set_message_speed");
            continue;
          case 27:
            append_callable("set_message_speed_default");
            continue;
          case 26:
            append_callable("get_message_speed", Type::Int);
            continue;
          case 28:
            append_callable("set_message_nowait_flag");
            continue;
          case 29:
            append_callable("get_message_nowait_flag", Type::Int);
            continue;
          case 21:
            append_callable("set_msg_async_mode_on");
            continue;
          case 51:
            append_callable("set_msg_async_mode_on_once");
            continue;
          case 22:
            append_callable("set_msg_async_mode_off");
            continue;
          case 46:
            append_callable("set_hide_mwnd_disable");
            continue;
          case 47:
            append_callable("set_hide_mwnd_enable");
            continue;
          case 12:
            append_callable("set_msg_back_disable");
            continue;
          case 13:
            append_callable("set_msg_back_enable");
            continue;
          case 34:
            append_callable("set_msg_back_off");
            continue;
          case 35:
            append_callable("set_msg_back_on");
            continue;
          case 56:
            append_callable("set_msg_back_disp_off");
            continue;
          case 57:
            append_callable("set_msg_back_disp_on");
            continue;
          case 14:
            append_callable("set_mouse_disp_off");
            continue;
          case 15:
            append_callable("set_mouse_disp_on");
            continue;
          case 19:
            append_callable("set_mouse_move_by_key_disable");
            continue;
          case 20:
            append_callable("set_mouse_move_by_key_enable");
            continue;
          case 40:
            append_callable("set_key_disable");
            continue;
          case 41:
            append_callable("set_key_enable");
            continue;
          case 3:
            append_callable("set_mwnd_anime_off_flag");
            continue;
          case 9:
            append_callable("get_mwnd_anime_off_flag", Type::Int);
            continue;
          case 4:
            append_callable("set_mwnd_anime_on_flag");
            continue;
          case 16:
            append_callable("get_mwnd_anime_on_flag", Type::Int);
            continue;
          case 8:
            append_callable("set_mwnd_disp_off_flag");
            continue;
          case 17:
            append_callable("get_mwnd_disp_off_flag", Type::Int);
            continue;
          case 42:
            append_callable("set_koe_dont_stop_on_flag");
            continue;
          case 43:
            append_callable("get_koe_dont_stop_on_flag", Type::Int);
            continue;
          case 44:
            append_callable("set_koe_dont_stop_off_flag");
            continue;
          case 45:
            append_callable("get_koe_dont_stop_off_flag", Type::Int);
            continue;
          case 33:
            append_callable("set_shortcut_enable");
            continue;
          case 32:
            append_callable("set_shortcut_disable");
            continue;
          case 11:
            append_callable("set_quake_stop_flag");
            continue;
          case 23:
            append_callable("get_quake_stop_flag", Type::Int);
            continue;
          case 96:
            append_callable("set_emote_mouth_stop_flag");
            continue;
          case 97:
            append_callable("get_emote_mouth_stop_flag", Type::Int);
            continue;
          case 36:
            append_callable("start_bgmfade");
            continue;
          case 37:
            append_callable("end_bgmfade");
            continue;
          case 10:
            append_callable("set_vsync_wait_off_flag");
            continue;
          case 24:
            append_callable("get_vsync_wait_off_flag", Type::Int);
            continue;
          case 48:
            append_callable("set_skip_trigger");
            continue;
          case 49:
            append_callable("ignore_r_on");
            continue;
          case 50:
            append_callable("ignore_r_off");
            continue;
          case 52:
            append_callable("set_cursor_no");
            continue;
          case 53:
            append_callable("get_cursor_no", Type::Int);
            continue;
          case 66:
            append_callable("set_time_stop_flag");
            continue;
          case 67:
            append_callable("get_time_stop_flag", Type::Int);
            continue;
          case 68:
            append_callable("set_counter_time_stop_flag");
            continue;
          case 73:
            append_callable("get_counter_time_stop_flag", Type::Int);
            continue;
          case 69:
            append_callable("set_frame_action_time_stop_flag");
            continue;
          case 72:
            append_callable("get_frame_action_time_stop_flag", Type::Int);
            continue;
          case 70:
            append_callable("set_stage_time_stop_flag");
            continue;
          case 71:
            append_callable("get_stage_time_stop_flag", Type::Int);
            continue;
          default:
            break;
        }
        break;
      case Type::ScriptExcall:
        switch (key) {
          case 87:
            append_callable("set_font_name");
            continue;
          case 89:
            append_callable("set_font_name_default");
            continue;
          case 88:
            append_callable("get_font_name", Type::String);
            continue;
          case 90:
            append_callable("set_font_bold");
            continue;
          case 91:
            append_callable("set_font_bold_default");
            continue;
          case 92:
            append_callable("get_font_bold", Type::Int);
            continue;
          case 93:
            append_callable("set_font_shadow");
            continue;
          case 94:
            append_callable("set_font_shadow_default");
            continue;
          case 95:
            append_callable("get_font_shadow", Type::Int);
            continue;
          default:
            break;
        }
        break;
      case Type::FrameActionList:
        switch (key) {
          case -1:
            parse_index(Type::FrameAction);
            continue;
          case 2:
            append_callable("size", Type::Int);
            continue;
          case 1:
            append_callable("resize");
            continue;
          default:
            break;
        }
        break;
      case Type::FrameAction:
        switch (key) {
          case 1:
            append_callable("start", Type::None, AWAIT);
            continue;
          case 3:
            append_callable("start_real", Type::None, AWAIT);
            continue;
          case 2:
            append_callable("end", Type::None, AWAIT);
            continue;
          case 0:
            parse_property("counter", Type::Counter);
            continue;
          case 4:
            append_callable("is_end_action", Type::Int);
            continue;
          default:
            break;
        }
        break;
      case Type::CounterList:
        switch (key) {
          case -1:
            parse_index(Type::Counter);
            continue;
          case 1:
            append_callable("size", Type::Int);
            continue;
          default:
            break;
        }
        break;
      case Type::Counter:
        switch (key) {
          case 0:
            append_callable("set");
            continue;
          case 1:
            append_callable("get", Type::Int);
            continue;
          case 2:
            append_callable("reset");
            continue;
          case 3:
            append_callable("start");
            continue;
          case 9:
            append_callable("start_real");
            continue;
          case 10:
            append_callable("start_frame");
            continue;
          case 11:
            append_callable("start_frame_real");
            continue;
          case 12:
            append_callable("start_frame_loop");
            continue;
          case 13:
            append_callable("start_frame_loop_real");
            continue;
          case 4:
            append_callable("stop");
            continue;
          case 5:
            append_callable("resume");
            continue;
          case 6:
            append_callable("wait", Type::None, AWAIT);
            continue;
          case 8:
            append_callable("wait_key", Type::Int, AWAIT);
            continue;
          case 7:
            append_callable("check_value", Type::Int);
            continue;
          case 14:
            append_callable("check_active", Type::Int);
            continue;
          default:
            break;
        }
        break;
      case Type::Syscom:
        switch (key) {
          case 0:
            append_callable("menu");
            continue;
          case 6:
            append_node(Type::Callable, Member("menu_enable"));
            continue;
          case 7:
            append_node(Type::Callable, Member("menu_disable"));
            continue;
          case 11: {
            switch (elm.bind_ctx.overload_id) {
              case 0:
                append_callable("btn_enable_all");
                break;
              case 1:
                append_callable("btn_enable");
                break;
              default:
                warn_(std::format(
                    "[Callable] overload {} not found while parsing {}",
                    elm.bind_ctx.overload_id, result.ToDebugString()));
                append_callable("btn_enable_all");
                break;
            }
            continue;
          }
          case 12: {
            switch (elm.bind_ctx.overload_id) {
              case 0:
                append_callable("btn_disable_all");
                break;
              case 1:
                append_callable("btn_disable");
                break;
              default:
                warn_(std::format(
                    "[Callable] overload {} not found while parsing {}",
                    elm.bind_ctx.overload_id, result.ToDebugString()));
                append_callable("btn_disable_all");
                break;
            }
            continue;
          }
          case 133:
            append_node(Type::None, Member("touch_enable"));
            continue;
          case 134:
            append_node(Type::None, Member("touch_disable"));
            continue;
          case 5:
            append_callable("init_flags", Type::None);
            continue;
          case 200:
            append_node(Type::Callable, Member("set_readskip"));
            continue;
          case 201:
            append_callable("get_readskip", Type::Int);
            continue;
          case 202:
            append_node(Type::Callable, Member("set_enable_readskip"));
            continue;
          case 203:
            append_callable("get_enable_readskip", Type::Int);
            continue;
          case 204:
            append_node(Type::Callable, Member("set_exist_readskip"));
            continue;
          case 205:
            append_callable("get_exist_readskip", Type::Int);
            continue;
          case 206:
            append_callable("is_readskip_enable", Type::Int);
            continue;
          case 207:
            append_callable("set_autoskip");
            continue;
          case 208:
            append_callable("get_autoskip", Type::Int);
            continue;
          case 209:
            append_callable("set_enable_autoskip");
            continue;
          case 210:
            append_callable("get_enable_autoskip", Type::Int);
            continue;
          case 211:
            append_callable("set_exist_autoskip");
            continue;
          case 212:
            append_callable("get_exist_autoskip", Type::Int);
            continue;
          case 213:
            append_callable("is_autoskip_enable", Type::Int);
            continue;
          case 214:
            append_node(Type::Callable, Member("set_automode"));
            continue;
          case 215:
            append_callable("get_automode", Type::Int);
            continue;
          case 216:
            append_node(Type::Callable, Member("set_enable_automode"));
            continue;
          case 217:
            append_callable("get_enable_automode", Type::Int);
            continue;
          case 218:
            append_node(Type::Callable, Member("set_exist_automode"));
            continue;
          case 219:
            append_callable("get_exist_automode", Type::Int);
            continue;
          case 220:
            append_callable("is_automode_enable", Type::Int);
            continue;
          case 221:
            append_node(Type::Callable, Member("set_hidemwnd"));
            continue;
          case 222:
            append_callable("get_hidemwnd", Type::Int);
            continue;
          case 223:
            append_node(Type::Callable, Member("set_enable_hidemwnd"));
            continue;
          case 224:
            append_callable("get_enable_hidemwnd", Type::Int);
            continue;
          case 225:
            append_callable("set_exist_hidemwnd");
            continue;
          case 226:
            append_callable("get_exist_hidemwnd", Type::Int);
            continue;
          case 227:
            append_callable("is_hidemwnd_enable", Type::Int);
            continue;
          case 300:
            append_node(Type::Callable, Member("set_extraswitch"));
            continue;
          case 301:
            append_node(Type::Callable, Member("get_extraswitch"));
            continue;
          case 302:
            append_node(Type::Callable, Member("set_enable_extraswitch"));
            continue;
          case 303:
            append_node(Type::Callable, Member("get_enable_extraswitch"));
            continue;
          case 304:
            append_node(Type::Callable, Member("set_exist_extraswitch"));
            continue;
          case 305:
            append_node(Type::Callable, Member("get_exist_extraswitch"));
            continue;
          case 306:
            append_node(Type::Callable, Member("is_extraswitch_enable"));
            continue;
          case 23:
            append_node(Type::Callable, Member("set_localmode"));
            continue;
          case 57:
            append_callable("get_localmode", Type::Int);
            continue;
          case 58:
            append_node(Type::Callable, Member("set_enable_localmode"));
            continue;
          case 59:
            append_callable("get_enable_localmode", Type::Int);
            continue;
          case 62:
            append_node(Type::Callable, Member("set_exist_localmode"));
            continue;
          case 63:
            append_callable("get_exist_localmode", Type::Int);
            continue;
          case 64:
            append_callable("is_localmode_enable", Type::Int);
            continue;
          case 192:
            append_callable("open_msgback", Type::None, AWAIT);
            continue;
          case 193:
            append_callable("close_msgback");
            continue;
          case 194:
            append_callable("set_enable_msgback");
            continue;
          case 195:
            append_callable("get_enable_msgback", Type::Int);
            continue;
          case 196:
            append_callable("set_exist_msgback");
            continue;
          case 197:
            append_callable("get_exist_msgback", Type::Int);
            continue;
          case 198:
            append_callable("is_msgback_enable", Type::Int);
            continue;
          case 329:
            append_callable("is_msgback_open", Type::Int);
            continue;
          case 228:
            append_callable("return_to_sel", Type::None);
            continue;
          case 230:
            append_node(Type::Callable, Member("set_enable_retsel"));
            continue;
          case 231:
            append_callable("get_enable_retsel", Type::Int);
            continue;
          case 232:
            append_node(Type::Callable, Member("set_exist_retsel"));
            continue;
          case 233:
            append_callable("get_exist_retsel", Type::Int);
            continue;
          case 234:
            append_callable("is_retsel_enable", Type::Int);
            continue;
          case 235:
            append_node(Type::Callable, Member("return_to_menu"));
            continue;
          case 237:
            append_node(Type::Callable, Member("set_enable_retmenu"));
            continue;
          case 238:
            append_callable("get_enable_retmenu", Type::Int);
            continue;
          case 239:
            append_callable("set_exist_retmenu");
            continue;
          case 240:
            append_callable("get_exist_retmenu", Type::Int);
            continue;
          case 241:
            append_callable("is_retmenu_enable", Type::Int);
            continue;
          case 242:
            append_callable("end_game");
            continue;
          case 244:
            append_node(Type::Callable, Member("set_enable_endgame"));
            continue;
          case 245:
            append_callable("get_enable_endgame", Type::Int);
            continue;
          case 246:
            append_node(Type::Callable, Member("set_exist_endgame"));
            continue;
          case 247:
            append_callable("get_exist_endgame", Type::Int);
            continue;
          case 248:
            append_callable("is_endgame_enable", Type::Int);
            continue;
          case 288:
            append_node(Type::None, Member("replay_koe"));
            continue;
          case 292:
            append_callable("check_koe", Type::Int);
            continue;
          case 289:
            append_callable("get_cur_koe", Type::Int);
            continue;
          case 291:
            append_callable("get_cur_chr", Type::Int);
            continue;
          case 293:
            append_node(Type::None, Member("clear_koe_chr"));
            continue;
          case 294:
            append_callable("get_current_save_title", Type::String);
            continue;
          case 295:
            append_callable("get_current_save_message", Type::String);
            continue;
          case 199:
            append_callable("get_total_playtime", Type::None);
            continue;
          case 229:
            append_callable("set_total_playtime", Type::None);
            continue;
          case 1:
            append_callable("open_save");
            continue;
          case 251:
            append_node(Type::Callable, Member("set_enable_save"));
            continue;
          case 252:
            append_callable("get_enable_save", Type::Int);
            continue;
          case 253:
            append_callable("set_exist_save");
            continue;
          case 254:
            append_callable("get_exist_save", Type::Int);
            continue;
          case 255:
            append_callable("is_save_enable", Type::Int);
            continue;
          case 2:
            append_callable("open_load");
            continue;
          case 258:
            append_node(Type::Callable, Member("set_enable_load"));
            continue;
          case 259:
            append_callable("get_enable_load", Type::Int);
            continue;
          case 260:
            append_node(Type::Callable, Member("set_exist_load"));
            continue;
          case 261:
            append_callable("get_exist_load", Type::Int);
            continue;
          case 262:
            append_callable("is_load_enable", Type::Int);
            continue;
          case 249:
            append_callable("save", Type::Int);
            continue;
          case 256:
            append_callable("load");
            continue;
          case 18:
            append_callable("qsave", Type::Int);
            continue;
          case 20:
            append_callable("qload");
            continue;
          case 271:
            append_callable("endsave", Type::Int);
            continue;
          case 269:
            append_callable("endload", Type::None);
            continue;
          case 272:
            append_callable("inner_save", Type::Int);
            continue;
          case 273:
            append_callable("inner_load", Type::Int);
            continue;
          case 276:
            append_callable("clear_inner_save", Type::Int);
            continue;
          case 274:
            append_callable("copy_inner_save", Type::Int);
            continue;
          case 275:
            append_callable("check_inner_save", Type::Int);
            continue;
          case 310:
            append_callable("msgbk_load", Type::None);
            continue;
          case 68:
            append_callable("get_save_count", Type::Int);
            continue;
          case 168:
            append_callable("get_qsave_count", Type::Int);
            continue;
          case 79:
            append_callable("get_new_save_no", Type::Int);
            continue;
          case 170:
            append_callable("get_new_qsave_no", Type::Int);
            continue;
          case 69:
            append_callable("is_save_exist", Type::Int);
            continue;
          case 70:
            append_callable("get_save_year", Type::Int);
            continue;
          case 71:
            append_callable("get_save_month", Type::Int);
            continue;
          case 72:
            append_callable("get_save_day", Type::Int);
            continue;
          case 73:
            append_callable("get_save_weekday", Type::Int);
            continue;
          case 74:
            append_callable("get_save_hour", Type::Int);
            continue;
          case 75:
            append_callable("get_save_minute", Type::Int);
            continue;
          case 76:
            append_callable("get_save_second", Type::Int);
            continue;
          case 77:
            append_callable("get_save_millisecond", Type::Int);
            continue;
          case 78:
            append_callable("get_save_title", Type::String);
            continue;
          case 129:
            append_callable("get_save_message", Type::String);
            continue;
          case 324:
            append_callable("get_save_full_message", Type::String);
            continue;
          case 131:
            append_callable("get_save_comment", Type::String);
            continue;
          case 180:
            append_callable("set_save_comment", Type::None);
            continue;
          case 183:
            append_callable("get_save_value");
            continue;
          case 182:
            append_callable("set_save_value");
            continue;
          case 320:
            append_callable("get_save_append_dir", Type::String);
            continue;
          case 321:
            append_callable("get_save_append_name", Type::String);
            continue;
          case 169:
            append_callable("is_qsave_exist", Type::Int);
            continue;
          case 171:
            append_callable("get_qsave_year", Type::Int);
            continue;
          case 172:
            append_callable("get_qsave_month", Type::Int);
            continue;
          case 173:
            append_callable("get_qsave_day", Type::Int);
            continue;
          case 174:
            append_callable("get_qsave_weekday", Type::Int);
            continue;
          case 175:
            append_callable("get_qsave_hour", Type::Int);
            continue;
          case 176:
            append_callable("get_qsave_minute", Type::Int);
            continue;
          case 177:
            append_callable("get_qsave_second", Type::Int);
            continue;
          case 178:
            append_callable("get_qsave_millisecond", Type::Int);
            continue;
          case 179:
            append_callable("get_qsave_title", Type::String);
            continue;
          case 130:
            append_callable("get_qsave_message", Type::String);
            continue;
          case 325:
            append_callable("get_qsave_full_message", Type::String);
            continue;
          case 132:
            append_callable("get_qsave_comment", Type::String);
            continue;
          case 181:
            append_callable("set_qsave_comment", Type::None);
            continue;
          case 184:
            append_callable("get_qsave_value", Type::None);
            continue;
          case 185:
            append_callable("set_qsave_value");
            continue;
          case 322:
            append_callable("get_qsave_append_dir", Type::String);
            continue;
          case 323:
            append_callable("get_qsave_append_name", Type::String);
            continue;
          case 270:
            append_callable("is_endsave_exist", Type::Int);
            continue;
          case 67:
            append_callable("copy_save", Type::Int);
            continue;
          case 22:
            append_callable("change_save", Type::Int);
            continue;
          case 19:
            append_callable("delete_save", Type::Int);
            continue;
          case 128:
            append_callable("copy_qsave", Type::Int);
            continue;
          case 66:
            append_callable("change_qsave", Type::Int);
            continue;
          case 65:
            append_callable("delete_qsave", Type::Int);
            continue;
          case 3:
            append_callable("open_config_menu");
            continue;
          case 138:
            append_callable("open_config_windowmode_menu");
            continue;
          case 139:
            append_node(Type::None, Member("open_config_volume_menu"));
            continue;
          case 137:
            append_node(Type::None, Member("open_config_bgm_fade_menu"));
            continue;
          case 147:
            append_node(Type::None, Member("open_config_koemode_menu"));
            continue;
          case 146:
            append_node(Type::None, Member("open_config_charakoe_menu"));
            continue;
          case 151:
            append_node(Type::None, Member("open_config_jitan_menu"));
            continue;
          case 135:
            append_node(Type::None, Member("open_config_message_speed_menu"));
            continue;
          case 136:
            append_callable("open_config_filter_color_menu");
            continue;
          case 140:
            append_node(Type::None, Member("open_config_auto_mode_menu"));
            continue;
          case 142:
            append_callable("open_config_font_menu");
            continue;
          case 141:
            append_callable("open_config_system_menu");
            continue;
          case 167:
            append_callable("open_config_movie_menu");
            continue;
          case 4:
            append_node(Type::Callable, Member("set_window_mode"));
            continue;
          case 99:
            append_node(Type::None, Member("set_window_mode_default"));
            continue;
          case 9:
            append_callable("get_window_mode", Type::Int);
            continue;
          case 13:
            append_node(Type::Callable, Member("set_window_mode_size"));
            continue;
          case 100:
            append_node(Type::None, Member("set_window_mode_default"));
            continue;
          case 16:
            append_callable("get_window_mode_size", Type::Int);
            continue;
          case 309:
            append_node(Type::Callable,
                        Member("check_window_mode_size_enable"));
            continue;
          case 39:
            append_node(Type::Callable, Member("set_all_volume"));
            continue;
          case 21:
            append_node(Type::Callable, Member("set_bgm_volume"));
            continue;
          case 26:
            append_node(Type::Callable, Member("set_koe_volume"));
            continue;
          case 29:
            append_node(Type::Callable, Member("set_pcm_volume"));
            continue;
          case 32:
            append_node(Type::Callable, Member("set_se_volume"));
            continue;
          case 263:
            append_node(Type::Callable, Member("set_mov_volume"));
            continue;
          case 277:
            append_node(Type::Callable, Member("set_sound_volume"));
            continue;
          case 60:
            append_node(Type::Callable, Member("set_all_onoff"));
            continue;
          case 35:
            append_node(Type::Callable, Member("set_bgm_onoff"));
            continue;
          case 36:
            append_node(Type::Callable, Member("set_koe_onoff"));
            continue;
          case 37:
            append_node(Type::Callable, Member("set_pcm_onoff"));
            continue;
          case 38:
            append_node(Type::Callable, Member("set_se_onoff"));
            continue;
          case 266:
            append_node(Type::Callable, Member("set_mov_onoff"));
            continue;
          case 280:
            append_node(Type::Callable, Member("set_sound_onoff"));
            continue;
          case 40:
            append_callable("set_all_volume_default");
            continue;
          case 24:
            append_callable("set_bgm_volume_default");
            continue;
          case 27:
            append_callable("set_koe_volume_default");
            continue;
          case 30:
            append_callable("set_pcm_volume_default");
            continue;
          case 33:
            append_callable("set_se_volume_default");
            continue;
          case 264:
            append_callable("set_mov_volume_default");
            continue;
          case 278:
            append_node(Type::Callable, Member("set_sound_volume_default"));
            continue;
          case 101:
            append_callable("set_all_onoff_default");
            continue;
          case 102:
            append_callable("set_bgm_onoff_default");
            continue;
          case 103:
            append_callable("set_koe_onoff_default");
            continue;
          case 104:
            append_callable("set_pcm_onoff_default");
            continue;
          case 105:
            append_callable("set_se_onoff_default");
            continue;
          case 267:
            append_callable("set_mov_onoff_default");
            continue;
          case 281:
            append_node(Type::Callable, Member("set_sound_onoff_default"));
            continue;
          case 41:
            append_callable("get_all_volume", Type::Int);
            continue;
          case 25:
            append_callable("get_bgm_volume", Type::Int);
            continue;
          case 28:
            append_callable("get_koe_volume", Type::Int);
            continue;
          case 31:
            append_callable("get_pcm_volume", Type::Int);
            continue;
          case 34:
            append_callable("get_se_volume", Type::Int);
            continue;
          case 265:
            append_callable("get_mov_volume", Type::Int);
            continue;
          case 279:
            append_node(Type::Callable, Member("get_sound_volume"));
            continue;
          case 61:
            append_callable("get_all_onoff", Type::Int);
            continue;
          case 42:
            append_callable("get_bgm_onoff", Type::Int);
            continue;
          case 43:
            append_callable("get_koe_onoff", Type::Int);
            continue;
          case 44:
            append_callable("get_pcm_onoff", Type::Int);
            continue;
          case 45:
            append_callable("get_se_onoff", Type::Int);
            continue;
          case 268:
            append_callable("get_mov_onoff", Type::Int);
            continue;
          case 282:
            append_node(Type::Callable, Member("get_sound_onoff"));
            continue;
          case 94:
            append_node(Type::Callable, Member("set_bgmfade_volume"));
            continue;
          case 97:
            append_node(Type::Callable, Member("set_bgmfade_onoff"));
            continue;
          case 95:
            append_callable("set_bgmfade_volume_default");
            continue;
          case 106:
            append_callable("set_bgmfade_onoff_default");
            continue;
          case 96:
            append_callable("get_bgmfade_volume", Type::Int);
            continue;
          case 98:
            append_callable("get_bgmfade_onoff", Type::Int);
            continue;
          case 148:
            append_node(Type::Callable, Member("set_koemode"));
            continue;
          case 149:
            append_node(Type::None, Member("set_koemode_default"));
            continue;
          case 150:
            append_callable("get_koemode", Type::Int);
            continue;
          case 143:
            append_node(Type::Callable, Member("set_charakoe_onoff"));
            continue;
          case 144:
            append_node(Type::Callable, Member("set_charakoe_onoff_default"));
            continue;
          case 145:
            append_node(Type::Callable, Member("get_charakoe_onoff"));
            continue;
          case 186:
            append_node(Type::Callable, Member("set_charakoe_volume"));
            continue;
          case 187:
            append_node(Type::Callable, Member("set_charakoe_volume_default"));
            continue;
          case 188:
            append_node(Type::Callable, Member("get_charakoe_volume"));
            continue;
          case 153:
            append_node(Type::Callable, Member("set_jitan_normal_onoff"));
            continue;
          case 154:
            append_node(Type::None, Member("set_jitan_normal_onoff_default"));
            continue;
          case 155:
            append_callable("get_jitan_normal_onoff", Type::Int);
            continue;
          case 156:
            append_node(Type::Callable, Member("set_jitan_auto_mode_onoff"));
            continue;
          case 157:
            append_node(Type::None,
                        Member("set_jitan_auto_mode_onoff_default"));
            continue;
          case 158:
            append_callable("get_jitan_auto_mode_onoff", Type::Int);
            continue;
          case 159:
            append_node(Type::Callable, Member("set_jitan_koe_replay_onoff"));
            continue;
          case 160:
            append_node(Type::None,
                        Member("set_jitan_koe_replay_onoff_default"));
            continue;
          case 161:
            append_callable("get_jitan_koe_replay_onoff", Type::Int);
            continue;
          case 152:
            append_node(Type::Callable, Member("set_jitan_speed"));
            continue;
          case 162:
            append_node(Type::None, Member("set_jitan_speed_default"));
            continue;
          case 163:
            append_callable("get_jitan_speed", Type::Int);
            continue;
          case 46:
            append_node(Type::Callable, Member("set_message_speed"));
            continue;
          case 47:
            append_callable("set_message_speed_default");
            continue;
          case 48:
            append_callable("get_message_speed", Type::Int);
            continue;
          case 49:
            append_node(Type::Callable, Member("set_message_nowait"));
            continue;
          case 107:
            append_callable("set_message_nowait_default");
            continue;
          case 50:
            append_callable("get_message_nowait", Type::Int);
            continue;
          case 51:
            append_node(Type::Callable, Member("set_auto_mode_moji_wait"));
            continue;
          case 52:
            append_callable("set_auto_mode_moji_wait_default");
            continue;
          case 53:
            append_callable("get_auto_mode_moji_wait", Type::Int);
            continue;
          case 54:
            append_node(Type::Callable, Member("set_auto_mode_min_wait"));
            continue;
          case 55:
            append_callable("set_auto_mode_min_wait_default");
            continue;
          case 56:
            append_callable("get_auto_mode_min_wait", Type::Int);
            continue;
          case 311:
            append_node(Type::Callable, Member("set_mouse_cursor_hide_onoff"));
            continue;
          case 312:
            append_node(Type::None,
                        Member("set_mouse_cursor_hide_onoff_default"));
            continue;
          case 313:
            append_callable("get_mouse_cursor_hide_onoff", Type::Int);
            continue;
          case 317:
            append_node(Type::Callable, Member("set_mouse_cursor_hide_time"));
            continue;
          case 318:
            append_node(Type::None,
                        Member("set_mouse_cursor_hide_time_default"));
            continue;
          case 319:
            append_callable("get_mouse_cursor_hide_time", Type::Int);
            continue;
          case 82:
            append_node(Type::Callable, Member("set_filter_color_r"));
            continue;
          case 85:
            append_node(Type::Callable, Member("set_filter_color_g"));
            continue;
          case 86:
            append_node(Type::Callable, Member("set_filter_color_b"));
            continue;
          case 87:
            append_node(Type::Callable, Member("set_filter_color_a"));
            continue;
          case 83:
            append_node(Type::None, Member("set_filter_color_r_default"));
            continue;
          case 88:
            append_node(Type::None, Member("set_filter_color_g_default"));
            continue;
          case 89:
            append_node(Type::None, Member("set_filter_color_b_default"));
            continue;
          case 90:
            append_callable("set_filter_color_a_default");
            continue;
          case 84:
            append_callable("get_filter_color_r", Type::Int);
            continue;
          case 91:
            append_callable("get_filter_color_g", Type::Int);
            continue;
          case 92:
            append_callable("get_filter_color_b", Type::Int);
            continue;
          case 93:
            append_callable("get_filter_color_a", Type::Int);
            continue;
          case 189:
            append_node(Type::Callable, Member("set_obj_disp_onoff"));
            continue;
          case 190:
            append_node(Type::Callable, Member("set_obj_disp_onoff_default"));
            continue;
          case 191:
            append_node(Type::Callable, Member("get_obj_disp_onoff"));
            continue;
          case 14:
            append_node(Type::Callable, Member("set_global_extraswitch_onoff"));
            continue;
          case 15:
            append_node(Type::Callable,
                        Member("set_global_extraswitch_onoff_default"));
            continue;
          case 17:
            append_node(Type::Callable, Member("get_global_extraswitch_onoff"));
            continue;
          case 164:
            append_node(Type::Callable, Member("set_global_extramode"));
            continue;
          case 165:
            append_node(Type::Callable, Member("set_global_extramode_default"));
            continue;
          case 166:
            append_node(Type::Callable, Member("get_global_extramode_default"));
            continue;
          case 80:
            append_callable("set_saveload_alert_onoff");
            continue;
          case 108:
            append_callable("set_saveload_alert_onoff_default", Type::None);
            continue;
          case 10:
            append_callable("get_saveload_alert_onoff", Type::Int);
            continue;
          case 110:
            append_node(Type::Callable, Member("set_sleep_onoff"));
            continue;
          case 111:
            append_node(Type::None, Member("set_sleep_onoff_default"));
            continue;
          case 112:
            append_callable("get_sleep_onoff", Type::Int);
            continue;
          case 113:
            append_node(Type::Callable, Member("set_no_wipe_anime_onoff"));
            continue;
          case 114:
            append_node(Type::None, Member("set_no_wipe_anime_onoff_default"));
            continue;
          case 115:
            append_node(Type::Callable, Member("get_no_wipe_anime_onoff"));
            continue;
          case 116:
            append_node(Type::Callable, Member("set_skip_wipe_anime_onoff"));
            continue;
          case 117:
            append_node(Type::None,
                        Member("set_skip_wipe_anime_onoff_default"));
            continue;
          case 118:
            append_callable("get_skip_wipe_anime_onoff", Type::Int);
            continue;
          case 8:
            append_callable("set_no_mwnd_anime_onoff");
            continue;
          case 109:
            append_callable("set_no_mwnd_anime_onoff_default");
            continue;
          case 81:
            append_callable("get_no_mwnd_anime_onoff", Type::Int);
            continue;
          case 119:
            append_node(Type::Callable, Member("set_wheel_next_message_onoff"));
            continue;
          case 120:
            append_node(Type::None,
                        Member("set_wheel_next_message_onoff_default"));
            continue;
          case 121:
            append_callable("get_wheel_next_message_onoff", Type::Int);
            continue;
          case 122:
            append_node(Type::Callable, Member("set_koe_dont_stop_onoff"));
            continue;
          case 123:
            append_node(Type::None, Member("set_koe_dont_stop_onoff_default"));
            continue;
          case 124:
            append_callable("get_koe_dont_stop_onoff", Type::Int);
            continue;
          case 125:
            append_node(Type::Callable,
                        Member("set_skip_unread_message_onoff"));
            continue;
          case 126:
            append_node(Type::None,
                        Member("set_skip_unread_message_onoff_default"));
            continue;
          case 127:
            append_callable("get_skip_unread_message_onoff", Type::Int);
            continue;
          case 250:
            append_node(Type::Callable, Member("set_play_silent_sound_onoff"));
            continue;
          case 243:
            append_callable("get_play_silent_sound_onoff", Type::Int);
            continue;
          case 283:
            append_node(Type::Callable, Member("set_font_name"));
            continue;
          case 326:
            append_node(Type::None, Member("set_font_name_default"));
            continue;
          case 284:
            append_callable("get_font_name", Type::String);
            continue;
          case 285:
            append_node(Type::Callable, Member("is_font_exist"));
            continue;
          case 296:
            append_node(Type::Callable, Member("set_font_bold"));
            continue;
          case 298:
            append_node(Type::None, Member("set_font_bold_default"));
            continue;
          case 307:
            append_callable("get_font_bold", Type::Int);
            continue;
          case 297:
            append_node(Type::Callable, Member("set_font_decoration"));
            continue;
          case 299:
            append_node(Type::None, Member("set_font_decoration_default"));
            continue;
          case 308:
            append_callable("get_font_decoration", Type::Int);
            continue;
          case 286:
            append_callable("create_capture_buffer");
            continue;
          case 287:
            append_callable("destroy_capture_buffer");
            continue;
          case 290:
            append_callable("capture_to_capture_buffer", Type::None, AWAIT);
            continue;
          case 314:
            append_callable("save_capture_buffer_to_file", Type::Int,
                            NONSIMPLE | AWAIT);
            continue;
          case 315:
            append_callable("load_flag_from_capture_file", Type::Int,
                            NONSIMPLE | AWAIT);
            continue;
          case 316:
            append_callable("capture_to_png", Type::None, AWAIT);
            continue;
          case 327:
            append_callable("twitter");
            continue;
          case 328:
            append_node(Type::Callable, Member("set_ret_scene_once"));
            continue;
          case 330:
            append_node(Type::Callable, Member("get_sys_extra_int"));
            continue;
          case 331:
            append_node(Type::Callable, Member("get_sys_extra_str"));
            continue;
          default:
            break;
        }
        break;
      case Type::Excall:
        switch (key) {
          case -1:
            parse_index(Type::Excall);
            continue;
          case 4:
            append_callable("alloc", Type::None);
            continue;
          case 5:
            append_callable("free", Type::None, AWAIT);
            continue;
          case 12:
            append_callable("is_excall", Type::Int);
            continue;
          case 8:
            append_callable("check_alloc", Type::Int);
            continue;
          case 7:
            append_node(Type::IntList, Member("F"));
            continue;
          case 6:
            append_node(Type::CounterList, Member("counter"));
            continue;
          case 9:
            append_node(Type::FrameAction, Member("frame_action"));
            continue;
          case 10:
            append_node(Type::FrameActionList, Member("frame_action_ch"));
            continue;
          case 0:
            append_node(Type::StageList, Member("stage"));
            continue;
          case 2:
            append_node(Type::Stage, Member("back"));
            continue;
          case 1:
            append_node(Type::Stage, Member("front"));
            continue;
          case 3:
            append_node(Type::Stage, Member("next"));
            continue;
          case 13:
            append_node(Type::ScriptExcall, Member("script"));
            continue;
          default:
            break;
        }
        break;
      case Type::StageList:
        switch (key) {
          case -1:
            parse_index(Type::Stage);
            continue;
          default:
            break;
        }
        break;
      case Type::Stage:
        switch (key) {
          case 2:
            append_node(Type::ObjList, Member("object"));
            continue;
          case 3:
            append_node(Type::MwndList, Member("mwnd"));
            continue;
          case 6:
            append_node(Type::GroupList, Member("objgroup"));
            continue;
          case 5:
            append_node(Type::BtnselItemList, Member("btnsel"));
            continue;
          case 8:
            append_node(Type::WorldList, Member("world"));
            continue;
          case 4:
            append_node(Type::EffectList, Member("effect"));
            continue;
          case 7:
            append_node(Type::QuakeList, Member("quake"));
            continue;
          case 0:
            append_node(Type::Callable, Member("create_obj"));
            continue;
          case 1:
            append_node(Type::Callable, Member("create_mwnd"));
            continue;
          default:
            break;
        }
        break;
      case Type::GroupList:
        switch (key) {
          case -1:
            parse_index(Type::Group);
            continue;
          case 1:
            append_callable("alloc", Type::None);
            continue;
          case 2:
            append_callable("free", Type::None);
            continue;
          default:
            break;
        }
        break;
      case Type::Group:
        switch (key) {
          case 0:
            append_callable("sel", Type::Int, AWAIT);
            continue;
          case 1:
            append_callable("sel_cancel", Type::Int, AWAIT);
            continue;
          case 4:
            append_callable("init", Type::None);
            continue;
          case 2:
            append_callable("start", Type::None);
            continue;
          case 10:
            append_callable("start_cancel", Type::None);
            continue;
          case 5:
            append_callable("end", Type::None);
            continue;
          case 6:
            append_callable("get_hit_no", Type::Int);
            continue;
          case 7:
            append_callable("get_pushed_no", Type::Int);
            continue;
          case 3:
            append_callable("get_decided_no", Type::Int);
            continue;
          case 15:
            append_callable("get_result", Type::Int);
            continue;
          case 16:
            append_callable("get_result_button_no", Type::Int);
            continue;
          case 8:
            parse_property("order", Type::Int, true);
            continue;
          case 13:
            parse_property("layer", Type::Int, true);
            continue;
          case 14:
            parse_property("cancel_priority", Type::Int, true);
            continue;
          default:
            break;
        }
        break;
      case Type::Screen:
        switch (key) {
          case 31:
            append_node(Type::EffectList, Member("effect"));
            continue;
          case 25:
            append_node(Type::QuakeList, Member("quake"));
            continue;
          default:
            break;
        }
        break;
      case Type::EffectList:
        switch (key) {
          case -1:
            parse_index(Type::Effect);
            continue;
          case 1:
            append_callable("resize");
            continue;
          case 2:
            append_callable("size", Type::Int);
            continue;
          default:
            break;
        }
        break;
      case Type::Effect:
        switch (key) {
          case 30:
            append_callable("init");
            continue;
          case 31:
            parse_property("wipe_copy", Type::Int, true);
            continue;
          case 32:
            parse_property("wipe_erase", Type::Int, true);
            continue;
          case 0:
            parse_property("x", Type::Int, true);
            continue;
          case 1:
            parse_property("y", Type::Int, true);
            continue;
          case 2:
            parse_property("z", Type::Int, true);
            continue;
          case 3:
            parse_property("mono", Type::Int, true);
            continue;
          case 4:
            parse_property("reverse", Type::Int, true);
            continue;
          case 5:
            parse_property("bright", Type::Int, true);
            continue;
          case 6:
            parse_property("dark", Type::Int, true);
            continue;
          case 7:
            parse_property("color_r", Type::Int, true);
            continue;
          case 8:
            parse_property("color_g", Type::Int, true);
            continue;
          case 9:
            parse_property("color_b", Type::Int, true);
            continue;
          case 10:
            parse_property("color_rate", Type::Int, true);
            continue;
          case 11:
            parse_property("color_add_r", Type::Int, true);
            continue;
          case 12:
            parse_property("color_add_g", Type::Int, true);
            continue;
          case 13:
            parse_property("color_add_b", Type::Int, true);
            continue;
          case 14:
            append_node(Type::IntEvent, Member("x_eve"));
            continue;
          case 15:
            append_node(Type::IntEvent, Member("y_eve"));
            continue;
          case 16:
            append_node(Type::IntEvent, Member("z_eve"));
            continue;
          case 17:
            append_node(Type::IntEvent, Member("mono_eve"));
            continue;
          case 18:
            append_node(Type::IntEvent, Member("reverse_eve"));
            continue;
          case 19:
            append_node(Type::IntEvent, Member("bright_eve"));
            continue;
          case 20:
            append_node(Type::IntEvent, Member("dark_eve"));
            continue;
          case 21:
            append_node(Type::IntEvent, Member("color_r_eve"));
            continue;
          case 22:
            append_node(Type::IntEvent, Member("color_g_eve"));
            continue;
          case 23:
            append_node(Type::IntEvent, Member("color_b_eve"));
            continue;
          case 24:
            append_node(Type::IntEvent, Member("color_rate_eve"));
            continue;
          case 25:
            append_node(Type::IntEvent, Member("color_add_r_eve"));
            continue;
          case 26:
            append_node(Type::IntEvent, Member("color_add_g_eve"));
            continue;
          case 27:
            append_node(Type::IntEvent, Member("color_add_b_eve"));
            continue;
          case 28:
            parse_property("begin_order", Type::Int, true);
            continue;
          case 29:
            parse_property("end_order", Type::Int, true);
            continue;
          case 33:
            parse_property("begin_layer", Type::Int, true);
            continue;
          case 34:
            parse_property("end_layer", Type::Int, true);
            continue;
          default:
            break;
        }
        break;
      case Type::QuakeList:
        switch (key) {
          case -1:
            parse_index(Type::Quake);
            continue;
          default:
            break;
        }
        break;
      case Type::Quake:
        switch (key) {
          case 0:
            append_callable("start", Type::None, NONSIMPLE);
            continue;
          case 1:
            append_callable("start_wait", Type::None, NONSIMPLE | AWAIT);
            continue;
          case 2:
            append_callable("start_wait_key", Type::None, NONSIMPLE | AWAIT);
            continue;
          case 3:
            append_callable("start_nowait", Type::None, NONSIMPLE);
            continue;
          case 4:
            append_callable("start_all", Type::None, NONSIMPLE);
            continue;
          case 5:
            append_callable("start_all_wait", Type::None, NONSIMPLE | AWAIT);
            continue;
          case 6:
            append_callable("start_all_wait_key", Type::None,
                            NONSIMPLE | AWAIT);
            continue;
          case 7:
            append_callable("start_all_nowait", Type::None, NONSIMPLE);
            continue;
          case 8:
            append_callable("end", Type::None, NONSIMPLE);
            continue;
          case 10:
            append_callable("wait", Type::None, AWAIT);
            continue;
          case 11:
            append_callable("wait_key", Type::None, AWAIT);
            continue;
          case 9:
            append_callable("check", Type::Int);
            continue;
          default:
            break;
        }
        break;
      case Type::Input:
        switch (key) {
          case 2:
            append_callable("clear", Type::None);
            continue;
          case 3:
            append_callable("next", Type::None);
            continue;
          case 0:
            append_node(Type::Key, Member("decide"));
            continue;
          case 1:
            append_node(Type::Key, Member("cancel"));
            continue;
          default:
            break;
        }
        break;
      case Type::KeyList:
        switch (key) {
          case -1:
            parse_index(Type::Key);
            continue;
          case 0:
            append_callable("wait", Type::None, AWAIT);
            continue;
          case 1:
            append_callable("wait_force", Type::None, AWAIT);
            continue;
          case 3:
            append_callable("clear", Type::None);
            continue;
          case 5:
            append_callable("next", Type::None);
            continue;
          default:
            break;
        }
        break;
      case Type::Key:
        switch (key) {
          case 1:
            append_callable("on_down", Type::Int);
            continue;
          case 4:
            append_callable("on_up", Type::Int);
            continue;
          case 5:
            append_callable("on_down_up", Type::Int);
            continue;
          case 6:
            append_callable("is_down", Type::Int);
            continue;
          case 7:
            append_callable("is_up", Type::Int);
            continue;
          case 10:
            append_callable("on_flick", Type::Int);
            continue;
          case 0:
            append_callable("get_flick_angle", Type::Int);
            continue;
          case 14:
            append_callable("get_flick_pixel", Type::Int);
            continue;
          case 15:
            append_callable("get_flick_mm", Type::Int);
            continue;
          default:
            break;
        }
        break;
      case Type::Mouse:
        switch (key) {
          case 4:
            append_callable("clear", Type::None);
            continue;
          case 8:
            append_callable("next", Type::None);
            continue;
          case 0:
            append_callable("pos_x", Type::Int);
            continue;
          case 1:
            append_callable("pos_y", Type::Int);
            continue;
          case 2:
            append_callable("get_pos_x", Type::Int);
            continue;
          case 3:
            append_callable("get_pos_y", Type::Int);
            continue;
          case 9:
            append_callable("get_pos", Type::None);
            continue;
          case 10:
            append_callable("set_pos", Type::None);
            continue;
          case 5:
            append_callable("wheel", Type::Int);
            continue;
          case 7:
            append_node(Type::Key, Member("left"));
            continue;
          case 6:
            append_node(Type::Key, Member("right"));
            continue;
          default:
            break;
        }
        break;
      case Type::ObjList:
        switch (key) {
          case -1:
            parse_index(Type::Object);
            continue;
          case 4:
            append_node(Type::Callable, Member("resize"));
            continue;
          case 3:
            append_node(Type::Callable, Member("size"));
            continue;
          default:
            break;
        }
        break;
      case Type::Object:
        switch (key) {
          case 56:
            parse_property("wipe_copy", Type::Int, true);
            continue;
          case 92:
            parse_property("wipe_erase", Type::Int, true);
            continue;
          case 139:
            parse_property("click_disable", Type::Int, true);
            continue;
          case 0:
            parse_property("disp", Type::Int, true);
            continue;
          case 1:
            parse_property("patno", Type::Int, true);
            continue;
          case 44:
            parse_property("world", Type::Int, true);
            continue;
          case 55:
            parse_property("order", Type::Int, true);
            continue;
          case 2:
            parse_property("layer", Type::Int, true);
            continue;
          case 3:
            parse_property("x", Type::Int, true);
            continue;
          case 4:
            parse_property("y", Type::Int, true);
            continue;
          case 5:
            parse_property("z", Type::Int, true);
            continue;
          case 54:
            parse_property("x_rep", Type::IntList);
            continue;
          case 63:
            parse_property("y_rep", Type::IntList);
            continue;
          case 110:
            parse_property("z_rep", Type::IntList);
            continue;
          case 48:
            append_node(Type::Callable, Member("set_pos"));
            continue;
          case 6:
            parse_property("center_x", Type::Int, true);
            continue;
          case 7:
            parse_property("center_y", Type::Int, true);
            continue;
          case 8:
            parse_property("center_z", Type::Int, true);
            continue;
          case 158:
            append_node(Type::Callable, Member("set_center"));
            continue;
          case 9:
            parse_property("center_rep_x", Type::Int, true);
            continue;
          case 10:
            parse_property("center_rep_y", Type::Int, true);
            continue;
          case 11:
            parse_property("center_rep_z", Type::Int, true);
            continue;
          case 159:
            append_node(Type::Callable, Member("set_center_rep"));
            continue;
          case 12:
            parse_property("scale_x", Type::Int, true);
            continue;
          case 13:
            parse_property("scale_y", Type::Int, true);
            continue;
          case 14:
            parse_property("scale_z", Type::Int, true);
            continue;
          case 49:
            append_node(Type::Callable, Member("set_scale"));
            continue;
          case 15:
            parse_property("rotate_x", Type::Int, true);
            continue;
          case 16:
            parse_property("rotate_y", Type::Int, true);
            continue;
          case 17:
            parse_property("rotate_z", Type::Int, true);
            continue;
          case 50:
            append_node(Type::Callable, Member("set_rotate"));
            continue;
          case 18:
            parse_property("clip_use", Type::Int, true);
            continue;
          case 19:
            parse_property("clip_left", Type::Int, true);
            continue;
          case 20:
            parse_property("clip_top", Type::Int, true);
            continue;
          case 21:
            parse_property("clip_right", Type::Int, true);
            continue;
          case 22:
            parse_property("clip_bottom", Type::Int, true);
            continue;
          case 160:
            append_node(Type::Callable, Member("set_clip"));
            continue;
          case 149:
            parse_property("src_clip_use", Type::Int, true);
            continue;
          case 150:
            parse_property("src_clip_left", Type::Int, true);
            continue;
          case 151:
            parse_property("src_clip_top", Type::Int, true);
            continue;
          case 152:
            parse_property("src_clip_right", Type::Int, true);
            continue;
          case 153:
            parse_property("src_clip_bottom", Type::Int, true);
            continue;
          case 161:
            append_node(Type::Callable, Member("set_src_clip"));
            continue;
          case 27:
            parse_property("tr", Type::Int, true);
            continue;
          case 141:
            parse_property("tr_rep", Type::IntList);
            continue;
          case 28:
            parse_property("mono", Type::Int, true);
            continue;
          case 29:
            parse_property("reverse", Type::Int, true);
            continue;
          case 30:
            parse_property("bright", Type::Int, true);
            continue;
          case 31:
            parse_property("dark", Type::Int, true);
            continue;
          case 32:
            parse_property("color_r", Type::Int, true);
            continue;
          case 33:
            parse_property("color_g", Type::Int, true);
            continue;
          case 34:
            parse_property("color_b", Type::Int, true);
            continue;
          case 23:
            parse_property("color_rate", Type::Int, true);
            continue;
          case 57:
            parse_property("color_add_r", Type::Int, true);
            continue;
          case 58:
            parse_property("color_add_g", Type::Int, true);
            continue;
          case 59:
            parse_property("color_add_b", Type::Int, true);
            continue;
          case 145:
            parse_property("mask_no", Type::Int, true);
            continue;
          case 109:
            parse_property("tonecurve_no", Type::Int, true);
            continue;
          case 146:
            parse_property("culling", Type::Int, true);
            continue;
          case 147:
            parse_property("alpha_test", Type::Int, true);
            continue;
          case 148:
            parse_property("alpha_blend", Type::Int, true);
            continue;
          case 46:
            parse_property("blend", Type::Int, true);
            continue;
          case 168:
            parse_property("light_no", Type::Int, true);
            continue;
          case 144:
            parse_property("fog_use", Type::Int, true);
            continue;
          case 90:
            parse_property("patno_eve", Type::IntEvent);
            continue;
          case 51:
            parse_property("x_eve", Type::IntEvent);
            continue;
          case 64:
            parse_property("y_eve", Type::IntEvent);
            continue;
          case 65:
            parse_property("z_eve", Type::IntEvent);
            continue;
          case 112:
            parse_property("x_rep_eve", Type::IntEventList);
            continue;
          case 113:
            parse_property("y_rep_eve", Type::IntEventList);
            continue;
          case 114:
            parse_property("z_rep_eve", Type::IntEventList);
            continue;
          case 77:
            parse_property("center_x_eve", Type::IntEvent);
            continue;
          case 78:
            parse_property("center_y_eve", Type::IntEvent);
            continue;
          case 79:
            parse_property("center_z_eve", Type::IntEvent);
            continue;
          case 80:
            parse_property("center_rep_x_eve", Type::IntEvent);
            continue;
          case 81:
            parse_property("center_rep_y_eve", Type::IntEvent);
            continue;
          case 82:
            parse_property("center_rep_z_eve", Type::IntEvent);
            continue;
          case 67:
            parse_property("scale_x_eve", Type::IntEvent);
            continue;
          case 68:
            parse_property("scale_y_eve", Type::IntEvent);
            continue;
          case 66:
            parse_property("scale_z_eve", Type::IntEvent);
            continue;
          case 69:
            parse_property("rotate_x_eve", Type::IntEvent);
            continue;
          case 70:
            parse_property("rotate_y_eve", Type::IntEvent);
            continue;
          case 71:
            parse_property("rotate_z_eve", Type::IntEvent);
            continue;
          case 105:
            parse_property("clip_left_eve", Type::IntEvent);
            continue;
          case 106:
            parse_property("clip_top_eve", Type::IntEvent);
            continue;
          case 107:
            parse_property("clip_right_eve", Type::IntEvent);
            continue;
          case 108:
            parse_property("clip_bottom_eve", Type::IntEvent);
            continue;
          case 154:
            parse_property("src_clip_left_eve", Type::IntEvent);
            continue;
          case 155:
            parse_property("src_clip_top_eve", Type::IntEvent);
            continue;
          case 156:
            parse_property("src_clip_right_eve", Type::IntEvent);
            continue;
          case 157:
            parse_property("src_clip_bottom_eve", Type::IntEvent);
            continue;
          case 72:
            parse_property("tr_eve", Type::IntEvent);
            continue;
          case 140:
            parse_property("tr_rep_eve", Type::IntEventList);
            continue;
          case 73:
            parse_property("mono_eve", Type::IntEvent);
            continue;
          case 74:
            parse_property("reverse_eve", Type::IntEvent);
            continue;
          case 75:
            parse_property("bright_eve", Type::IntEvent);
            continue;
          case 76:
            parse_property("dark_eve", Type::IntEvent);
            continue;
          case 87:
            parse_property("color_r_eve", Type::IntEvent);
            continue;
          case 88:
            parse_property("color_g_eve", Type::IntEvent);
            continue;
          case 89:
            parse_property("color_b_eve", Type::IntEvent);
            continue;
          case 83:
            parse_property("color_rate_eve", Type::IntEvent);
            continue;
          case 84:
            parse_property("color_add_r_eve", Type::IntEvent);
            continue;
          case 85:
            parse_property("color_add_g_eve", Type::IntEvent);
            continue;
          case 86:
            parse_property("color_add_b_eve", Type::IntEvent);
            continue;
          case 91: {
            if (elmcode.size() < 2) {
              warn_("[AllEvent] expected operation");
              elmcode = elmcode.subspan(1);
              continue;
            }
            const int peek = AsInt(elmcode[1]);
            switch (peek) {
              case 0:  // ALLEVENT_END
                result.nodes.emplace_back(
                    Type::Callable, Member("alleve_end", Type::None, true));
                elmcode = elmcode.subspan(2);
                break;
              case 1:
                result.nodes.emplace_back(
                    Type::Callable,
                    Member("alleve_wait", Type::None, true, true, true));
                elmcode = elmcode.subspan(2);
                break;
              case 2:
                result.nodes.emplace_back(
                    Type::Callable, Member("alleve_check", Type::Int, true));
                elmcode = elmcode.subspan(2);
                break;
              default:
                result.nodes.emplace_back(Type::Invalid, Member("alleve_???"));
                elmcode = elmcode.subspan(2);
                break;
            }

            continue;
          }
          case 169:
            append_node(Type::Int, Member("pat_cnt"));
            continue;
          case 24:
            append_callable("get_size_x", Type::Int);
            continue;
          case 25:
            append_callable("get_size_y", Type::Int);
            continue;
          case 47:
            append_callable("get_size_z", Type::Int);
            continue;
          case 100:
            append_callable("get_pixel_color_a", Type::Int);
            continue;
          case 101:
            append_callable("get_pixel_color_r", Type::Int);
            continue;
          case 102:
            append_callable("get_pixel_color_g", Type::Int);
            continue;
          case 103:
            append_callable("get_pixel_color_b", Type::Int);
            continue;
          case 62:
            append_callable("get_file_path", Type::String);
            continue;
          case 111:
            parse_property("F", Type::IntList);
            continue;
          case 93:
            parse_property("child", Type::ObjList);
            continue;
          case 35:
            append_callable("init", Type::None);
            continue;
          case 36:
            append_callable("free", Type::None);
            continue;
          case 37:
            append_callable("init_param", Type::None);
            continue;
          case 38:
            append_node(Type::Callable, Member("create"));
            continue;
          case 40:
            append_node(Type::Callable, Member("create_rect"));
            continue;
          case 39:
            append_callable("create_string");
            continue;
          case 132:
            append_callable("create_number");
            continue;
          case 129:
            append_node(Type::Callable, Member("create_weather"));
            continue;
          case 43:
            append_node(Type::Callable, Member("create_mesh"));
            continue;
          case 45:
            append_node(Type::Callable, Member("create_billboard"));
            continue;
          case 116:
            append_node(Type::Callable, Member("create_save_thumb"));
            continue;
          case 170:
            append_node(Type::Callable, Member("create_capture_thumb"));
            continue;
          case 165:
            append_node(Type::Callable, Member("create_capture"));
            continue;
          case 120:
            append_callable("create_movie", Type::None, NONSIMPLE);
            continue;
          case 121:
            append_callable("create_movie_loop", Type::None, NONSIMPLE);
            continue;
          case 122:
            append_callable("create_movie_wait", Type::None, NONSIMPLE | AWAIT);
            continue;
          case 143:
            append_callable("create_movie_waitkey", Type::Int,
                            NONSIMPLE | AWAIT);
            continue;
          case 177:
            append_node(Type::Callable, Member("create_emote"));
            continue;
          case 0x01000000:
            append_callable("load_gan", Type::None);
            continue;
          case 0x01000001:
            append_callable("start_gan", Type::None);
            continue;
          case 41:
            append_node(Type::Callable, Member("copy_from"));
            continue;
          case 53:
            append_node(Type::Callable, Member("change_file"));
            continue;
          case 174:
            append_callable("exist_type", Type::Int);
            continue;
          case 99:
            append_callable("set_string");
            continue;
          case 135:
            append_callable("get_string", Type::String);
            continue;
          case 104:
            append_callable("set_string_param");
            continue;
          case 133:
            append_callable("set_number");
            continue;
          case 136:
            append_callable("get_number", Type::Int);
            continue;
          case 134:
            append_callable("set_number_param");
            continue;
          case 130:
            append_node(Type::Callable, Member("set_weather_param_type_a"));
            continue;
          case 131:
            append_node(Type::Callable, Member("set_weather_param_type_a"));
            continue;
          case 125:
            append_callable("pause_movie", Type::None);
            continue;
          case 126:
            append_callable("resume_movie", Type::None);
            continue;
          case 137:
            append_node(Type::Callable, Member("seek_movie"));
            continue;
          case 138:
            append_callable("get_movie_seek_time", Type::Int);
            continue;
          case 127:
            append_callable("check_movie", Type::Int);
            continue;
          case 128:
            append_callable("wait_movie", Type::None, AWAIT);
            continue;
          case 142:
            append_callable("wait_movie_key", Type::Int, AWAIT);
            continue;
          case 171:
            append_callable("end_movie_loop", Type::None);
            continue;
          case 172:
            append_node(Type::Callable, Member("set_movie_auto_free"));
            continue;
          case 52:
            parse_property("frame_action", Type::FrameAction);
            continue;
          case 115:
            parse_property("frame_action_ch", Type::FrameActionList);
            continue;
          case 61:
            append_callable("clear_button", Type::None);
            continue;
          case 42:
            append_node(Type::Callable, Member("set_button"));
            continue;
          case 164:
            append_node(Type::Callable, Member("set_button_group"));
            continue;
          case 98:
            append_node(Type::Callable, Member("set_button_pushkeep"));
            continue;
          case 119:
            append_callable("get_button_pushkeep", Type::Int);
            continue;
          case 175:
            append_node(Type::Callable, Member("set_button_alpha_test"));
            continue;
          case 176:
            append_callable("get_button_alpha_test", Type::Int);
            continue;
          case 95:
            append_callable("set_button_state_normal", Type::None);
            continue;
          case 96:
            append_callable("set_button_state_select", Type::None);
            continue;
          case 97:
            append_callable("set_button_state_disable", Type::None);
            continue;
          case 118:
            append_callable("get_button_state", Type::Int);
            continue;
          case 123:
            append_callable("get_button_hit_state", Type::Int);
            continue;
          case 124:
            append_callable("get_button_real_state", Type::Int);
            continue;
          case 26:
            append_node(Type::Callable, Member("set_button_call"));
            continue;
          case 60:
            append_callable("clear_button_call", Type::None);
            continue;
          default:
            break;
        }
        break;
      case Type::G00bufList:
        switch (key) {
          case -1:
            parse_index(Type::G00buf);
            continue;
          case 0:
            append_callable("free_all");
            continue;
          case 2:
            append_callable("size", Type::Int);
            continue;
          default:
            break;
        }
        break;
      case Type::G00buf:
        switch (key) {
          case 0:
            append_callable("load");
            continue;
          case 1:
            append_callable("free");
            continue;
          default:
            break;
        }
        break;
      case Type::MaskList:
        switch (key) {
          case -1:
            parse_index(Type::Mask);
            continue;
          case 1:
            append_callable("size", Type::Int);
            continue;
          default:
            break;
        }
        break;
      case Type::Mask:
        switch (key) {
          case 1:
            append_callable("init");
            continue;
          case 0:
            append_callable("create", Type::None);
            continue;
          case 4:
            parse_property("x", Type::Int, true);
            continue;
          case 5:
            parse_property("y", Type::Int, true);
            continue;
          case 2:
            parse_property("x_eve", Type::IntEvent);
            continue;
          case 3:
            parse_property("y_eve", Type::IntEvent);
            continue;
          default:
            break;
        }
        break;
      case Type::Movie:
        switch (key) {
          case 0:
            append_callable("play", Type::None);
            continue;
          case 2:
            append_callable("play_wait", Type::None, AWAIT);
            continue;
          case 3:
            append_callable("play_waitkey", Type::Int, AWAIT);
            continue;
          case 1:
            append_callable("stop", Type::None);
            continue;
          default:
            break;
        }
        break;
      case Type::BgmTable:
        switch (key) {
          case 0:
            append_node(Type::Callable, Member("cnt"));
            continue;
          case 2:
            append_node(Type::Callable, Member("set_listen"));
            continue;
          case 4:
            append_node(Type::Callable, Member("set_listen_all"));
            continue;
          case 1:
            append_node(Type::Callable, Member("get_listen"));
            continue;
          default:
            break;
        }
        break;
      case Type::Bgm:
        switch (key) {
          case 0:
            append_callable("play");
            continue;
          case 1:
            append_callable("play_oneshot");
            continue;
          case 2:
            append_callable("play_wait", Type::None, AWAIT);
            continue;
          case 16:
            append_callable("ready");
            continue;
          case 17:
            append_callable("ready_oneshot");
            continue;
          case 4:
            append_callable("stop");
            continue;
          case 10:
            append_callable("pause");
            continue;
          case 11:
            append_callable("resume");
            continue;
          case 12:
            append_callable("resume_wait", Type::None, AWAIT);
            continue;
          case 3:
            append_callable("wait", Type::None, AWAIT);
            continue;
          case 14:
            append_callable("wait_key", Type::Int, AWAIT);
            continue;
          case 5:
            append_callable("wait_fade", Type::None, AWAIT);
            continue;
          case 15:
            append_callable("wait_fade_key", Type::Int, AWAIT);
            continue;
          case 18:
            append_callable("check", Type::Int);
            continue;
          case 6:
            append_callable("set_volume");
            continue;
          case 7:
            append_callable("set_volume_max");
            continue;
          case 8:
            append_callable("set_volume_min");
            continue;
          case 9:
            append_callable("get_volume", Type::Int);
            continue;
          case 19:
            append_callable("get_regist_name", Type::String);
            continue;
          case 13:
            append_callable("get_play_pos", Type::Int);
            continue;
          default:
            break;
        }
        break;
      case Type::Se:
        switch (key) {
          case 0:
            append_callable("play");
            continue;
          case 5:
            append_callable("play_by_file_name");
            continue;
          case 6:
            append_callable("play_by_koe_no");
            continue;
          case 9:
            append_callable("play_by_se_no");
            continue;
          case 7:
            append_callable("stop");
            continue;
          case 8:
            append_callable("wait", Type::None, AWAIT);
            continue;
          case 10:
            append_callable("wait_key", Type::Int, AWAIT);
            continue;
          case 11:
            append_callable("check", Type::Int);
            continue;
          case 1:
            append_callable("set_volume");
            continue;
          case 2:
            append_callable("set_volume_max");
            continue;
          case 3:
            append_callable("set_volume_min");
            continue;
          case 4:
            append_callable("get_volume", Type::Int);
            continue;
          default:
            break;
        }
        break;
      case Type::MwndList:
        switch (key) {
          case -1:
            parse_index(Type::Mwnd);
            continue;
          case 1:
            append_callable("close", Type::None);
            continue;
          case 2:
            append_callable("close_wait", Type::None);
            continue;
          case 3:
            append_callable("close_nowait", Type::None);
            continue;
          default:
            break;
        }
        break;
      case Type::Mwnd:
        switch (key) {
          case 0:
            append_callable("set_waku");
            continue;
          case 79:
            append_callable("init_waku_file", Type::None);
            continue;
          case 78:
            append_callable("set_waku_file");
            continue;
          case 80:
            append_callable("get_waku_file", Type::String);
            continue;
          case 82:
            append_callable("init_filter_file", Type::None);
            continue;
          case 81:
            append_callable("set_filter_file");
            continue;
          case 83:
            append_callable("get_filter_file", Type::String);
            continue;
          case 1:
            append_callable("open", Type::None);
            continue;
          case 15:
            append_callable("open_wait", Type::None);
            continue;
          case 16:
            append_callable("open_nowait", Type::None);
            continue;
          case 65:
            append_callable("check_open", Type::Int);
            continue;
          case 2:
            append_callable("close", Type::None);
            continue;
          case 13:
            append_callable("close_wait", Type::None);
            continue;
          case 14:
            append_callable("close_nowait", Type::None);
            continue;
          case 64:
            append_callable("end_close", Type::None);
            continue;
          case 49:
            append_callable("msg_block", Type::None);
            continue;
          case 59:
            append_callable("msg_pp_block", Type::None);
            continue;
          case 3:
            append_callable("clear", Type::None);
            continue;
          case 55:
            append_callable("novel_clear", Type::None);
            continue;
          case 4:
            append_callable("print", Type::None, AWAIT);
            continue;
          case 57: {
            switch (elm.bind_ctx.overload_id) {
              case 0:
                append_callable("overflow_print");
                break;
              case 1:
                append_callable("print", Type::None, AWAIT);
                break;
              default:
                warn_(std::format(
                    "[Callable] overload {} not found while parsing {}",
                    elm.bind_ctx.overload_id, result.ToDebugString()));
                append_callable("overflow_print");
                break;
            }
            continue;
          }
          case 63:
            append_callable("overflow_name");
            continue;
          case 12: {
            switch (elm.bind_ctx.overload_id) {
              case 0:
                append_callable("ruby_end");
                break;
              default:
                append_callable("ruby_start");
                break;
            }
            continue;
          }
          case 18:
            append_callable("msg_wait", Type::None, AWAIT);
            continue;
          case 19:
            append_callable("pp", Type::None, AWAIT);
            continue;
          case 20:
            append_callable("r", Type::None, AWAIT);
            continue;
          case 54:
            append_callable("page", Type::None, AWAIT);
            continue;
          case 6:
            append_callable("nl", Type::None);
            continue;
          case 17:
            append_callable("nil", Type::None);
            continue;
          case 56:
            append_callable("indent", Type::None);
            continue;
          case 28:
            append_callable("clear_indent", Type::None);
            continue;
          case 31:
            append_callable("enable_multi_msg", Type::None);
            continue;
          case 29:
            append_callable("next_msg", Type::None);
            continue;
          case 58:
            append_callable("set_slide_msg");
            continue;
          case 60:
            append_callable("set_slide_msg", Type::None);
            continue;
          case 61:
            append_callable("slide_msg", Type::None);
            continue;
          case 5:
            append_callable("sel", Type::Int, AWAIT | NONSIMPLE | CONTEXT);
            continue;
          case 51:
            append_callable("sel_cancel", Type::Int,
                            AWAIT | NONSIMPLE | CONTEXT);
            continue;
          case 50:
            append_callable("selmsg", Type::Int, AWAIT | NONSIMPLE | CONTEXT);
            continue;
          case 52:
            append_callable("selmsg_cancel", Type::Int,
                            AWAIT | NONSIMPLE | CONTEXT);
            continue;
          case 84: {
            switch (elm.bind_ctx.overload_id) {
              case 0:
                append_callable("rep_pos_default");
                break;
              case 1:
                append_callable("rep_pos");
                break;
              default:
                warn_(std::format(
                    "[Callable] overload {} not found while parsing {}",
                    elm.bind_ctx.overload_id, result.ToDebugString()));
                append_callable("rep_pos_default");
                break;
            }
            continue;
          }
          case 7: {
            switch (elm.bind_ctx.overload_id) {
              case 0:
                append_callable("size_default");
                break;
              case 1:
                append_callable("size");
                break;
              default:
                warn_(std::format(
                    "[Callable] overload {} not found while parsing {}",
                    elm.bind_ctx.overload_id, result.ToDebugString()));
                append_callable("size_default");
                break;
            }
            continue;
          }
          case 8: {
            switch (elm.bind_ctx.overload_id) {
              case 0:
                append_callable("color_default");
                break;
              case 1:
                append_callable("color");
                break;
              default:
                warn_(std::format(
                    "[Callable] overload {} not found while parsing {}",
                    elm.bind_ctx.overload_id, result.ToDebugString()));
                append_callable("color_default");
                break;
            }
            continue;
          }
          case 86:
            append_node(Type::Callable, Member("msgbtn"));
            continue;
          case 85:
            append_node(Type::Callable, Member("set_namae"));
            continue;
          case 9:
            append_callable("koe", Type::None, NONSIMPLE);
            continue;
          case 26:
            append_callable("koe_play_wait", Type::None, NONSIMPLE | AWAIT);
            continue;
          case 27:
            append_callable("koe_play_wait_key", Type::Int, NONSIMPLE | AWAIT);
            continue;
          case 22:
            append_callable("clear_face", Type::None);
            continue;
          case 21:
            append_node(Type::Callable, Member("set_face"));
            continue;
          case 10:
            parse_property("layer", Type::Int, true);
            continue;
          case 11:
            parse_property("world", Type::Int, true);
            continue;
          case 32:
            parse_property("button", Type::ObjList);
            continue;
          case 53:
            parse_property("face", Type::ObjList);
            continue;
          case 30:
            parse_property("object", Type::ObjList);
            continue;
          case 66:
            append_node(Type::None, Member("init_window_pos"));
            continue;
          case 67:
            append_node(Type::None, Member("init_window_size"));
            continue;
          case 74:
            append_node(Type::None, Member("init_window_moji_cnt"));
            continue;
          case 68:
            append_node(Type::Callable, Member("set_window_pos"));
            continue;
          case 69:
            append_node(Type::Callable, Member("set_window_size"));
            continue;
          case 75:
            append_node(Type::Callable, Member("set_window_moji_cnt"));
            continue;
          case 70:
            append_node(Type::Int, Member("get_window_pos_x"));
            continue;
          case 71:
            append_node(Type::Int, Member("get_window_pos_y"));
            continue;
          case 72:
            append_node(Type::Int, Member("get_window_size_x"));
            continue;
          case 73:
            append_node(Type::Int, Member("get_window_size_y"));
            continue;
          case 77:
            append_node(Type::Int, Member("get_window_moji_cnt_x"));
            continue;
          case 76:
            append_node(Type::Int, Member("get_window_moji_cnt_y"));
            continue;
          case 41:
            append_node(Type::None, Member("init_open_anime_type"));
            continue;
          case 42:
            append_node(Type::None, Member("init_open_anime_time"));
            continue;
          case 43:
            append_node(Type::None, Member("init_close_anime_type"));
            continue;
          case 44:
            append_node(Type::None, Member("init_close_anime_time"));
            continue;
          case 37:
            append_node(Type::Callable, Member("set_open_anime_type"));
            continue;
          case 34:
            append_node(Type::Callable, Member("set_open_anime_time"));
            continue;
          case 45:
            append_node(Type::Callable, Member("set_close_anime_type"));
            continue;
          case 35:
            append_node(Type::Callable, Member("set_close_anime_time"));
            continue;
          case 48:
            append_node(Type::Int, Member("get_open_anime_type"));
            continue;
          case 47:
            append_node(Type::Int, Member("get_open_anime_time"));
            continue;
          case 38:
            append_node(Type::Int, Member("get_close_anime_type"));
            continue;
          case 46:
            append_node(Type::Int, Member("get_close_anime_time"));
            continue;
          case 36:
            append_node(Type::Int, Member("get_default_open_anime_type"));
            continue;
          case 33:
            append_node(Type::Int, Member("get_default_open_anime_time"));
            continue;
          case 40:
            append_node(Type::Int, Member("get_default_close_anime_type"));
            continue;
          case 39:
            append_node(Type::Int, Member("get_default_close_anime_time"));
            continue;
          default:
            break;
        }
        break;
      case Type::Msgbk:
        switch (key) {
          case 1:
            append_callable("insert_msg", Type::None);
            continue;
          case 4:
            append_callable("add_koe", Type::None);
            continue;
          case 5:
            append_callable("add_namae", Type::None);
            continue;
          case 3:
            append_callable("add_msg", Type::None);
            continue;
          case 2:
            append_callable("go_next_msg", Type::None);
            continue;
          default:
            break;
        }
        break;
      case Type::Pcm:
        switch (key) {
          case 0:
            append_callable("play", Type::None);
            continue;
          case 1:
            append_callable("stop", Type::None);
            continue;
          default:
            break;
        }
        break;
      case Type::PcmchList:
        switch (key) {
          case -1:
            parse_index(Type::Pcmch);
            continue;
          default:
            break;
        }
        break;
      case Type::Pcmch:
        switch (key) {
          case 0:
            append_callable("play");
            continue;
          case 2:
            append_callable("play_loop");
            continue;
          case 1:
            append_callable("play_wait", Type::None, AWAIT);
            continue;
          case 11:
            append_callable("ready");
            continue;
          case 16:
            append_callable("ready_loop");
            continue;
          case 5:
            append_callable("stop");
            continue;
          case 10:
            append_callable("pause");
            continue;
          case 9:
            append_callable("resume");
            continue;
          case 17:
            append_callable("resume_wait", Type::None, AWAIT);
            continue;
          case 3:
            append_callable("wait", Type::None, AWAIT);
            continue;
          case 6:
            append_callable("wait_key", Type::Int, AWAIT);
            continue;
          case 8:
            append_callable("wait_fade", Type::None, AWAIT);
            continue;
          case 7:
            append_callable("wait_fade_key", Type::Int, AWAIT);
            continue;
          case 4:
            append_callable("check", Type::Int);
            continue;
          case 13:
            append_callable("set_volume");
            continue;
          case 14:
            append_callable("set_vol_max");
            continue;
          case 15:
            append_callable("set_vol_min");
            continue;
          case 12:
            append_callable("get_volume", Type::Int);
            continue;
          default:
            break;
        }
        break;
      case Type::Wipe:
        switch (key) {
          case 7:
            append_callable("wipe", Type::None, AWAIT | NONSIMPLE);
            continue;
          case 23:
            append_callable("wipe_all", Type::None, NONSIMPLE | AWAIT);
            continue;
          case 51:
            append_callable("wipe_mask", Type::None, AWAIT | NONSIMPLE);
            continue;
          case 50:
            append_callable("wipe_mask_all", Type::None, AWAIT | NONSIMPLE);
            continue;
          case 33:
            append_callable("end", Type::None, NONSIMPLE);
            continue;
          case 103:
            append_callable("wait", Type::Int, AWAIT);
            continue;
          case 109:
            append_callable("check", Type::Int);
            continue;
          default:
            break;
        }
        break;
      default:
        break;
    }
    // An unrecognized member or terminal type leaves the remaining code intact.
    break;
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
