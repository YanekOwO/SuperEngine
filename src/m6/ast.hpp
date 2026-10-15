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

#include "m6/source_location.hpp"

#include <memory>
#include <optional>
#include <variant>
#include <vector>

enum class Op;

namespace m6 {

class AST;
class ExprAST;
struct Token;

// -----------------------------------------------------------------------
// Expression AST Nodes

// Literal
struct NilLiteral {
  SourceLocation loc;
  std::string DebugString() const;
};

struct TrueLiteral {
  SourceLocation loc;
  std::string DebugString() const;
};

struct FalseLiteral {
  SourceLocation loc;
  std::string DebugString() const;
};

struct IntLiteral {
  int value;
  SourceLocation loc;
  std::string DebugString() const;
};

struct StrLiteral {
  std::string_view value;
  SourceLocation loc;
  std::string DebugString() const;
};

struct ListLiteral {
  std::vector<std::shared_ptr<ExprAST>> elements;
  SourceLocation loc;
  std::string DebugString() const;
};

struct DictLiteral {
  std::vector<std::pair<std::shared_ptr<ExprAST>, std::shared_ptr<ExprAST>>>
      elements;
  SourceLocation loc;
  std::string DebugString() const;
};

// Identifier
struct Identifier {
  std::string value;
  SourceLocation loc;
  std::string DebugString() const;
};

// Binary operation node
struct BinaryExpr {
  Op op;
  std::shared_ptr<ExprAST> lhs;
  std::shared_ptr<ExprAST> rhs;
  SourceLocation op_loc, lhs_loc, rhs_loc;

  std::string DebugString() const;
};

// Unary operation node
struct UnaryExpr {
  Op op;
  std::shared_ptr<ExprAST> sub;
  SourceLocation op_loc, sub_loc;

  std::string DebugString() const;
};

// Parenthesized expression node
struct ParenExpr {
  std::shared_ptr<ExprAST> sub;
  SourceLocation loc;

  std::string DebugString() const;
};

// Function call node
struct InvokeExpr {
  std::shared_ptr<ExprAST> fn;
  std::vector<std::shared_ptr<ExprAST>> args;
  std::vector<std::pair<std::string_view, std::shared_ptr<ExprAST>>> kwargs;
  SourceLocation fn_loc;
  std::vector<SourceLocation> args_loc;
  std::vector<SourceLocation> kwargs_loc;

  std::string DebugString() const;
};

// Array subscripting node
struct SubscriptExpr {
  std::shared_ptr<ExprAST> primary;
  std::shared_ptr<ExprAST> index;

  SourceLocation primary_loc, idx_loc;

  std::string DebugString() const;
};

// Member access node
struct MemberExpr {
  std::shared_ptr<ExprAST> primary;
  std::string_view member;

  SourceLocation primary_loc, mem_loc;

  std::string DebugString() const;
};

struct SpawnExpr {
  std::shared_ptr<ExprAST> invoke;
  SourceLocation kwLoc;
  std::string DebugString() const;
};

struct AwaitExpr {
  std::shared_ptr<ExprAST> corout;
  SourceLocation kwLoc;
  std::string DebugString() const;
};

// -----------------------------------------------------------------------
// ExprAST
using expr_variant_t = std::variant<NilLiteral,
                                    TrueLiteral,
                                    FalseLiteral,
                                    IntLiteral,
                                    StrLiteral,
                                    ListLiteral,
                                    DictLiteral,
                                    Identifier,
                                    InvokeExpr,
                                    SubscriptExpr,
                                    MemberExpr,
                                    BinaryExpr,
                                    UnaryExpr,
                                    SpawnExpr,
                                    AwaitExpr,
                                    ParenExpr>;

class ExprAST {
 public:
  ExprAST(expr_variant_t var) : var_(std::move(var)) {}

  std::string DumpAST(std::string txt = "",
                      std::string prefix = "",
                      bool is_last = true) const;

  template <class Visitor>
  decltype(auto) Apply(Visitor&& vis) {
    return std::visit(std::forward<Visitor>(vis), var_);
  }
  template <class Visitor>
  decltype(auto) Apply(Visitor&& vis) const {
    return std::visit(std::forward<Visitor>(vis), var_);
  }
  template <class R, class Visitor>
  R Apply(Visitor&& vis) {
    return std::visit<R>(std::forward<Visitor>(vis), var_);
  }
  template <class R, class Visitor>
  R Apply(Visitor&& vis) const {
    return std::visit<R>(std::forward<Visitor>(vis), var_);
  }

  template <typename T>
  auto Get_if() {
    return std::get_if<T>(&var_);
  }

  template <typename T>
  bool HoldsAlternative() const {
    return std::holds_alternative<T>(var_);
  }

 private:
  expr_variant_t var_;
};

// -----------------------------------------------------------------------
// AST

// Simple Assignment '='
struct AssignStmt {
  std::shared_ptr<ExprAST> lhs;
  std::shared_ptr<ExprAST> rhs;
  SourceLocation lhs_loc, op_loc, rhs_loc;

