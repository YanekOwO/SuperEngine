// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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
//
// -----------------------------------------------------------------------

#include "m6/codegen.hpp"

#include "utilities/mpl.hpp"
#include "vm/object.hpp"
#include "vm/string.hpp"
#include "vm/value.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace m6 {

namespace sr = serilang;

static constexpr std::string_view kReplPrintName = "__repl_print__";

namespace {

std::optional<uint16_t> ToFastLocalSlot(std::size_t slot) {
  if (slot > std::numeric_limits<uint16_t>::max())
    return std::nullopt;
  return static_cast<uint16_t>(slot);
}

}  // namespace

// Constructor / Destructor
CodeGenerator::CodeGenerator(std::shared_ptr<serilang::GarbageCollector> gc,
                             bool repl)
    : gc_(gc),
      repl_mode_(repl),
      mode_(CompileMode::Global),
      scope_depth_(0),
      chunk_(gc->Allocate<sr::Code>()),
      locals_(),
      local_depth_(0),
      errors_() {
  if (repl_mode_) {  // emit an implicit handler for uncaught exceptions
    auto jmp = code_size();
    emit(sr::Jump{0});

    auto catch_start = code_size();
    emit(sr::LoadGlobal{intern_name(kReplPrintName)});
    emit(sr::Swap{});
    emit(sr::Call{1, 0});
    emit(sr::Return{});

    // patch the unconditional jump to skip over the catch block
    patch(jmp, code_size());

    auto try_begin = code_size();
    emit(sr::TryBegin{0});
    patch(try_begin, catch_start);
    // ignore tryend
  }
}

CodeGenerator::~CodeGenerator() = default;

// Public status API
bool CodeGenerator::Ok() const { return errors_.empty(); }

std::span<const Error> CodeGenerator::GetErrors() const { return errors_; }

void CodeGenerator::ClearErrors() { errors_.clear(); }

// Code accessors
sr::Code* CodeGenerator::GetChunk() const { return chunk_; }
void CodeGenerator::SetChunk(sr::Code* c) { chunk_ = c; }

// Entry point
void CodeGenerator::Gen(std::shared_ptr<AST> ast) { emit_stmt(std::move(ast)); }

// Error recording
void CodeGenerator::AddError(std::string msg,
                             std::optional<SourceLocation> loc) {
  errors_.emplace_back(std::move(msg), std::move(loc));
}

// Constant-pool helpers
uint32_t CodeGenerator::constant(std::string_view str) {
  sr::String* s = gc_->Allocate<sr::String>(std::string(str));
  return constant(Value(s));
}
uint32_t CodeGenerator::constant(Value v) {
  chunk_->const_pool.emplace_back(std::move(v));
  return static_cast<uint32_t>(chunk_->const_pool.size() - 1);
}

uint32_t CodeGenerator::intern_name(std::string_view s) {
  for (uint32_t i = 0; i < chunk_->const_pool.size(); ++i) {
    if (auto p = chunk_->const_pool[i].Get_if<sr::String>();
        p && p->str_ == s) {
      return i;
    }
  }
  return constant(s);
}

// emit helpers
void CodeGenerator::emit_const(Value v) {
  const auto slot = constant(std::move(v));
  emit(sr::Push{slot});
}

void CodeGenerator::emit_const(std::string_view s) {
  const auto slot = intern_name(s);
  emit(sr::Push{slot});
}

std::size_t CodeGenerator::code_size() const { return chunk_->code.size(); }

void CodeGenerator::emit_store_var(const std::string& id) {
  if (mode_ == CompileMode::Global || get_scope(id) == SCOPE::GLOBAL) {
    emit(sr::StoreGlobal{intern_name(id)});
    return;
  }

  std::optional<std::size_t> slot = resolve_local(id);
  if (!slot)
    slot = add_local(id);
  if (!slot)
    return;
  const auto fast_slot = ToFastLocalSlot(*slot);
  if (!fast_slot) {
    AddError("Codegen: too many fast locals (" + std::to_string(*slot) + ')');
    return;
  }

  emit(sr::StoreFast{*fast_slot});
}

