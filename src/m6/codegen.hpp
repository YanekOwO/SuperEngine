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

#pragma once

#include "m6/ast.hpp"
#include "m6/error.hpp"
#include "utilities/transparent_hash.hpp"
#include "vm/gc.hpp"
#include "vm/instruction.hpp"
#include "vm/object.hpp"
#include "vm/value.hpp"

#include <concepts>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace m6 {

class CodeGenerator {
 public:
  explicit CodeGenerator(std::shared_ptr<serilang::GarbageCollector> gc,
                         bool repl = false);
  ~CodeGenerator();

  bool Ok() const;
  std::span<const Error> GetErrors() const;
  void ClearErrors();

  serilang::Code* GetChunk() const;
  void SetChunk(serilang::Code* c);

  void Gen(std::shared_ptr<AST> ast);

 private:
  // -- Type aliases ----------------------------------------------------
  using Value = serilang::Value;
  enum class SCOPE { NONE = 1, GLOBAL, LOCAL };
  enum class CompileMode { Global, Function, Ctor };

  // -- Data members ---------------------------------------------------
  std::shared_ptr<serilang::GarbageCollector> gc_;
  bool repl_mode_;
  CompileMode mode_;
  std::size_t scope_depth_;

  serilang::Code* chunk_;
  transparent_hashmap<SCOPE> scope_heuristic_;
  transparent_hashmap<std::size_t> locals_;
  std::size_t local_depth_;
  std::vector<Error> errors_;

  // -- Error handling -------------------------------------------------
  void AddError(std::string msg,
                std::optional<SourceLocation> loc = std::nullopt);

  // -- Constant-pool helpers ------------------------------------------
  uint32_t constant(Value v);
  uint32_t constant(std::string_view str);
  uint32_t intern_name(std::string_view s);
  void emit_const(Value v);
  void emit_const(std::string_view s);
  template <typename T>
    requires std::constructible_from<Value::value_t, T>
  inline void emit_const(T t) {
    if constexpr (std::same_as<T, std::string> ||
                  std::same_as<T, std::string_view>)
      emit_const(std::string_view(t));
    else
      emit_const(Value(std::move(t)));
  }

  // -- Identifier resolution ------------------------------------------
  std::optional<std::size_t> resolve_local(std::string_view id) const;
  std::optional<std::size_t> add_local(const std::string& id);
  SCOPE get_scope(std::string_view id);

  // -- Emit helpers ---------------------------------------------------
  template <typename T>
  inline void emit(T ins) {
    chunk_->Append(std::move(ins));
  }
  std::size_t code_size() const;
  void emit_store_var(const std::string& id);
  void emit_load_var(std::string_view id);

  // -- Expression codegen ---------------------------------------------
  void emit_expr(std::shared_ptr<ExprAST> n = nullptr);
  void emit_expr_node(const NilLiteral&);
  void emit_expr_node(const TrueLiteral&);
  void emit_expr_node(const FalseLiteral&);
  void emit_expr_node(const IntLiteral& n);
  void emit_expr_node(const StrLiteral& n);
  void emit_expr_node(const ListLiteral&);
  void emit_expr_node(const DictLiteral&);
  void emit_expr_node(const Identifier& n);
  void emit_expr_node(const UnaryExpr& u);
  void emit_expr_node(const BinaryExpr& b);
  void emit_expr_node(const ParenExpr& p);
  void emit_expr_node(const InvokeExpr& call);
  void emit_expr_node(const SubscriptExpr& s);
  void emit_expr_node(const MemberExpr& m);
  void emit_expr_node(const SpawnExpr& s);
  void emit_expr_node(const AwaitExpr& a);

  // -- Statement codegen ----------------------------------------------
  void emit_stmt(std::shared_ptr<AST> s);
  void emit_stmt_node(const ScopeStmt& s);
  void emit_stmt_node(const AssignStmt& s);
  void emit_stmt_node(const AugStmt& s);
  void emit_stmt_node(const IfStmt& s);
  void emit_stmt_node(const WhileStmt& s);
  void emit_stmt_node(const ForStmt& f);
  void emit_stmt_node(const BlockStmt& s);
  void emit_stmt_node(const FuncDecl& fn);
  void emit_stmt_node(const ClassDecl& cd);
  void emit_stmt_node(const ReturnStmt& r);
  void emit_stmt_node(const YieldStmt& y);
  void emit_stmt_node(const ThrowStmt& t);
  void emit_stmt_node(const TryStmt& t);
  void emit_stmt_node(const ImportStmt& is);
  void emit_stmt_node(const std::shared_ptr<ExprAST>& s);

  bool emit_function(const FuncDecl& fn,
                     CompileMode nested_mode = CompileMode::Function,
                     bool implicit_parameters = false);
  void emit_return(std::shared_ptr<ExprAST> expr = nullptr);

  // -- Jump-patching --------------------------------------------------
  void patch(std::size_t site, std::size_t target);
};

}  // namespace m6
