// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
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

#include "libsiglus/parser.hpp"

#include "libsiglus/lexeme.hpp"
#include "libsiglus/token.hpp"
#include "libsiglus/value.hpp"

#include <exception>
#include <format>
#include <ranges>

namespace libsiglus {
using namespace token;

// -----------------------------------------------------------------------
// class Parser
Parser::Parser(std::string_view rawdata,
               std::span<const std::string> strpool,
               std::span<const int> labels,
               std::span<const int> zlabels,
               std::span<const Property> scnprop,
               std::span<const Property> globalprop,
               std::span<const ::libsiglus::Command> scncmd,
               std::span<const ::libsiglus::Command> gcmd,
               int scnno,
               std::string_view debug_title)
    : raw_(rawdata),
      debug_title_(debug_title),
      strpool_(strpool),
      labels_(labels),
      zlabels_(zlabels),
      scnprop_(scnprop),
      gprop_(globalprop),
      scncmd_(scncmd),
      gcmd_(gcmd),
      reader_(""),
      scnno_(scnno),
      elm_parser_(
          scnprop_,
          gprop_,
          scncmd_,
          gcmd_,
          curcall_args_,
          scnno_,
          [this] { return read_kidoku(); },
          [this](std::string message) {
            warnings_.emplace_back(std::move(message));
          }) {}

Variable Parser::add_var(Type type) {
  Variable var(type, var_cnt_++);
  return var;
}

Value Parser::pop(Type type) {
  if (stack_.Empty())
    return {};

  switch (type) {
    case Type::Int:
      return stack_.Popint();
    case Type::String:
      return stack_.Popstr();

    case Type::Other:  // ignore
    case Type::Invalid:
    case Type::Callable:
    case Type::None:
      return {};

    default: {
      token::ElmAlias tok;
      tok.elmcode = stack_.Popelm();
      tok.elmcode.force_bind = false;
      tok.chain = elm_parser_.Parse(tok.elmcode);
      Variable var = add_var(type);
      tok.dst = var;
      emit_token(std::move(tok));
      return var;
    }
  }
}

Value Parser::pop_arg(const elm::ArgumentList::node_t& node) {
  return std::visit(
      [&](const auto& x) -> Value {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::same_as<T, Type>)
          return pop(x);
        else {  // type is list
          // Pop backward from the stack while retaining the source list order.
          std::vector<Value> vals(x.args.size());
          for (size_t i = x.args.size(); i > 0; --i)
            vals[i - 1] = pop_arg(x.args[i - 1]);
          return List(std::move(vals));
        }
      },
      node);
}

void Parser::push(const token::GetProperty& prop) {
  Type type = Typeof(prop.dst);

  switch (type) {
    case Type::Int:
    case Type::String:
      push(prop.dst);
      break;

    default:
      push(prop.elmcode);
      break;
  }
}

void Parser::add_label(int id) { emit_token(Label{id}); }
void Parser::add_zlabel(int id) { emit_token(Zlabel{id}); }

expected<std::pair<Parser::Tokens, Parser::Warnings>, std::string>
Parser::ParseAll() noexcept {
  try {
    parsed_.clear(), warnings_.clear();

    reader_ = ByteReader(raw_);
    var_cnt_ = lineno_ = 0;
    offset2cmd_.clear();
    offset2labels_.clear();
    offset2zlabels_.clear();
    curcall_cmd_ = nullptr;
    curcall_args_.clear();
    stack_.Clear();

    for (size_t i = 0; i < labels_.size(); ++i)
      offset2labels_.emplace(labels_[i], i);  // (location, lid)
    for (size_t i = 0; i < zlabels_.size(); ++i)
      offset2zlabels_.emplace(zlabels_[i], i);  // (location, zid)
    for (size_t i = 0; i < scncmd_.size(); ++i)
      offset2cmd_.emplace(scncmd_[i].offset, &scncmd_[i]);
    for (size_t i = 0; i < gcmd_.size(); ++i)
      if (gcmd_[i].scene_id == scnno_)
        offset2cmd_.emplace(gcmd_[i].offset, &gcmd_[i]);

    while (reader_.Position() < reader_.Size()) {
      // Add labels
      for (auto [begin, end] = offset2labels_.equal_range(reader_.Position());
           begin != end; ++begin) {
        add_label(begin->second);
        debug_assert_stack_empty();
      }

      // Add zlabels
      for (auto [begin, end] = offset2zlabels_.equal_range(reader_.Position());
           begin != end; ++begin) {
        const auto [loc, zid] = *begin;
        if (loc <= 0)
          continue;  // implicit zlabel -> use %%script instead
        add_zlabel(begin->second);
        debug_assert_stack_empty();
      }

      // update curcall
      const auto it = offset2cmd_.find(reader_.Position());
      if (it != offset2cmd_.cend()) {
        curcall_cmd_ = it->second;
        curcall_args_.clear();
        inside_curcall_body = false;
        debug_assert_stack_empty();
      }

      static Lexer lexer;
      auto lex = lexer.Parse(reader_);
      Add(std::move(lex));
    }

    return std::make_pair(std::move(parsed_), std::move(warnings_));
  } catch (const std::exception& e) {
    std::string errmsg = e.what();
    errmsg += "\nstack:\n";
    errmsg += stack_.ToDebugString();
    return unexpected(std::move(errmsg));
  } catch (...) {
    std::string errmsg = "unknown parser error\nstack:\n";
    errmsg += stack_.ToDebugString();
    return unexpected(std::move(errmsg));
  }
}