void CodeGenerator::emit_load_var(std::string_view id) {
  if (get_scope(id) == SCOPE::GLOBAL) {
    emit(sr::LoadGlobal{intern_name(id)});
    return;
  }

  if (auto slot = resolve_local(id)) {
    const auto fast_slot = ToFastLocalSlot(*slot);
    if (!fast_slot) {
      AddError("Codegen: too many fast locals (" + std::to_string(*slot) + ')');
      return;
    }
    emit(sr::LoadFast{*fast_slot});
  } else {
    emit(sr::LoadGlobal{intern_name(id)});
  }
}

// Identifier resolution
std::optional<std::size_t> CodeGenerator::resolve_local(
    std::string_view id) const {
  if (mode_ == CompileMode::Global)
    return std::nullopt;
  auto it = locals_.find(id);
  if (it != locals_.cend())
    return it->second;
  return std::nullopt;
}

std::optional<std::size_t> CodeGenerator::add_local(const std::string& id) {
  if (!ToFastLocalSlot(local_depth_).has_value()) {
    AddError("Codegen: too many fast locals (" + std::to_string(local_depth_) +
             ')');
    return std::nullopt;
  }

  locals_[id] = local_depth_;
  chunk_->fast_locals.emplace_back(id);
  return local_depth_++;
}

CodeGenerator::SCOPE CodeGenerator::get_scope(std::string_view id) {
  auto it = scope_heuristic_.find(id);
  if (it == scope_heuristic_.cend())
    return SCOPE::NONE;
  else
    return it->second;
}

// Expression codegen
void CodeGenerator::emit_expr(std::shared_ptr<ExprAST> n) {
  if (!n)
    emit_expr_node(NilLiteral());
  else
    n->Apply([&](auto&& x) { emit_expr_node(x); });
}

void CodeGenerator::emit_expr_node(const NilLiteral&) {
  emit_const(std::monostate());
}

void CodeGenerator::emit_expr_node(const TrueLiteral&) { emit_const(true); }

void CodeGenerator::emit_expr_node(const FalseLiteral&) { emit_const(false); }

void CodeGenerator::emit_expr_node(const IntLiteral& n) { emit_const(n.value); }

void CodeGenerator::emit_expr_node(const StrLiteral& n) { emit_const(n.value); }

void CodeGenerator::emit_expr_node(const ListLiteral& n) {
  for (const auto& it : n.elements)
    emit_expr(it);
  emit(sr::MakeList{static_cast<uint32_t>(n.elements.size())});
}

void CodeGenerator::emit_expr_node(const DictLiteral& n) {
  for (const auto& [key, val] : n.elements) {
    emit_expr(key);
    emit_expr(val);
  }
  emit(sr::MakeDict{static_cast<uint32_t>(n.elements.size())});
}

void CodeGenerator::emit_expr_node(const Identifier& n) {
  emit_load_var(n.value);
}

void CodeGenerator::emit_expr_node(const UnaryExpr& u) {
  emit_expr(u.sub);
  emit(sr::UnaryOp{u.op});
}

void CodeGenerator::emit_expr_node(const BinaryExpr& b) {
  emit_expr(b.lhs);
  emit_expr(b.rhs);
  emit(sr::BinaryOp{b.op});
}

void CodeGenerator::emit_expr_node(const ParenExpr& p) { emit_expr(p.sub); }

void CodeGenerator::emit_expr_node(const InvokeExpr& call) {
  emit_expr(call.fn);
  for (auto& arg : call.args)
    emit_expr(arg);
  for (auto& [k, arg] : call.kwargs) {
    emit_const(std::string(k));
    emit_expr(arg);
  }
  emit(sr::Call{static_cast<uint32_t>(call.args.size()),
                static_cast<uint32_t>(call.kwargs.size())});
}

void CodeGenerator::emit_expr_node(const SubscriptExpr& s) {
  emit_expr(s.primary);
  emit_expr(s.index);
  emit(sr::GetItem{});
}

void CodeGenerator::emit_expr_node(const MemberExpr& m) {
  emit_expr(m.primary);
  emit(sr::GetField{intern_name(m.member)});
}

void CodeGenerator::emit_expr_node(const SpawnExpr& s) {
  InvokeExpr* invoke = s.invoke->Get_if<InvokeExpr>();
  emit_expr(invoke->fn);  // (fn)
  for (auto& arg : invoke->args)
    emit_expr(arg);
  // (fn, arg...)
  for (auto& [k, arg] : invoke->kwargs) {
    emit_const(std::string(k));
    emit_expr(arg);
  }
  // (fn, arg..., kwarg...)

  emit(sr::MakeFiber{.argcnt = static_cast<uint32_t>(invoke->args.size()),
                     .kwargcnt = static_cast<uint32_t>(invoke->kwargs.size())});
  // -> (fiber)
}

