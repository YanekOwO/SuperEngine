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

#include "libsiglus/recompiler.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <exception>
#include <format>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <variant>

#include "libsiglus/element.hpp"
#include "libsiglus/intern_name.hpp"
#include "libsiglus/token.hpp"
#include "libsiglus/types.hpp"
#include "libsiglus/value.hpp"
#include "log/core.hpp"
#include "log/domain_logger.hpp"
#include "utilities/assertx.hpp"
#include "utilities/overload.hpp"
#include "vm/instruction.hpp"
#include "vm/object.hpp"
#include "vm/string.hpp"

namespace sr = serilang;

namespace libsiglus {

// helper to allocate fast locals
expected<AllocationPlan, std::string> resolve_fast_slots(
    const std::vector<Parser::ParsedToken>& tokens) {
  std::string errmsg;
  std::vector<std::pair<int, std::size_t>> reads, writes;  // (tid, token_idx)
  std::vector<std::pair<std::size_t, uint16_t>> subroutine_args{
      std::make_pair(0, 0)};

  struct Visitor {
    std::vector<std::pair<int, std::size_t>>& reads;
    std::vector<std::pair<int, std::size_t>>& writes;
    std::vector<std::pair<std::size_t, uint16_t>>& sub_args;
    std::size_t cur = 0;

    void emit_read(const Variable& v) { reads.emplace_back(v.id, cur); }
    void emit_write(const Variable& v) { writes.emplace_back(v.id, cur); }

    void invoke(const elm::Invoke& inv) {
      for (const Value& arg : inv.arg)
        (*this)(arg);
      for (const auto& arg : inv.named_arg)
        (*this)(arg.second);
    }

    void access_chain(const elm::AccessChain& chain) {
      std::visit(*this, chain.root.var);
      for (const elm::Node& nd : chain.nodes)
        std::visit(*this, nd.var);
    }

    void assignment_target(const elm::AccessChain& chain) {
      if ((std::holds_alternative<elm::Usrprop>(chain.root.var) ||
           std::holds_alternative<elm::Arg>(chain.root.var)) &&
          chain.nodes.empty()) {
        return;
      }

      const bool has_root =
          !std::holds_alternative<std::monostate>(chain.root.var);
      if (has_root)
        std::visit(*this, chain.root.var);

      if (chain.nodes.empty())
        return;

      const std::size_t prefix_count = chain.nodes.size() - 1;
      for (std::size_t i = 0; i < prefix_count; ++i) {
        if (!has_root && i == 0 &&
            std::holds_alternative<elm::Member>(chain.nodes[i].var)) {
          continue;
        }
        std::visit(*this, chain.nodes[i].var);
      }

      std::visit(
          [&](const auto& nd) {
            using T = std::decay_t<decltype(nd)>;
            if constexpr (std::same_as<T, elm::Subscript>) {
              (*this)(nd.idx);
            }
          },
          chain.nodes.back().var);
    }

    void operator()(const token::ElmAlias& t) {
      access_chain(t.chain);
      emit_write(t.dst);
    }
    void operator()(const token::Command& t) {
      access_chain(t.chain);
      emit_write(t.dst);
    }
    void operator()(const token::Name& t) { (*this)(t.str); }
    void operator()(const token::Textout& t) { (*this)(t.str); }
    void operator()(const token::GetProperty& t) {
      access_chain(t.chain);
      emit_write(t.dst);
    }
    void operator()(const token::Operate1& t) {
      if (t.val)
        return;
      (*this)(t.rhs);
      emit_write(t.dst);
    }
    void operator()(const token::Operate2& t) {
      if (t.val)
        return;
      (*this)(t.lhs), (*this)(t.rhs);
      emit_write(t.dst);
    }
    void operator()(const token::Label&) {}
    void operator()(const token::Zlabel&) {}
    void operator()(const token::Goto&) {}
    void operator()(const token::GotoIf& t) { (*this)(t.src); }
    void operator()(const token::Gosub& t) {
      for (const Value& arg : t.args)
        (*this)(arg);
      emit_write(t.dst);
    }
    void operator()(const token::Assign& t) {
      assignment_target(t.dst);
      (*this)(t.src);
    }
    void operator()(const token::Duplicate& t) {
      (*this)(t.src);
      emit_write(t.dst);
    }
    void operator()(const token::Subroutine& t) {
      sub_args.emplace_back(cur, t.args.size());
    }
    void operator()(const token::LocalVar&) { sub_args.back().second++; }
    void operator()(const token::Return& t) {
      for (const Value& val : t.ret_vals)
        (*this)(val);
    }
    void operator()(const token::Eof&) {}

    void operator()(const Value& v) { std::visit(*this, v); }
    void operator()(const Integer&) {}
    void operator()(const String&) {}
    void operator()(const List& t) {
      for (const Value& val : t.vals)
        (*this)(val);
    }
    void operator()(const Variable& t) { emit_read(t); }

    void operator()(const std::monostate&) {}
    void operator()(const elm::Usrcmd& t) { invoke(t.arguments); }
    void operator()(const elm::Usrprop&) {}
    void operator()(const elm::Arg&) {}
    void operator()(const elm::Farcall& t) {
      (*this)(t.scn_name);
      (*this)(t.zlabel);
      for (const Value& arg : t.intargs)
        (*this)(arg);
      for (const Value& arg : t.strargs)
        (*this)(arg);
    }
    void operator()(const elm::Member&) {}
    void operator()(const elm::Call& t) {
      for (const Value& arg : t.args)
        (*this)(arg);
      for (const auto& arg : t.kwargs)
        (*this)(arg.second);
    }
    void operator()(const elm::Subscript& t) { (*this)(t.idx); }
    void operator()(const elm::SetMember& t) { (*this)(t.value); }
  } visitor(reads, writes, subroutine_args);
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    const Parser::ParsedToken& token = tokens[i];
    visitor.cur = i;
    std::visit(visitor, token.token);
  }
  if (!errmsg.empty())
    return unexpected(std::move(errmsg));