  std::string DebugString() const;
};

// Compound Assignment
struct AugStmt {
  std::shared_ptr<ExprAST> lhs;
  Op op;
  std::shared_ptr<ExprAST> rhs;
  SourceLocation lhs_loc, op_loc, rhs_loc;

  std::string DebugString() const;
  Op GetRmAssignmentOp() const;
};

struct IfStmt {
  std::shared_ptr<ExprAST> cond;
  std::shared_ptr<AST> then;
  std::shared_ptr<AST> els;

  std::string DebugString() const;
};

struct WhileStmt {
  std::shared_ptr<ExprAST> cond;
  std::shared_ptr<AST> body;

  std::string DebugString() const;
};

struct ForStmt {
  std::shared_ptr<AST> init;
  std::shared_ptr<ExprAST> cond;
  std::shared_ptr<AST> inc;
  std::shared_ptr<AST> body;

  std::string DebugString() const;
};

struct BlockStmt {
  std::vector<std::shared_ptr<AST>> body;

  std::string DebugString() const;
};

struct FuncDecl {
  std::string name;
  std::vector<std::string>
      params;  // Ordered list of required (no-default) parameters
  std::vector<std::pair<std::string, std::shared_ptr<ExprAST>>>
      default_params;  // Ordered list of parameters that have defaults, plus
                       // their default Value
  std::string
      var_arg;  // Name for a varargs ("*args") parameter, or empty if none
  std::string kw_arg;  // Name for a varkeywords ("**kwargs") parameter, or
                       // empty if none

  std::shared_ptr<AST> body;  // guaranteed BlockStmt

  SourceLocation name_loc;
  std::vector<SourceLocation> param_locs, def_params_loc;
  SourceLocation var_arg_loc, kw_arg_loc;

  std::string DebugString() const;
};

struct PropertyDecl {
  std::string name;
  std::optional<FuncDecl> getter, setter;
  SourceLocation name_loc;

  std::string DebugString() const;
};

struct ClassDecl {
  std::string name;
  std::vector<FuncDecl> memfn, staticfn;

  SourceLocation name_loc;
  std::vector<PropertyDecl> properties;

  std::string DebugString() const;
};

struct ReturnStmt {
  std::shared_ptr<ExprAST> value;  // nullptr means `return;`
  SourceLocation kw_loc;

  std::string DebugString() const;
};

struct YieldStmt {
  std::shared_ptr<ExprAST> value;
  SourceLocation kw_loc;

  std::string DebugString() const;
};

struct ThrowStmt {
  std::shared_ptr<ExprAST> value;
  SourceLocation kw_loc;

  std::string DebugString() const;
};

struct TryStmt {
  std::shared_ptr<AST> body;
  std::string catch_var;
  SourceLocation catch_loc;
  std::shared_ptr<AST> handler;

  std::string DebugString() const;
};

struct ImportStmt {
  std::string mod;
  std::string alias;
  std::vector<std::pair<std::string, std::string>> names;  // name, alias
  SourceLocation kw_loc;

  std::string DebugString() const;
};

struct ScopeStmt {
  std::vector<std::string> vars;
  std::vector<SourceLocation> locs;

  std::string DebugString() const;
};

using stmt_variant_t = std::variant<AssignStmt,
                                    AugStmt,
                                    IfStmt,
                                    WhileStmt,
                                    ForStmt,
                                    BlockStmt,
                                    FuncDecl,
                                    ClassDecl,
                                    ReturnStmt,
                                    YieldStmt,
                                    ThrowStmt,
                                    TryStmt,
                                    ImportStmt,
                                    ScopeStmt,
                                    std::shared_ptr<ExprAST>>;

class AST {
 public:
  AST(stmt_variant_t var) : var_(std::move(var)) {}

  std::string DumpAST(std::string txt = "",
                      std::string prefix = "",
                      bool is_last = true) const;

  template <class Visitor>
  decltype(auto) Apply(Visitor&& vis) {
    return std::visit(std::forward<Visitor>(vis), var_);
  }
  template <class Visitor>
  decltype(auto) Apply(Visitor&& vis) const {
    return std::visit(std::forward<Visitor>(vis), var_);
  }
  template <class R, class Visitor>
  R Apply(Visitor&& vis) {
    return std::visit<R>(std::forward<Visitor>(vis), var_);
  }
  template <class R, class Visitor>
  R Apply(Visitor&& vis) const {
    return std::visit<R>(std::forward<Visitor>(vis), var_);
  }

  template <typename T>
  auto Get_if() {
    return std::get_if<T>(&var_);
  }

  template <typename T>
  bool HoldsAlternative() const {
    return std::holds_alternative<T>(var_);
  }

 private:
  stmt_variant_t var_;
};

}  // namespace m6