void CodeGenerator::emit_expr_node(const AwaitExpr& s) {
  emit_expr(s.corout);
  // (corout)
  // TODO: support passing argument

  emit(sr::Await{});
}

// Statement codegen
void CodeGenerator::emit_stmt(std::shared_ptr<AST> s) {
  s->Apply([&](auto&& n) { emit_stmt_node(n); });
}

void CodeGenerator::emit_stmt_node(const ScopeStmt& s) {
  for (const auto& it : s.vars)
    scope_heuristic_[it] = SCOPE::GLOBAL;
}

void CodeGenerator::emit_stmt_node(const AssignStmt& s) {
  if (auto id = s.lhs->Get_if<Identifier>()) {
    // simple variable
    emit_expr(s.rhs);
    const std::string name{id->value};
    emit_store_var(name);
  } else if (auto mem = s.lhs->Get_if<MemberExpr>()) {
    // object field
    emit_expr(mem->primary);                       // (obj)
    emit_expr(s.rhs);                              // (obj,val)
    emit(sr::SetField{intern_name(mem->member)});  // ->
  } else if (auto sub = s.lhs->Get_if<SubscriptExpr>()) {
    // subscription
    emit_expr(sub->primary);  // (cont)
    emit_expr(sub->index);    // (cont,idx)
    emit_expr(s.rhs);         // (cont,idx,val)
    emit(sr::SetItem{});      // ->
  }
}

void CodeGenerator::emit_stmt_node(const AugStmt& s) {
  if (auto id = s.lhs->Get_if<Identifier>()) {
    // simple variable
    std::string name{id->value};
    emit_load_var(name);                        // (lhs)
    emit_expr(s.rhs);                           // (lhs,rhs)
    emit(sr::BinaryOp{s.GetRmAssignmentOp()});  // (result)
    emit_store_var(name);                       // ->
  } else if (auto mem = s.lhs->Get_if<MemberExpr>()) {
    // object field
    emit_expr(mem->primary);                       // (obj)
    emit(sr::Dup{});                               // (obj,obj)
    emit(sr::GetField{intern_name(mem->member)});  // (obj,val)
    emit_expr(s.rhs);                              // (obj,val,rhs)
    emit(sr::BinaryOp{s.GetRmAssignmentOp()});     // (obj,result)
    emit(sr::SetField{intern_name(mem->member)});  // ->
  } else if (auto sub = s.lhs->Get_if<SubscriptExpr>()) {
    // subscription
    emit_expr(sub->primary);                    // (cont)
    emit_expr(sub->index);                      // (cont,idx)
    emit(sr::Dup{.top_ofs = 1});                // (cont,idx,cont)
    emit(sr::Dup{.top_ofs = 1});                // (cont,idx,cont,idx)
    emit(sr::GetItem{});                        // (cont,idx,val)
    emit_expr(s.rhs);                           // (cont,idx,val,rhs)
    emit(sr::BinaryOp{s.GetRmAssignmentOp()});  // (cont,idx,result)
    emit(sr::SetItem{});                        // ->
  }
}

void CodeGenerator::emit_stmt_node(const IfStmt& s) {
  ++scope_depth_;

  emit_expr(s.cond);
  auto jfalse = code_size();
  emit(sr::JumpIfFalse{0});

  emit_stmt(s.then);
  if (s.els) {
    auto jend = code_size();
    emit(sr::Jump{0});

    patch(jfalse, code_size());
    emit_stmt(s.els);
    patch(jend, code_size());
  } else {
    patch(jfalse, code_size());
  }

  --scope_depth_;
}

void CodeGenerator::emit_stmt_node(const WhileStmt& s) {
  ++scope_depth_;

  auto loop_top = code_size();
  emit_expr(s.cond);
  auto exitj = code_size();
  emit(sr::JumpIfFalse{0});
  emit_stmt(s.body);

  auto jmp = code_size();
  emit(sr::Jump{0});
  patch(jmp, loop_top);
  patch(exitj, code_size());

  --scope_depth_;
}