  auto fixed_fast_local_count = [&] {
    uint16_t total_fast_locals = 1;  // slot 0 is reserved
    for (const auto& subroutine : subroutine_args) {
      const uint16_t reserved_slots = subroutine.second;
      total_fast_locals =
          std::max<uint16_t>(total_fast_locals, reserved_slots + 1);
    }
    return total_fast_locals;
  };

  int max_tid = -1;
  for (const auto [tid, idx] : writes) {
    if (tid < 0) {
      errmsg += std::format("{}: t{} is invalid\n", idx, tid);
      continue;
    }
    max_tid = std::max(max_tid, tid);
  }

  if (max_tid < 0) {
    for (const auto [tid, idx] : reads)
      errmsg += std::format("{}: t{} is uninitialized\n", idx, tid);
    if (!errmsg.empty())
      return unexpected(std::move(errmsg));
    return AllocationPlan{.slots = {},
                          .total_fast_locals = fixed_fast_local_count()};
  }

  auto segment_start = [&](std::size_t idx) {
    const auto it = std::upper_bound(
        subroutine_args.cbegin(), subroutine_args.cend(),
        std::make_pair(idx, std::numeric_limits<uint16_t>::max()));
    return std::prev(it)->first;
  };

  const int var_cnt = max_tid + 1;
  std::vector<std::size_t> first(var_cnt,
                                 std::numeric_limits<std::size_t>::max()),
      last(var_cnt, 0);
  for (const auto [tid, idx] : writes) {
    if (tid < 0)
      continue;
    first[tid] = std::min(first[tid], idx);
  }
  for (int tid = 0; tid < var_cnt; ++tid) {
    if (first[tid] != std::numeric_limits<std::size_t>::max())
      last[tid] = first[tid];
  }
  for (const auto [tid, idx] : reads) {
    if (tid < 0 || tid >= var_cnt ||
        first[tid] == std::numeric_limits<std::size_t>::max() ||
        first[tid] >= idx || segment_start(first[tid]) != segment_start(idx)) {
      errmsg += std::format("{}: t{} is uninitialized\n", idx, tid);
      continue;
    }
    last[tid] = std::max(last[tid], idx);
  }
  if (!errmsg.empty())
    return unexpected(std::move(errmsg));

  uint16_t total_fast_locals = fixed_fast_local_count();
  std::vector<uint16_t> ret(var_cnt, static_cast<uint16_t>(-1));
  for (auto sub_it = subroutine_args.cbegin(); sub_it != subroutine_args.cend();
       ++sub_it) {
    const std::size_t segment_begin = sub_it->first;
    const std::size_t segment_end = std::next(sub_it) == subroutine_args.cend()
                                        ? tokens.size()
                                        : std::next(sub_it)->first;
    uint16_t next_slot = sub_it->second + 1;
    total_fast_locals = std::max(total_fast_locals, next_slot);
    if (segment_begin >= segment_end)
      continue;

    std::vector<std::pair<std::size_t, int>> in, out;
    for (int tid = 0; tid < var_cnt; ++tid) {
      if (first[tid] < segment_begin || first[tid] >= segment_end)
        continue;
      in.emplace_back(first[tid], tid);
      out.emplace_back(last[tid] + 1, tid);
    }
    std::sort(in.begin(), in.end());
    std::sort(out.begin(), out.end());

    std::deque<uint16_t> reusable_slots;
    auto in_it = in.cbegin(), out_it = out.cbegin();
    for (std::size_t i = segment_begin; i < segment_end; ++i) {
      while (out_it != out.cend() && out_it->first <= i) {
        const uint16_t release = ret[out_it->second];
        if (release != static_cast<uint16_t>(-1))
          reusable_slots.emplace_back(release);
        ++out_it;
      }
      while (in_it != in.cend() && in_it->first <= i) {
        uint16_t slot;
        if (!reusable_slots.empty()) {
          slot = reusable_slots.front();
          reusable_slots.pop_front();
        } else {
          slot = next_slot++;
          total_fast_locals = std::max(total_fast_locals, next_slot);
        }
        ret[in_it->second] = slot;
        ++in_it;
      }
    }
  }

  return AllocationPlan{.slots = std::move(ret),
                        .total_fast_locals = total_fast_locals};
}

