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
// Along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
//
// -----------------------------------------------------------------------

#pragma once

#include "m6/ast.hpp"

#include <string>
#include <variant>

namespace m6 {

namespace tok {

struct Reserved {
  enum Type {
    _nil,
    _true,
    _false,
    _if,
    _else,
    _while,
    _for,
    _class,
    _fn,
    _goto,
    _return,
    _yield,
    _spawn,
    _await,
    _try,
    _catch,
    _throw,
    _global,
    _import,
    _from,
    _as
  };
  Type type;
  auto operator<=>(const Reserved& rhs) const = default;
};

struct Literal {
  std::string str;
  auto operator<=>(const Literal& rhs) const = default;
};

struct ID {
  std::string id;
  auto operator<=>(const ID& rhs) const = default;
};

struct WS {
  auto operator<=>(const WS& rhs) const = default;
};

struct Comment {
  enum class Kind { Line, Block };
  Kind kind;
  std::string text;
  auto operator<=>(const Comment& rhs) const = default;
};

struct Int {
  int value;
  auto operator<=>(const Int& rhs) const = default;
};

struct Operator {
  Op op;
  auto operator<=>(const Operator& rhs) const = default;
};

struct Dollar {
  auto operator<=>(const Dollar& rhs) const = default;
};

struct SquareL {
  auto operator<=>(const SquareL& rhs) const = default;
};

struct SquareR {
  auto operator<=>(const SquareR& rhs) const = default;
};

struct CurlyL {
  auto operator<=>(const CurlyL& rhs) const = default;
};

struct CurlyR {
  auto operator<=>(const CurlyR& rhs) const = default;
};

struct ParenthesisL {
  auto operator<=>(const ParenthesisL& rhs) const = default;
};

struct ParenthesisR {
  auto operator<=>(const ParenthesisR& rhs) const = default;
};

struct Semicol {
  auto operator<=>(const Semicol& rhs) const = default;
};

struct Colon {
  auto operator<=>(const Colon& rhs) const = default;
};

struct Eof {
  auto operator<=>(const Eof& rhs) const = default;
};

using Token_t = std::variant<Reserved,
                             Literal,
                             ID,
                             WS,
                             Comment,
                             Int,
                             Operator,
                             Dollar,
                             SquareL,
                             SquareR,
                             CurlyL,
                             CurlyR,
                             ParenthesisL,
                             ParenthesisR,
                             Semicol,
                             Colon,
                             Eof>;

}  // namespace tok

struct Token {
  tok::Token_t token_;
  SourceLocation loc_ = SourceLocation();

  bool operator==(const tok::Token_t& rhs) const { return token_ == rhs; }
  bool operator!=(const tok::Token_t& rhs) const { return token_ != rhs; }

  template <typename T>
  auto HoldsAlternative() const {
    return std::holds_alternative<T>(token_);
  }

  template <typename T>
  auto GetIf() const {
    return std::get_if<T>(&token_);
  }

  std::string GetDebugString() const;
};

namespace tok {
struct DebugStringVisitor {
  std::string operator()(const tok::Reserved&) const;
  std::string operator()(const tok::Literal&) const;
  std::string operator()(const tok::ID&) const;
  std::string operator()(const tok::WS&) const;
  std::string operator()(const tok::Comment&) const;
  std::string operator()(const tok::Int&) const;
  std::string operator()(const tok::Operator&) const;
  std::string operator()(const tok::Dollar&) const;
  std::string operator()(const tok::SquareL&) const;
  std::string operator()(const tok::SquareR&) const;
  std::string operator()(const tok::CurlyL&) const;
  std::string operator()(const tok::CurlyR&) const;
  std::string operator()(const tok::ParenthesisL&) const;
  std::string operator()(const tok::ParenthesisR&) const;
  std::string operator()(const tok::Semicol&) const;
  std::string operator()(const tok::Colon&) const;
  std::string operator()(const tok::Eof&) const;
};
}  // namespace tok

}  // namespace m6