void CodeGenerator::emit_stmt_node(const ForStmt& f) {
  ++scope_depth_;

  if (f.init)
    emit_stmt(f.init);

  auto condpos = code_size();
  if (f.cond)
    emit_expr(f.cond);
  else
    emit(sr::Push{constant(Value(true))});

  auto exitj = code_size();
  emit(sr::JumpIfFalse{0});
  emit_stmt(f.body);
  if (f.inc)
    emit_stmt(f.inc);

  auto jmp = code_size();
  emit(sr::Jump{0});
  patch(jmp, condpos);

  patch(exitj, code_size());

  --scope_depth_;
}

void CodeGenerator::emit_stmt_node(const BlockStmt& s) {
  ++scope_depth_;
  for (auto& stmt : s.body)
    emit_stmt(stmt);
  --scope_depth_;
}

bool CodeGenerator::emit_function(const FuncDecl& fn,
                                  CompileMode nested_mode,
                                  bool implicit_parameters) {
  // compile body with a fresh compiler
  CodeGenerator nested(gc_, repl_mode_);
  nested.SetChunk(gc_->Allocate<serilang::Code>());
  nested.scope_heuristic_ = scope_heuristic_;
  nested.mode_ = nested_mode;

  const auto propagate_nested_errors = [&] {
    errors_.insert(errors_.end(), nested.errors_.begin(), nested.errors_.end());
  };

  if (!nested.add_local(fn.name))
    return propagate_nested_errors(), false;
  for (auto& p : fn.params) {
    if (implicit_parameters)
      nested.scope_heuristic_.erase(p);
    if (!nested.add_local(p))
      return propagate_nested_errors(), false;
  }
  for (auto& p : fn.default_params)
    if (!nested.add_local(p.first))
      return propagate_nested_errors(), false;
  if (!fn.var_arg.empty())
    if (!nested.add_local(fn.var_arg))
      return propagate_nested_errors(), false;
  if (!fn.kw_arg.empty())
    if (!nested.add_local(fn.kw_arg))
      return propagate_nested_errors(), false;

  nested.emit_stmt(fn.body);
  nested.emit_return();

  if (!nested.Ok())
    return propagate_nested_errors(), false;

  emit_const(nested.GetChunk());
  for (auto& p : fn.default_params) {
    emit_const(p.first);
    emit_expr(p.second);
  }
  for (auto& p : fn.params)
    emit_const(p);
  for (auto& p : fn.default_params)
    emit_const(p.first);

  emit(sr::MakeFunction{.entry = 0,
                        .nparam = fn.params.size() + fn.default_params.size(),
                        .ndefault = fn.default_params.size(),
                        .has_vararg = !fn.var_arg.empty(),
                        .has_kwarg = !fn.kw_arg.empty()});

  return true;
}

void CodeGenerator::emit_return(std::shared_ptr<ExprAST> expr) {
  if (mode_ == CompileMode::Ctor) {
    if (expr)
      AddError("Can't return a value from __init__");
    emit(sr::LoadFast{.slot = 1});  // 'self'
    emit(sr::Return{});
  } else if (mode_ != CompileMode::Function) {
    AddError("'return' outside function");
    emit(sr::Push{constant(Value(std::monostate()))});
  } else {
    emit_expr(expr);
    emit(sr::Return{});
  }
}

void CodeGenerator::emit_stmt_node(const FuncDecl& fn) {
  ++scope_depth_;
  if (!emit_function(fn)) {
    --scope_depth_;
    return;
  }
  emit(sr::StoreGlobal{intern_name(fn.name)});
  --scope_depth_;
}