// CompileError
std::string CompileError::ToString() const {
  std::string ret;
  if (token)
    ret = ::libsiglus::ToString(*token) + '\n';
  ret += message;
  return ret;
}

namespace {

uint16_t fast_local_slot(int id) {
  if (id < 0)
    throw std::runtime_error("Codegen: invalid fast-local id " +
                             std::to_string(id));
  if (id > std::numeric_limits<uint16_t>::max())
    throw std::runtime_error("Codegen: too many fast locals (" +
                             std::to_string(id) + ')');
  return static_cast<uint16_t>(id);
}

}  // namespace

DomainLogger logger("Recompiler");

Recompiler::Recompiler(std::shared_ptr<serilang::GarbageCollector> gc)
    : gc_(gc) {
  cur_chunk_ = gc_->Allocate<sr::Code>();
  module_ = gc_->Allocate<sr::Module>("<siglus script>");
  cur_chunk_->const_pool.emplace_back(cur_chunk_);

  initial_jump_site_ = code_size();
  emit(sr::Jump{0});
  main_entry_ = code_size();
}
Recompiler::~Recompiler() = default;

void Recompiler::SetSceneProperties(int scene_id,
                                    std::vector<Property> properties) {
  scene_id_ = scene_id;
  scene_properties_ = std::move(properties);
}

void Recompiler::Compile(const std::vector<Parser::ParsedToken>& tokens) {
  if (auto plan = resolve_fast_slots(tokens)) {
    cur_chunk_->fast_locals.resize(plan->total_fast_locals);
    for (uint16_t i = 0; i < plan->total_fast_locals; ++i) {
      cur_chunk_->fast_locals[i] =
          i == 0 ? std::string("fn") : 't' + std::to_string(i);
    }
    tvar_slots_ = std::move(plan->slots);
  } else
    throw std::runtime_error(plan.error());

  for (const Parser::ParsedToken& tok : tokens) {
    Gen(tok.token, tok.line);
  }

  Finish();
}

