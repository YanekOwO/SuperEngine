
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

#include "m6/error.hpp"
#include "m6/source_location.hpp"
#include "m6/token.hpp"

#include <memory>
#include <span>
#include <vector>

namespace m6 {

class ExprAST;

class Parser {
  using iterator_t = Token*;

 public:
  explicit Parser(std::span<Token> input)
      : it_(input.data()),
        begin_(input.data()),
        end_(input.data() + input.size()) {}

  // Public entry points (never throw). They may return nullptr on fatal error.
  std::shared_ptr<ExprAST> ParseExpression();                    // one expr
  std::shared_ptr<AST> ParseStatement(bool requireSemi = true);  // one stmt
  std::vector<std::shared_ptr<AST>> ParseAll();                  // whole TU

  bool Ok() const;
  std::span<const Error> GetErrors() const;
  void ClearErrors();

 private:
  //------------------------------------------------------------------
  // helper functions
  //------------------------------------------------------------------
  inline SourceLocation LocPrevEnd(iterator_t it) {
    return (it - 1)->loc_.After();
  }

  void AddError(std::string m, iterator_t loc);
  void AddError(std::string m, SourceLocation loc);
  void Synchronize();  // panic‑mode error recovery

  template <class Tok>
  bool tryConsume() {
    if (it_ == end_)
      return false;
    if (!it_->template HoldsAlternative<Tok>())
      return false;
    ++it_;
    return true;
  }

  template <class Tok, class... Args>
  bool tryConsume(Args&&... params) {
    if (it_ == end_)
      return false;
    if (!it_->HoldsAlternative<Tok>())
      return false;
    if (auto p = it_->template GetIf<Tok>();
        *p == Tok{std::forward<Args>(params)...}) {
      ++it_;
      return true;
    }
    return false;
  }

  std::optional<Op> tryConsumeAny(std::initializer_list<Op> ops) {
    if (it_ == end_)
      return std::nullopt;
    if (auto p = it_->GetIf<tok::Operator>()) {
      for (auto cand : ops)
        if (cand == p->op) {
          ++it_;
          return cand;
        }
    }
    return std::nullopt;
  }

  template <class Tok>
  bool require(const char* msg) {
    if (!tryConsume<Tok>()) {
      AddError(msg, LocPrevEnd(it_));
      return false;
    }
    return true;
  }

  template <class Tok, class... Args>
  bool require(const char* msg, Args&&... params) {
    if (!tryConsume<Tok>(std::forward<Args>(params)...)) {
      AddError(msg, LocPrevEnd(it_));
      return false;
    }
    return true;
  }

 private:
  //------------------------------------------------------------------
  //  Recursive‑descent productions (all member functions)
  //------------------------------------------------------------------
  std::shared_ptr<AST> parseAssignment();
  std::shared_ptr<AST> parseImportStmt();
  std::shared_ptr<AST> parseFromImportStmt();
  std::shared_ptr<AST> parseFuncDecl(bool alreadyConsumedFn = false);
  std::shared_ptr<AST> parseClassDecl();
  std::optional<PropertyDecl> parsePropertyDecl();

  std::shared_ptr<ExprAST> parseLogicalOr();
  std::shared_ptr<ExprAST> parseLogicalAnd();
  std::shared_ptr<ExprAST> parseBitwiseOr();
  std::shared_ptr<ExprAST> parseBitwiseXor();
  std::shared_ptr<ExprAST> parseBitwiseAnd();
  std::shared_ptr<ExprAST> parseEquality();
  std::shared_ptr<ExprAST> parseRelational();
  std::shared_ptr<ExprAST> parseShift();
  std::shared_ptr<ExprAST> parseAdditive();
  std::shared_ptr<ExprAST> parseMultiplicative();
  std::shared_ptr<ExprAST> parseUnary();
  std::shared_ptr<ExprAST> parseExponentiation();
  std::shared_ptr<ExprAST> parseAwait();
  std::shared_ptr<ExprAST> parsePostfix();
  std::shared_ptr<ExprAST> parsePrimary();

  bool ScanParameterList(
      std::vector<std::string>& required,
      std::vector<SourceLocation>& requiredLocs,
      std::vector<std::pair<std::string, std::shared_ptr<ExprAST>>>& defaulted,
      std::vector<SourceLocation>& defaultedLocs,
      std::string& varArg,
      SourceLocation& varArgLoc,
      std::string& kwArg,
      SourceLocation& kwArgLoc);

 private:
  //------------------------------------------------------------------
  //  Data members
  //------------------------------------------------------------------
  iterator_t it_;     // current cursor
  iterator_t begin_;  // start of the buffer
  iterator_t end_;    // sentinel
  std::vector<Error> errors_;
};

}  // namespace m6
