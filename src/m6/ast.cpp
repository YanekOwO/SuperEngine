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

#include "m6/ast.hpp"

#include "m6/token.hpp"
#include "machine/op.hpp"
#include "utilities/string_utilities.hpp"

#include <format>
#include <sstream>
#include <unordered_map>

namespace m6 {

// -----------------------------------------------------------------------
// AST node member functions

std::string NilLiteral::DebugString() const { return "NilLiteral"; }
std::string TrueLiteral::DebugString() const { return "TrueLiteral"; }
std::string FalseLiteral::DebugString() const { return "FalseLiteral"; }
std::string IntLiteral::DebugString() const {
  return "IntLiteral " + std::to_string(value);
}
std::string StrLiteral::DebugString() const {
  return "StrLiteral " + std::string(value);
}
std::string ListLiteral::DebugString() const { return "ListLiteral"; }
std::string DictLiteral::DebugString() const { return "DictLiteral"; }
std::string BinaryExpr::DebugString() const {
  return "Binaryop " + ToString(op);
}
std::string UnaryExpr::DebugString() const { return "Unaryop " + ToString(op); }
std::string ParenExpr::DebugString() const { return "Parenthesis"; }
std::string InvokeExpr::DebugString() const { return "Invoke"; }
std::string SubscriptExpr::DebugString() const { return "Subscript"; }
std::string MemberExpr::DebugString() const { return "Member"; }
std::string Identifier::DebugString() const {
  return "ID " + std::string(value);
}
std::string SpawnExpr::DebugString() const { return "spawn"; }
std::string AwaitExpr::DebugString() const { return "await"; }

std::string AssignStmt::DebugString() const { return "Assign"; }
std::string AugStmt::DebugString() const { return "AugAssign " + ToString(op); }
Op AugStmt::GetRmAssignmentOp() const {
  switch (op) {
    case Op::AddAssign:
      return Op::Add;
    case Op::SubAssign:
      return Op::Sub;
    case Op::MulAssign:
      return Op::Mul;
    case Op::DivAssign:
      return Op::Div;
    case Op::Mod:
      return Op::Mod;
    case Op::BitAnd:
      return Op::BitAnd;
    case Op::BitOr:
      return Op::BitOr;
    case Op::BitXor:
      return Op::BitXor;
    case Op::ShiftLeft:
      return Op::ShiftLeft;
    case Op::ShiftRight:
      return Op::ShiftRight;
    case Op::ShiftUnsignedRight:
      return Op::ShiftUnsignedRight;
    case Op::PowAssign:
      return Op::Pow;
    default:
      return op;
  }
}
std::string IfStmt::DebugString() const { return "If"; }
std::string WhileStmt::DebugString() const { return "While"; }
std::string ForStmt::DebugString() const { return "For"; }
std::string BlockStmt::DebugString() const { return "Compound"; }
std::string FuncDecl::DebugString() const {
  std::vector<std::string> parts;
  parts.reserve(params.size() + default_params.size() + 2);
  for (auto const& p : params)
    parts.push_back(p);
  for (auto const& [n, _] : default_params)
    parts.push_back(n);
  if (!var_arg.empty())
    parts.push_back("*" + var_arg);
  if (!kw_arg.empty())
    parts.push_back("**" + kw_arg);
  return std::format("fn {}({})", name, Join(",", parts));
}
std::string ClassDecl::DebugString() const {
  return "class " + std::string(name);
}
std::string PropertyDecl::DebugString() const { return "property " + name; }
std::string ReturnStmt::DebugString() const { return "return"; }
std::string YieldStmt::DebugString() const { return "yield"; }
std::string ThrowStmt::DebugString() const { return "throw"; }
std::string TryStmt::DebugString() const { return "try"; }
std::string ScopeStmt::DebugString() const {
  return "scope " + Join(",", vars);
}
std::string ImportStmt::DebugString() const {
  if (names.empty()) {
    if (alias.empty())
      return "import " + mod;
    return "import " + mod + " as " + alias;
  }
  std::vector<std::string> parts;
  for (auto const& [n, a] : names) {
    if (!a.empty())
      parts.push_back(n + " as " + a);
    else
      parts.push_back(n);
  }
  return "from " + mod + " import " + Join(",", parts);
}

// -----------------------------------------------------------------------
// Visitor to print debug string for an AST
struct Dumper {
  std::string pref;
  bool isLast;