void Recompiler::Gen(token::Token_t tok, int lineno) {
  try {
    if (is_finalized_)
      throw std::runtime_error("cannot emit token after EOF");

    if (is_debug_) {
      struct DebugAnnotationVisitor {
        Recompiler& compiler;
        std::uint32_t terms = 0;
        bool should_annotate = true;
        DebugAnnotationVisitor(Recompiler& r, int scnno, int lineno)
            : compiler(r) {
          str(std::format("[{}:{}] ", scnno, lineno));
        }
        void str(std::string s) {
          compiler.emit_const(std::move(s));
          ++terms;
        }
        void variable(int id) {
          compiler.emit_load_global("__builtin_dbgvalue");
          compiler.emit(sr::LoadFast{.slot = compiler.variable_fast_slot(id)});
          compiler.emit(sr::Call{.argcnt = 1, .kwargcnt = 0});
          ++terms;
        }
        void curcall(int id) {
          compiler.emit_load_global("__builtin_dbgvalue");
          compiler.emit(sr::LoadFast{.slot = compiler.curcall_fast_slot(id)});
          compiler.emit(sr::Call{.argcnt = 1, .kwargcnt = 0});
          ++terms;
        }
        void comma_if_needed(bool& needs_comma) {
          if (needs_comma)
            str(",");
          needs_comma = true;
        }
        void values(const std::vector<Value>& vals) {
          for (std::size_t i = 0; i < vals.size(); ++i) {
            if (i > 0)
              str(",");
            (*this)(vals[i]);
          }
        }
        void named_values(const std::vector<std::pair<int, Value>>& vals,
                          bool& needs_comma,
                          std::string_view prefix = "") {
          for (const auto& [key, val] : vals) {
            comma_if_needed(needs_comma);
            str(std::format("{}{}=", prefix, key));
            (*this)(val);
          }
        }
        void assignment_prefix(Type type, const Variable& dst) {
          str(std::format("{} {} = ", ToString(type), dst.ToDebugString()));
        }
        void invoke(const elm::Invoke& inv,
                    bool show_overload,
                    bool show_rettype) {
          if (show_overload)
            str("[" + std::to_string(inv.overload_id) + "]");

          str("(");
          bool needs_comma = false;
          for (const auto& arg : inv.arg) {
            comma_if_needed(needs_comma);
            (*this)(arg);
          }
          named_values(inv.named_arg, needs_comma, "_");
          str(")");

          if (show_rettype)
            str("->" + ToString(inv.return_type));
        }
        void access_chain(const elm::AccessChain& chain) {
          const bool elide_first_member_dot =
              std::holds_alternative<std::monostate>(chain.root.var) &&
              !chain.nodes.empty() &&
              std::holds_alternative<elm::Member>(chain.nodes.front().var);

          (*this)(chain.root);
          for (std::size_t i = 0; i < chain.nodes.size(); ++i) {
            if (elide_first_member_dot && i == 0) {
              const auto& member = std::get<elm::Member>(chain.nodes[i].var);
              str(std::string(member.name));
            } else {
              (*this)(chain.nodes[i]);
            }
          }
        }
        void operator()(const token::ElmAlias& t) {
          str(std::format("alias.{} {} = ", ToString(Typeof(t.dst)),
                          t.dst.ToDebugString()));
          access_chain(t.chain);
        }
        void operator()(const token::Command& t) {
          assignment_prefix(Typeof(t.dst), t.dst);
          access_chain(t.chain);
        }
        void operator()(const token::Name& t) {
          str("Name(");
          (*this)(t.str);
          str(")");
        }
        void operator()(const token::Textout& t) {
          str(std::format("Textout@{} (", t.kidoku));
          (*this)(t.str);
          str(")");
        }
        void operator()(const token::GetProperty& t) {
          assignment_prefix(Typeof(t.dst), t.dst);
          access_chain(t.chain);
        }
        void operator()(const token::Operate1& t) {
          assignment_prefix(Typeof(t.dst), t.dst);
          str(ToString(t.op) + " ");
          (*this)(t.rhs);
          if (t.val) {
            str(" ;");
            (*this)(*t.val);
          }
        }
        void operator()(const token::Operate2& t) {
          assignment_prefix(Typeof(t.dst), t.dst);
          (*this)(t.lhs);
          str(" " + ToString(t.op) + " ");
          (*this)(t.rhs);
          if (t.val) {
            str(" ;");
            (*this)(*t.val);
          }
        }
        void operator()(const token::Label& t) { str(t.ToDebugString()); }
        void operator()(const token::Zlabel& t) { str(t.ToDebugString()); }
        void operator()(const token::Goto& t) { str(t.ToDebugString()); }
        void operator()(const token::GotoIf& t) {
          str(t.cond ? "if(" : "ifnot(");
          (*this)(t.src);
          str(std::format(") goto .L{}", t.label));
        }
        void operator()(const token::Gosub& t) {
          assignment_prefix(Typeof(t.dst), t.dst);
          str(std::format("gosub@.L{}(", t.entry_id));
          values(t.args);
          str(")");
        }
        void operator()(const token::Assign& t) {
          should_annotate = false;  // lhs is not bounded yet
          access_chain(t.dst);
          should_annotate = true;
          str(" = ");
          (*this)(t.src);
        }
        void operator()(const token::Duplicate& t) {
          assignment_prefix(Typeof(t.dst), t.dst);
          (*this)(t.src);
        }
        void operator()(const token::Subroutine& t) { str(t.ToDebugString()); }
        void operator()(const token::LocalVar& t) { str(t.ToDebugString()); }
        void operator()(const token::Return& t) {
          str("ret (");
          values(t.ret_vals);
          str(")");
        }
        void operator()(const token::Eof& t) { str(t.ToDebugString()); }

        void operator()(const elm::Root& r) { std::visit(*this, r.var); }
        void operator()(const std::monostate&) {}
        void operator()(const elm::Usrcmd& t) {
          str(std::format("@{}.{}:{}", t.scene, t.entry, t.name));
          invoke(t.arguments, false, false);
        }
        void operator()(const elm::Usrprop& t) {
          str(std::format("@{}.{}:{}", t.scene, t.idx, t.name));
        }
        void operator()(const elm::Arg& t) {
          str("arg_" + std::to_string(t.id));
          if (should_annotate) {
            str(":");
            curcall(t.id);
          }
        }
        void operator()(const elm::Farcall& t) {
          str("farcall@[");
          (*this)(t.scn_name);
          str("].z[");
          (*this)(t.zlabel);
          str("](");
          values(t.intargs);
          str(")(");
          values(t.strargs);
          str(")");
        }

        void operator()(const elm::Node& n) { std::visit(*this, n.var); }
        void operator()(const elm::Member& t) {
          str("." + std::string(t.name));
        }
        void operator()(const elm::SetMember& t) {
          str("." + std::string(t.name) + " = ");
          (*this)(t.value);
        }
        void operator()(const elm::Subscript& t) {
          str("[");
          (*this)(t.idx);
          str("]");
        }
        void operator()(const elm::Call& t) {
          if (t.overload_id)
            str("[" + std::to_string(*t.overload_id) + "]");
          str("(");
          bool needs_comma = false;
          for (const auto& arg : t.args) {
            comma_if_needed(needs_comma);
            (*this)(arg);
          }
          named_values(t.kwargs, needs_comma);
          str(")");
        }

        void operator()(const Value& v) { std::visit(*this, v); }
        void operator()(const Integer& t) { str(t.ToDebugString()); }
        void operator()(const String& t) { str(t.ToDebugString()); }
        void operator()(const List& t) {
          str("[");
          for (std::size_t i = 0; i < t.vals.size(); ++i) {
            if (i > 0)
              str(",");
            (*this)(t.vals[i]);
          }
          str("]");
        }
        void operator()(const Variable& t) {
          str(t.ToDebugString());
          if (should_annotate) {
            str(":");
            variable(t.id);  // v123:123
          }
        }
      };
      emit_load_global("__builtin_dbgprint");
      DebugAnnotationVisitor visitor(*this, scene_id_.value_or(-1), lineno);
      std::visit(visitor, tok);
      emit(sr::Call{.argcnt = visitor.terms, .kwargcnt = 0});
      emit(sr::Pop{});

      emit_const(ToString(tok));
      emit(sr::DebugValue{});
    }
    std::visit([this](const auto& stmt) { emit_tok(stmt); }, std::move(tok));
  } catch (std::exception& e) {
    AddError(e.what(), tok);
  }
}