void Parser::Add(lex::Push p) {
  switch (p.type_) {
    case Type::Int:
      push(Integer(p.value_));
      break;
    case Type::String:
      push(String(strpool_[p.value_]));
      break;

    default:  // ignore
      break;
  }
}

void Parser::Add(lex::Pop p) { pop(p.type_); }

void Parser::Add(lex::Line line) {
  std::ignore = line.linenum_;  // lex::line marks the original siglus debug
                                // symbols, we assign new line numbers starting
                                // from 1 for each token in parser
  // is it safe to assume the stack is empty here?
  debug_assert_stack_empty();
}

void Parser::Add(lex::Marker marker) { stack_.PushMarker(); }

void Parser::Add(lex::Property) {
  token::GetProperty tok;
  tok.elmcode = stack_.Popelm();
  tok.chain = elm_parser_.Parse(tok.elmcode);
  tok.dst = add_var(tok.chain.GetType());

  push(tok);
  emit_token(std::move(tok));
}

void Parser::Add(lex::Command command) {
  auto& sig = command.sig;

  token::Command tok;
  elm::Invoke invoke;

  invoke.return_type = sig.rettype;
  invoke.overload_id = sig.overload_id;

  invoke.named_arg.resize(sig.argtags.size());
  invoke.arg.resize(sig.arglist.size() - sig.argtags.size());
  for (auto it = invoke.named_arg.rbegin(); it != invoke.named_arg.rend();
       ++it) {
    it->first = sig.argtags.back();
    it->second = pop_arg(sig.arglist.args.back());
    sig.argtags.pop_back();
    sig.arglist.args.pop_back();
  }
  for (auto it = invoke.arg.rbegin(); it != invoke.arg.rend(); ++it) {
    *it = pop_arg(sig.arglist.args.back());
    sig.arglist.args.pop_back();
  }

  tok.elmcode = stack_.Popelm();
  tok.dst = add_var(invoke.return_type);
  tok.elmcode.ForceBind(std::move(invoke));
  tok.chain = elm_parser_.Parse(tok.elmcode);

  push(tok.dst);

  emit_token(std::move(tok));
}

void Parser::Add(lex::Operate1 op) {  // + - ~ <int>
  token::Operate1 tok;
  tok.rhs = stack_.Popint();
  tok.dst = add_var(Type::Int);
  tok.op = op.op_;
  tok.val = TryEval(tok.op, tok.rhs);

  push(tok.val.value_or(tok.dst));
  emit_token(std::move(tok));
}

void Parser::Add(lex::Operate2 op) {
  Type result_type = Type::Invalid;

  if (op.ltype_ == Type::Int && op.rtype_ == Type::Int)
    result_type = Type::Int;
  else if (op.ltype_ == Type::String && op.rtype_ == Type::Int)
    result_type = Type::String;  // str * int
  else if (op.ltype_ == Type::String && op.rtype_ == Type::String) {
    result_type = op.op_ == OperatorCode::Plus ? Type::String /* str+str */
                                               : Type::Int /* str <comp> str */;
  }

  token::Operate2 tok;
  tok.dst = add_var(result_type);
  tok.op = op.op_;
  tok.rhs = pop(op.rtype_);
  tok.lhs = pop(op.ltype_);
  tok.val = TryEval(tok.lhs, tok.op, tok.rhs);

  push(tok.val.value_or(tok.dst));
  emit_token(std::move(tok));
}