  std::string operator()(const auto& x) const {
    using T = std::decay_t<decltype(x)>;

    std::ostringstream oss;
    if (!pref.empty()) {
      oss << pref;
      oss << (isLast ? "└─" : "├─");
    }
    oss << x.DebugString() << '\n';

    std::string childPrefix = pref + (isLast ? "   " : "│  ");

    if constexpr (std::same_as<T, ListLiteral>) {
      for (size_t i = 0; i < x.elements.size(); ++i)
        oss << x.elements[i]->Apply(
            Dumper(childPrefix, i == x.elements.size() - 1));
    }
    if constexpr (std::same_as<T, DictLiteral>) {
      for (size_t i = 0; i < x.elements.size(); ++i) {
        oss << x.elements[i].first->Apply(Dumper(childPrefix, false));
        oss << x.elements[i].second->Apply(
            Dumper(childPrefix, i == x.elements.size() - 1));
      }
    }
    if constexpr (std::same_as<T, BinaryExpr>) {
      oss << x.lhs->Apply(Dumper(childPrefix, false));
      oss << x.rhs->Apply(Dumper(childPrefix, true));
    }
    if constexpr (std::same_as<T, AugStmt> || std::same_as<T, AssignStmt>) {
      oss << x.lhs->Apply(Dumper(childPrefix, false));
      oss << x.rhs->Apply(Dumper(childPrefix, true));
    }
    if constexpr (std::same_as<T, UnaryExpr> || std::same_as<T, ParenExpr>) {
      oss << x.sub->Apply(Dumper(childPrefix, true));
    }
    if constexpr (std::same_as<T, InvokeExpr>) {
      oss << x.fn->Apply(Dumper(childPrefix, x.args.empty()));
      for (size_t i = 0; i < x.args.size(); ++i) {
        oss << x.args[i]->Apply(
            Dumper(childPrefix, (i == x.args.size() - 1 && x.kwargs.empty())));
      }
      for (size_t i = 0; i < x.kwargs.size(); ++i) {
        oss << x.kwargs[i].second->DumpAST(
            "kwarg " + std::string(x.kwargs[i].first), childPrefix,
            i == x.kwargs.size() - 1);
      }
    }
    if constexpr (std::same_as<T, SubscriptExpr>) {
      oss << x.primary->Apply(Dumper(childPrefix, false));
      oss << x.index->Apply(Dumper(childPrefix, true));
    }
    if constexpr (std::same_as<T, MemberExpr>) {
      oss << x.primary->Apply(Dumper(childPrefix, false));
      oss << childPrefix << "└─ID " << x.member << '\n';
    }
    if constexpr (std::same_as<T, IfStmt>) {
      oss << x.cond->DumpAST("cond", childPrefix, false);
      oss << x.then->DumpAST("then", childPrefix, x.els ? false : true);
      if (x.els)
        oss << x.els->DumpAST("else", childPrefix, true);
    }
    if constexpr (std::same_as<T, WhileStmt>) {
      oss << x.cond->DumpAST("cond", childPrefix, false);
      oss << x.body->DumpAST("body", childPrefix, true);
    }
    if constexpr (std::same_as<T, ForStmt>) {
      oss << x.init->DumpAST("init", childPrefix, false);
      oss << x.cond->DumpAST("cond", childPrefix, false);
      oss << x.inc->DumpAST("inc", childPrefix, false);
      oss << x.body->DumpAST("body", childPrefix, true);
    }
    if constexpr (std::same_as<T, BlockStmt>) {
      for (size_t i = 0; i < x.body.size(); ++i)
        oss << x.body[i]->DumpAST("", childPrefix, i + 1 >= x.body.size());
    }
    if constexpr (std::same_as<T, FuncDecl>) {
      for (size_t i = 0; i < x.default_params.size(); ++i) {
        const auto& [default_name, default_expr_ast] = x.default_params[i];

        if (!default_expr_ast)
          oss << childPrefix << "├─"
              << "default " + default_name << '\n';
        else
          oss << x.default_params[i].second->DumpAST("default " + default_name,
                                                     childPrefix, false);
      }
      oss << x.body->DumpAST("body", childPrefix, true);
    }
    if constexpr (std::same_as<T, ClassDecl>) {
      for (size_t i = 0; i < x.memfn.size(); ++i) {
        Dumper dumper(childPrefix, (i + 1 >= x.memfn.size()) &&
                                       x.staticfn.empty() &&
                                       x.properties.empty());
        oss << dumper(x.memfn[i]);
      }
      for (size_t i = 0; i < x.staticfn.size(); ++i) {
        Dumper dumper(childPrefix,
                      i + 1 >= x.staticfn.size() && x.properties.empty());
        oss << dumper(x.staticfn[i]);
      }
      for (size_t i = 0; i < x.properties.size(); ++i) {
        Dumper dumper(childPrefix, i + 1 >= x.properties.size());
        oss << dumper(x.properties[i]);
      }
    }
    if constexpr (std::same_as<T, PropertyDecl>) {
      if (x.getter)
        oss << x.getter->body->DumpAST("get", childPrefix, !x.setter);
      if (x.setter)
        oss << x.setter->body->DumpAST("set", childPrefix, true);
    }
    if constexpr (std::same_as<T, ReturnStmt>) {
      if (x.value)
        oss << x.value->Apply(Dumper(childPrefix, true));
    }
    if constexpr (std::same_as<T, YieldStmt>) {
      if (x.value)
        oss << x.value->Apply(Dumper(childPrefix, true));
    }
    if constexpr (std::same_as<T, ThrowStmt>) {
      if (x.value)
        oss << x.value->Apply(Dumper(childPrefix, true));
    }
    if constexpr (std::same_as<T, TryStmt>) {
      oss << x.body->DumpAST("try", childPrefix, false);
      oss << x.handler->DumpAST("catch " + x.catch_var, childPrefix, true);
    }
    if constexpr (std::same_as<T, ImportStmt>) {
      // no subnodes to dump
    }
    if constexpr (std::same_as<T, SpawnExpr>) {
      oss << x.invoke->DumpAST("", childPrefix, true);
    }
    if constexpr (std::same_as<T, AwaitExpr>) {
      oss << x.corout->DumpAST("", childPrefix, true);
    }

    return oss.str();
  }
};

// -----------------------------------------------------------------------
// ExprAST
std::string ExprAST::DumpAST(std::string txt,
                             std::string pref,
                             bool isLast) const {
  if (txt.empty())
    return std::visit(Dumper(pref, isLast), var_);

  std::ostringstream oss;

  if (!pref.empty()) {
    oss << pref;
    oss << (isLast ? "└─" : "├─");
  }
  if (!txt.empty())
    oss << txt << '\n';

  std::string childPrefix = pref + (isLast ? "   " : "│  ");
  oss << std::visit(Dumper(childPrefix, true), var_);
  return oss.str();
}

// -----------------------------------------------------------------------
// AST
std::string AST::DumpAST(std::string txt, std::string pref, bool isLast) const {
  return std::visit(
      [&](const auto& x) -> std::string {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::same_as<T, std::shared_ptr<ExprAST>>)
          return x->DumpAST(std::move(txt), std::move(pref), isLast);
        else {
          if (txt.empty())
            return Dumper(pref, isLast)(x);

          std::ostringstream oss;
          if (!pref.empty()) {
            oss << pref;
            oss << (isLast ? "└─" : "├─");
          }
          oss << txt << '\n';

          std::string childPrefix = pref + (isLast ? "   " : "│  ");

          oss << Dumper(childPrefix, true)(x);
          return oss.str();
        }
      },
      var_);
}

}  // namespace m6