void Recompiler::Finish() {
  if (is_finalized_)
    return;

  // Payload functions should not fall through into the bootstrap block.
  emit_const_nil();
  emit(sr::Return{});

  const std::size_t bootstrap_entry = code_size();
  patch(initial_jump_site_, bootstrap_entry);
  emit_store_function_global("%%script", main_entry_, 0);

  for (const UserCommandMetadata& metadata : user_commands_) {
    const std::string entry_name = GetUsercmdId(metadata.source_entry);
    emit_store_function_global(entry_name, metadata.bytecode_entry,
                               metadata.args.size());
  }

  for (const auto& record : zlabels_) {
    const std::string entry_name = GetZlabelId(record.id);
    emit_store_function_global(entry_name, record.bytecode_entry, 0);
  }

  if (scene_id_.has_value()) {
    emit_scene_property_table();
    emit_store_global("__usrprop");  // __usrprop -> list
  }

  emit_const_nil();
  emit(sr::Return{});

  is_finalized_ = true;
}

void Recompiler::emit_current_chunk() { emit(sr::Push{0}); }

uint32_t Recompiler::constant(sr::Value v) {
  if (!cur_chunk_)
    throw std::logic_error("Recompiler chunk is not set");

  const auto next_slot = static_cast<uint32_t>(cur_chunk_->const_pool.size());
  auto [it, ok] = const_pool_.emplace(std::move(v), next_slot);
  if (ok)
    cur_chunk_->const_pool.emplace_back(it->first);
  return it->second;
}
uint32_t Recompiler::emit_const_nil() {
  uint32_t slot = constant(sr::nil);
  emit(sr::Push{slot});
  return slot;
}
uint32_t Recompiler::emit_const(int v) {
  uint32_t slot = constant(sr::Value(v));
  emit(sr::Push{slot});
  return slot;
}
uint32_t Recompiler::emit_const(std::string v) {
  uint32_t slot = intern_name(std::move(v));
  emit(sr::Push{slot});
  return slot;
}
uint32_t Recompiler::intern_name(std::string v) {
  sr::String* str = gc_->Allocate<sr::String>(std::move(v));
  uint32_t slot = constant(sr::Value(str));
  return slot;
}

uint16_t Recompiler::variable_fast_slot(int id) {
  if (id < 0 || id >= tvar_slots_.size()) {
    throw std::runtime_error("Codegen: invalid temporary id " +
                             std::to_string(id));
  }
  return tvar_slots_[id];
}

uint16_t Recompiler::curcall_fast_slot(int id) {
  if (id < 0)
    throw std::runtime_error("Codegen: invalid curcall id " +
                             std::to_string(id));
  return fast_local_slot(id + 1);
}

void Recompiler::emit_store_global(std::string id) {
  emit(sr::StoreGlobal{intern_name(std::move(id))});
}
void Recompiler::emit_load_global(std::string id) {
  emit(sr::LoadGlobal{intern_name(std::move(id))});
}

void Recompiler::emit_make_function(std::size_t entry, std::size_t nargs) {
  emit_current_chunk();
  for (std::size_t i = 0; i < nargs; ++i)
    emit_const("arg" + std::to_string(i));

  emit(sr::MakeFunction{.entry = static_cast<uint32_t>(entry),
                        .nparam = static_cast<uint32_t>(nargs),
                        .ndefault = 0,
                        .has_vararg = false,
                        .has_kwarg = false});
}

void Recompiler::emit_store_function_global(std::string id,
                                            std::size_t entry,
                                            std::size_t nargs) {
  emit_make_function(entry, nargs);
  emit_store_global(std::move(id));
}

void Recompiler::emit_scene_property_table() {
  for (const Property& property : scene_properties_)
    emit_init_value(property.form, property.size);
  emit(sr::MakeList{.nelms = static_cast<uint32_t>(scene_properties_.size())});
}

void Recompiler::emit_load_proplist(int scene) {
  if (scene == -1) {
    emit_load_global("__globalprop");
    return;
  }

  if (scene == scene_id_) {
    emit_load_global("__usrprop");
    return;
  }

  emit_load_global("__builtin_load_scn");
  emit_const(scene);
  emit(sr::Call{.argcnt = 1, .kwargcnt = 0});
  emit(sr::GetField{intern_name("__usrprop")});
}

void Recompiler::emit_init_value(Type type, int size) {
  switch (type) {
    case Type::Int:
      emit_const(0);
      break;
    case Type::String:
      emit_const("");
      break;
    case Type::IntList:
      emit_load_global("make_intlist");
      emit_const(size);
      emit(sr::Call{.argcnt = 1, .kwargcnt = 0});
      break;
    case Type::StrList:
      emit_load_global("make_strlist");
      emit_const(size);
      emit(sr::Call{.argcnt = 1, .kwargcnt = 0});
      break;
    default:
      emit_const_nil();
      break;
  }
}