void Parser::Add(lex::Copy cp) {
  Value src;
  Variable dst = add_var(cp.type_);

  switch (cp.type_) {
    case Type::Int:
      src = stack_.Backint();
      break;
    case Type::String:
      src = stack_.Backstr();
      break;

    default:
      break;
  }

  push(dst);
  emit_token(Duplicate{std::move(src), std::move(dst)});
}

void Parser::Add(lex::CopyElm) { push(stack_.Backelm()); }

void Parser::Add(lex::Goto g) {
  if (g.cond_ == lex::Goto::Condition::Unconditional)
    emit_token(Goto{g.label_});
  else {
    GotoIf tok;
    tok.cond = g.cond_ == lex::Goto::Condition::True;
    tok.label = g.label_;
    tok.src = pop(Type::Int);

    emit_token(std::move(tok));
  }
}

void Parser::Add(lex::Assign a) {
  token::Assign tok;
  tok.src = pop(a.rtype_);
  tok.dst_elmcode = stack_.Popelm();
  tok.dst = elm_parser_.Parse(tok.dst_elmcode);
  emit_token(std::move(tok));
}

void Parser::Add(lex::Gosub s) {
  token::Gosub tok;

  tok.dst = add_var(s.return_type_);

  std::vector<Value> args;
  args.reserve(s.argt_.size());
  std::transform(s.argt_.args.rbegin(), s.argt_.args.rend(),
                 std::back_inserter(args),
                 [&](const auto& t) { return pop_arg(t); });
  tok.args = std::move(args);
  tok.entry_id = s.label_;

  push(tok.dst);
  emit_token(std::move(tok));
}

void Parser::Add(lex::Arg a) {
  token::Subroutine tok;
  if (curcall_cmd_) {
    tok.name = curcall_cmd_->name;
    tok.source_entry = curcall_cmd_->offset;
  }
  tok.args = curcall_args_;
  emit_token(std::move(tok));

  inside_curcall_body = true;
}

void Parser::Add(lex::Return r) {
  token::Return ret;
  ret.ret_vals.reserve(r.ret_types_.size());
  std::transform(r.ret_types_.args.rbegin(), r.ret_types_.args.rend(),
                 std::back_inserter(ret.ret_vals),
                 [&](const auto& t) { return pop_arg(t); });
  emit_token(std::move(ret));
}

void Parser::Add(lex::Declare d) {
  std::ignore = d.prop_id;
  int size = 0;
  if (d.type == Type::IntList || d.type == Type::StrList) {
    Value val = pop(Type::Int);
    auto* it = std::get_if<Integer>(&val);
    if (!it) {
      warnings_.emplace_back("expected integer, but got " + ToString(val));
    } else
      size = it->val_;
  }
  if (inside_curcall_body) {
    // local variable declaration
    emit_token(
        LocalVar{.id = curcall_args_.size(), .type = d.type, .size = size});
  }
  curcall_args_.push_back(d.type);
}

void Parser::Add(lex::Namae) { emit_token(Name{pop(Type::String)}); }

void Parser::Add(lex::Textout t) {
  emit_token(Textout{.kidoku = t.kidoku_, .str = pop(Type::String)});
}

void Parser::Add(lex::EndOfScene) {
  // force parser loop to quit
  reader_.Seek(reader_.Size());

  emit_token(Eof{});
}

void Parser::Add(lex::SelBegin) {
  warnings_.emplace_back("selbegin not implemented yet");
}

void Parser::Add(lex::SelEnd) {
  warnings_.emplace_back("selend not implemented yet");
}

void Parser::debug_assert_stack_empty() {
  if (!stack_.Empty()) {
    std::string msg = std::format(
        "[Parser] at {}:{}\n"
        "at line {}, expected stack to be empty. but got:\n"
        "{}",
        scnno_, debug_title_, lineno_, stack_.ToDebugString());
    warnings_.emplace_back(std::move(msg));
    stack_.Clear();
  }
}

}  // namespace libsiglus