void CodeGenerator::emit_stmt_node(const ClassDecl& cd) {
  bool ok = true;
  if (cd.memfn.size() > std::numeric_limits<uint16_t>::max()) {
    AddError("too many member functions in class", cd.name_loc);
    ok = false;
  }
  if (cd.staticfn.size() > std::numeric_limits<uint16_t>::max()) {
    AddError("too many static functions in class", cd.name_loc);
    ok = false;
  }
  if (cd.properties.size() > std::numeric_limits<uint16_t>::max()) {
    AddError("too many properties in class", cd.name_loc);
    ok = false;
  }
  if (!ok)
    return;

  // class member functions
  for (auto& m : cd.memfn) {
    std::string const& name = m.name;
    emit(sr::Push{constant(name)});
    if (!emit_function(
            m, name == "__init__" ? CompileMode::Ctor : CompileMode::Function))
      return;
  }

  // class static functions
  for (auto& m : cd.staticfn) {
    emit(sr::Push{constant(m.name)});
    if (!emit_function(m))
      return;
  }

  // class properties
  for (const auto& property : cd.properties) {
    emit_const(property.name);

    if (property.getter) {
      if (!emit_function(*property.getter, CompileMode::Function,
                         /*implicit_parameters=*/true))
        return;
    } else {
      emit_const(std::monostate());
    }

    if (property.setter) {
      if (!emit_function(*property.setter, CompileMode::Function,
                         /*implicit_parameters=*/true))
        return;
    } else {
      emit_const(std::monostate());
    }
  }

  emit(sr::MakeClass{intern_name(cd.name),
                     static_cast<uint16_t>(cd.memfn.size()),
                     static_cast<uint16_t>(cd.staticfn.size()),
                     static_cast<uint16_t>(cd.properties.size())});
  emit(sr::StoreGlobal{intern_name(cd.name)});
}

void CodeGenerator::emit_stmt_node(const ReturnStmt& r) {
  emit_return(r.value);
}

void CodeGenerator::emit_stmt_node(const YieldStmt& y) {
  emit_expr(y.value);

  if (mode_ != CompileMode::Function) {
    AddError("'yield' outside function");
    return;
  }

  emit(sr::Yield{});
}

void CodeGenerator::emit_stmt_node(const ThrowStmt& t) {
  emit_expr(t.value);
  emit(sr::Throw{});
}

void CodeGenerator::emit_stmt_node(const TryStmt& t) {
  ++scope_depth_;
  auto try_begin = code_size();
  emit(sr::TryBegin{0});
  emit_stmt(t.body);
  emit(sr::TryEnd{});

  auto jmp = code_size();
  emit(sr::Jump{0});

  auto catch_start = code_size();
  patch(try_begin, catch_start);

  emit_store_var(t.catch_var);
  emit_stmt(t.handler);

  auto end_pos = code_size();
  patch(jmp, end_pos);
  --scope_depth_;
}

void CodeGenerator::emit_stmt_node(const ImportStmt& is) {
  emit(sr::LoadGlobal{intern_name("import")});
  emit_const(is.mod);
  emit(sr::Call{1, 0});
  if (is.names.empty()) {
    auto name = is.alias.empty() ? is.mod : is.alias;
    emit(sr::StoreGlobal{intern_name(name)});
  } else {
    for (auto const& [sym, alias] : is.names) {
      emit(sr::Dup{});
      emit(sr::GetField{intern_name(sym)});
      emit(sr::StoreGlobal{intern_name(alias.empty() ? sym : alias)});
    }
    emit(sr::Pop{1});
  }
}

void CodeGenerator::emit_stmt_node(const std::shared_ptr<ExprAST>& s) {
  if (repl_mode_ && scope_depth_ == 0 && mode_ == CompileMode::Global) {
    emit(sr::LoadGlobal{intern_name(kReplPrintName)});
    emit_expr(s);
    emit(sr::Call{1, 0});
  } else {
    emit_expr(s);
    emit(sr::Pop{1});
  }
}

namespace {
// -- Offset helper --------------------------------------------------
template <typename offset_t>
static inline constexpr auto rel(offset_t from, offset_t to) -> offset_t {
  return to - from;
}
}  // namespace

void CodeGenerator::patch(std::size_t site,
                          std::size_t target) {  // Patch jumps
  const auto offset =
      rel<int32_t>(site + sizeof(std::byte) + sizeof(int32_t), target);

  switch (static_cast<sr::OpCode>((*chunk_)[site])) {
    case sr::OpCode::Jump:
    case sr::OpCode::JumpIfFalse:
    case sr::OpCode::JumpIfTrue:
    case sr::OpCode::TryBegin:
      chunk_->Write(site + 1, offset);
      break;

    default:
      throw std::runtime_error(
          "Codegen: invalid patch site (type" +
          std::to_string(static_cast<uint8_t>((*chunk_)[site])) + ')');
  }
}

}  // namespace m6