void Recompiler::add_patch_site(int lid, std::size_t site) {
  if (lid >= patch_sites_.size()) {
    patch_sites_.resize(lid + 1), label_offsets_.resize(lid + 1);
  }

  if (label_offsets_[lid].has_value())
    patch(site, *label_offsets_[lid]);
  else
    patch_sites_[lid].emplace_back(site);
}

template <typename offset_t>
static inline constexpr auto rel(offset_t from, offset_t to) -> offset_t {
  return to - from;
}
void Recompiler::patch(std::size_t site,
                       std::size_t target) {  // Patch jumps
  switch (static_cast<sr::OpCode>((*cur_chunk_)[site])) {
    case sr::OpCode::Jump:
    case sr::OpCode::JumpIfFalse:
    case sr::OpCode::JumpIfTrue:
    case sr::OpCode::TryBegin: {
      const auto offset =
          rel<int32_t>(site + sizeof(std::byte) + sizeof(int32_t), target);
      cur_chunk_->Write(site + 1, offset);
    } break;

    case sr::OpCode::MakeFunction:
      cur_chunk_->Write(site + 1, static_cast<uint32_t>(target));
      break;

    default:
      throw std::runtime_error(
          "Codegen: invalid patch site (type" +
          std::to_string(static_cast<uint8_t>((*cur_chunk_)[site])) + ')');
  }
}

// -----------------------------------------------------------------------
// token codegen
void Recompiler::emit_val(const Value& v) {
  std::visit(
      overload([&](Integer const& v) { emit_const(v.val_); },
               [&](String const& v) { emit_const(v.val_); },
               [&](List const& v) {
                 for (const auto& item : v.vals)
                   emit_val(item);
                 emit(sr::MakeList{.nelms = v.vals.size()});
               },
               [&](Variable const& v) {
                 emit(sr::LoadFast{.slot = variable_fast_slot(v.id)});
               },
               [&](const auto&) {
                 throw std::runtime_error("Cannot emit value " + ToString(v));
               }),
      v);
}

void Recompiler::emit_tok(const token::ElmAlias& tk) {
  emit_elm(tk.chain);
  emit(sr::StoreFast{.slot = variable_fast_slot(tk.dst.id)});
}
void Recompiler::emit_tok(const token::Command& tk) {
  emit_elm(tk.chain);
  emit(sr::StoreFast{.slot = variable_fast_slot(tk.dst.id)});
}
void Recompiler::emit_tok(const token::Name& tk) {
  emit_load_global("__builtin_name");
  emit_val(tk.str);
  emit(sr::Call{.argcnt = 1});
}
void Recompiler::emit_tok(const token::Textout& tk) {
  emit_load_global("__builtin_textout");
  emit_const(scene_id_.value_or(-1)), emit_const(tk.kidoku), emit_val(tk.str);
  emit(sr::Call{.argcnt = 3});
  emit(sr::Await{});
}
void Recompiler::emit_tok(const token::GetProperty& tk) {
  emit_elm(tk.chain);
  emit(sr::StoreFast{.slot = variable_fast_slot(tk.dst.id)});
  // (val) -> ()
}
void Recompiler::emit_tok(const token::Operate1& tk) {
  if (tk.val) {
    emit_val(*tk.val);
    return;
  }
  emit_val(tk.rhs);
  emit(sr::UnaryOp{LowerUnaryOperator(tk.op)});
  emit(sr::StoreFast{.slot = variable_fast_slot(tk.dst.id)});
}
void Recompiler::emit_tok(const token::Operate2& tk) {
  if (tk.val) {
    emit_val(*tk.val);
    return;
  }

  if ((tk.op == OperatorCode::Equal || tk.op == OperatorCode::Ne) &&
      Typeof(tk.lhs) == Type::String && Typeof(tk.rhs) == Type::String) {
    emit_load_global("__builtin_streq");
    emit_val(tk.lhs), emit_val(tk.rhs);
    emit(sr::Call{.argcnt = 2});
    if (tk.op == OperatorCode::Ne)
      emit(sr::UnaryOp{Op::Tilde});
  } else {
    emit_val(tk.lhs), emit_val(tk.rhs);
    emit(sr::BinaryOp{LowerBinaryOperator(tk.op)});
  }

  emit(sr::StoreFast{.slot = variable_fast_slot(tk.dst.id)});
}
void Recompiler::emit_tok(const token::Label& tk) {
  const int lid = tk.id;
  if (label_offsets_.size() <= lid) {
    label_offsets_.resize(lid + 1), patch_sites_.resize(lid + 1);
  }
  const std::size_t target = code_size();

  label_offsets_[lid] = target;
  for (auto site : patch_sites_[lid])
    patch(site, target);
  patch_sites_[lid].clear();
}
void Recompiler::emit_tok(const token::Zlabel& tk) {
  const std::size_t loc = code_size();
  auto [it, ok] = zlabel_entries_.emplace(tk.id, loc);
  if (!ok) {
    throw std::runtime_error(
        std::format("redefinition of zlabel entry {}", tk.id));
  }

  zlabels_.push_back(ZlabelRecord{.id = tk.id, .bytecode_entry = loc});
}
void Recompiler::emit_tok(const token::Goto& tk) {
  auto site = code_size();
  emit(sr::Jump{0});
  add_patch_site(tk.label, site);
}
void Recompiler::emit_tok(const token::GotoIf& tk) {
  emit_val(tk.src);
  auto site = code_size();
  if (tk.cond)
    emit(sr::JumpIfTrue{0});
  else
    emit(sr::JumpIfFalse{0});
  add_patch_site(tk.label, site);
}
void Recompiler::emit_tok(const token::Gosub& tk) {
  emit_current_chunk();
  for (int i = 0; i < tk.args.size(); ++i)
    emit_const("arg" + std::to_string(i));

  auto site = code_size();
  emit(sr::MakeFunction{.entry = 0,
                        .nparam = tk.args.size(),
                        .ndefault = 0,
                        .has_vararg = false,
                        .has_kwarg = false});
  add_patch_site(tk.entry_id, site);

  for (auto const& arg : tk.args)
    emit_val(arg);
  emit(sr::Call{.argcnt = tk.args.size(), .kwargcnt = 0});
  emit(sr::StoreFast{.slot = variable_fast_slot(tk.dst.id)});
}
void Recompiler::emit_tok(const token::Assign& tk) {
  emit_elm(tk.dst, &tk.src);
}
void Recompiler::emit_tok(const token::Duplicate& tk) {
  emit_val(tk.src);
  emit(sr::StoreFast{.slot = variable_fast_slot(tk.dst.id)});
}
void Recompiler::emit_tok(const token::Subroutine& tk) {
  const std::size_t loc = code_size();
  auto [it, ok] = subroutine_entries_.emplace(tk.source_entry, loc);
  if (!ok) {
    throw std::runtime_error(
        std::format("redefinition of user command entry {}", tk.source_entry));
  }

  user_commands_.emplace_back(tk.name, tk.source_entry, loc, tk.args);
}
void Recompiler::emit_tok(const token::LocalVar& tk) {
  emit_init_value(tk.type, tk.size);
  emit(sr::StoreFast{.slot = curcall_fast_slot(tk.id)});
}
void Recompiler::emit_tok(const token::Return& tk) {
  if (tk.ret_vals.size() == 0)
    emit_const_nil();
  else if (tk.ret_vals.size() == 1)
    emit_val(tk.ret_vals.front());
  else {
    for (const auto& it : tk.ret_vals)
      emit_val(it);
    emit(sr::MakeList{.nelms = tk.ret_vals.size()});
  }
  emit(sr::Return{});
}
void Recompiler::emit_tok(const token::Eof& tk) { Finish(); }

// element codegen
void Recompiler::emit_elm(const elm::AccessChain& e, const Value* assign) {
  if (assign && !e.nodes.empty() &&
      !std::holds_alternative<elm::Member>(e.nodes.back().var) &&
      !std::holds_alternative<elm::Subscript>(e.nodes.back().var)) {
    throw std::runtime_error("cannot assign to this element");
  }
  if (auto* prop = std::get_if<elm::Usrprop>(&e.root.var);
      prop && assign && e.nodes.empty()) {
    emit_load_proplist(prop->scene);  // (prop[])

    emit_const(prop->idx), emit_val(*assign);
    emit(sr::SetItem{});  // (prop[], idx, val) -> ()
    return;
  }
  if (auto* arg = std::get_if<elm::Arg>(&e.root.var);
      arg && assign && e.nodes.empty()) {
    emit_val(*assign);
    emit(sr::StoreFast{.slot = curcall_fast_slot(arg->id)});
    return;
  }

  bool is_first = true;
  if (!std::holds_alternative<std::monostate>(e.root.var)) {
    is_first = false;
    std::visit([&](const auto& rt) { emit_elm_root(rt); }, e.root.var);
  }

  auto fail = [] { throw std::runtime_error("cannot assign to this element"); };
  if (e.nodes.empty()) {
    if (assign)
      fail();
    return;
  }

  size_t n = e.nodes.size();
  if (assign)
    --n;
  for (size_t i = 0; i < n; ++i) {
    if (i == 0 && is_first) {
      if (auto* sym = std::get_if<elm::Member>(&e.nodes.front().var)) {
        emit_load_global(std::string(sym->name));
        continue;
      } else {
        throw std::runtime_error("[Recompiler] cannot compile element: " +
                                 e.ToDebugString());
      }
    }
    std::visit([&](const auto& nd) { emit_elm_node(nd); }, e.nodes[i].var);
  }

  if (assign) {
    if (auto* mem = std::get_if<elm::Member>(&e.nodes.back().var)) {
      emit_val(*assign);
      auto name_slot = intern_name(std::string(mem->name));
      emit(sr::SetField{name_slot});
    } else if (auto* itm = std::get_if<elm::Subscript>(&e.nodes.back().var)) {
      emit_val(itm->idx), emit_val(*assign);
      emit(sr::SetItem{});
    } else
      fail();
  }
}

void Recompiler::emit_elm_root(const std::monostate& r) {
  ASSERTX_TRUE(false);  // unreachable
}
void Recompiler::emit_elm_root(const elm::Usrcmd& r) {
  const std::string cmd_name = GetUsercmdName(std::string(r.name));

  emit_load_global("__builtin_usrcmd");
  emit_const(r.scene), emit_const(r.entry), emit_const(std::string(r.name));
  emit(sr::Call{.argcnt = 3, .kwargcnt = 0});
  // (usrcmd)

  const auto& arg = r.arguments;
  if (arg.overload_id != 0) {
    logger(Severity::Error)
        << "Usrcmd " << cmd_name << " has overload: " << arg.overload_id;
  }

  for (auto const& it : arg.arg)
    emit_val(it);

  if (!arg.named_arg.empty()) {
    emit_const("unsupported named/tagged user-command arguments for " +
               cmd_name);
    emit(sr::Throw{});
    return;
  }

  emit(sr::Call{.argcnt = arg.arg.size(), .kwargcnt = 0});
  // (usrcmd, args...) -> (ret)
}
void Recompiler::emit_elm_root(const elm::Usrprop& r) {
  emit_load_proplist(r.scene);
  // (prop[])
  emit_const(r.idx);
  emit(sr::GetItem{});  // (prop[], idx) -> (item)
}
void Recompiler::emit_elm_root(const elm::Arg& r) {
  // fast: (fn, arg_0, arg_1, ...)
  emit(sr::LoadFast{.slot = curcall_fast_slot(r.id)});
  // (arg_i) or (var_i)
}
void Recompiler::emit_elm_root(const elm::Farcall& r) {
  emit_load_global("__builtin_farcall");
  emit_val(r.scn_name), emit_val(r.zlabel);
  emit(sr::Call{.argcnt = 2, .kwargcnt = 0});
  // (farcall, scn, z) -> (fn)

  emit_load_global("__builtin_push_frame");
  for (auto const& it : r.intargs)
    emit_val(it);
  emit(sr::MakeList{.nelms = r.intargs.size()});
  for (auto const& it : r.strargs)
    emit_val(it);
  emit(sr::MakeList{.nelms = r.strargs.size()});
  emit(sr::Call{.argcnt = 2, .kwargcnt = 0});
  // (fn, push_frame, L[], K[]) -> (fn, nil)
  emit(sr::Pop{});
  // (fn, nil) -> (fn)

  emit(sr::Call{.argcnt = 0, .kwargcnt = 0});
  // -> (ret)

  emit_load_global("__builtin_pop_frame");
  emit(sr::Call{.argcnt = 0, .kwargcnt = 0});
  emit(sr::Pop{});
  // (ret,nil) -> (ret)
}

void Recompiler::emit_elm_node(const elm::Member& nd) {
  // (primary)
  uint32_t slot = intern_name(std::string(nd.name));
  emit(sr::GetField{slot});
  // (member)
}
void Recompiler::emit_elm_node(const elm::SetMember& nd) {
  emit_val(nd.value);
  emit(sr::SetField{intern_name(std::string(nd.name))});
  // (inst, val) -> ()
  emit_const_nil();
  // (nil)
}
void Recompiler::emit_elm_node(const elm::Call& nd) {
  // (fn)
  const unsigned context_count = nd.context ? 2 : 0;
  if (nd.context) {
    emit_const(scene_id_.value_or(-1));
    emit_const(nd.kidoku.value_or(-1));
  }
  if (nd.is_simple) {
    if (!nd.kwargs.empty()) {
      std::string msg = std::format(
          "Simple Siglus callable has {} tagged/keyword argument(s); ignoring "
          "kwargs",
          nd.kwargs.size());
      AddError(msg);
      logger(Severity::Warn) << msg;
    }

    // no overload, no kwargs
    for (auto const& arg : nd.args)
      emit_val(arg);
    // (fn, args...)
    emit(sr::Call(
        {.argcnt = static_cast<uint32_t>(nd.args.size()) + context_count,
         .kwargcnt = 0}));
    // -> (ret)
  } else {
    if (nd.overload_id)
      emit_const(*nd.overload_id);
    else
      emit_const_nil();
    // (fn, ol)

    for (auto const& arg : nd.args)
      emit_val(arg);
    emit(sr::MakeList{.nelms = nd.args.size()});
    // (fn, ol, args)

    for (auto const& [k, arg] : nd.kwargs) {
      emit_const("_" + std::to_string(k));
      emit_val(arg);
    }
    emit(sr::MakeDict{.nelms = nd.kwargs.size()});
    // (fn, ol, args, kwargs)

    emit(sr::Call{.argcnt = 3 + context_count, .kwargcnt = 0});
    // -> (ret)
  }

  if (nd.await_result)
    emit(sr::Await{});
}
void Recompiler::emit_elm_node(const elm::Subscript& nd) {
  // (primary)
  emit_val(nd.idx);     // (primary, idx)
  emit(sr::GetItem{});  // (item)
}

}  // namespace libsiglus
