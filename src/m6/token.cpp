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

#include "m6/token.hpp"
#include "machine/op.hpp"

namespace m6 {

std::string Token::GetDebugString() const {
  return '<' + std::visit(tok::DebugStringVisitor(), token_) + ", " +
         std::to_string(loc_.begin_offset) + ',' +
         std::to_string(loc_.end_offset) + '>';
}

std::string tok::DebugStringVisitor::operator()(const tok::Reserved& p) const {
  std::string s;
  switch (p.type) {
    case Reserved::Type::_nil:
      s = "nil";
      break;
    case Reserved::Type::_true:
      s = "true";
      break;
    case Reserved::Type::_false:
      s = "false";
      break;
    case Reserved::Type::_if:
      s = "if";
      break;
    case Reserved::Type::_else:
      s = "else";
      break;
    case Reserved::Type::_for:
      s = "for";
      break;
    case Reserved::Type::_while:
      s = "while";
      break;
    case Reserved::Type::_fn:
      s = "fn";
      break;
    case Reserved::Type::_class:
      s = "class";
      break;
    case Reserved::Type::_goto:
      s = "goto";
      break;
    case Reserved::Type::_return:
      s = "return";
      break;
    case Reserved::Type::_yield:
      s = "yield";
      break;
    case Reserved::Type::_spawn:
      s = "spawn";
      break;
    case Reserved::Type::_await:
      s = "await";
      break;
    case Reserved::Type::_try:
      s = "try";
      break;
    case Reserved::Type::_catch:
      s = "catch";
      break;
    case Reserved::Type::_throw:
      s = "throw";
      break;
    case Reserved::Type::_global:
      s = "global";
      break;
    case Reserved::Type::_import:
      s = "import";
      break;
    case Reserved::Type::_from:
      s = "from";
      break;
    case Reserved::Type::_as:
      s = "as";
      break;
  }
  return "Reserved(" + s + ')';
}

std::string tok::DebugStringVisitor::operator()(const tok::Literal& p) const {
  return "Str(" + p.str + ')';
}

std::string tok::DebugStringVisitor::operator()(const tok::ID& p) const {
  return "ID(\"" + p.id + "\")";
}

std::string tok::DebugStringVisitor::operator()(const tok::WS&) const {
  return "ws";
}

std::string tok::DebugStringVisitor::operator()(const tok::Comment& p) const {
  const char* kind = p.kind == tok::Comment::Kind::Line ? "line" : "block";
  return "Comment(" + std::string(kind) + ",\"" + p.text + "\")";
}

std::string tok::DebugStringVisitor::operator()(const tok::Int& p) const {
  return "Int(" + std::to_string(p.value) + ")";
}

std::string tok::DebugStringVisitor::operator()(const tok::Operator& p) const {
  return "Operator(" + ToString(p.op) + ")";
}

std::string tok::DebugStringVisitor::operator()(const tok::Dollar&) const {
  return "dollar";
}

std::string tok::DebugStringVisitor::operator()(const tok::SquareL&) const {
  return "SquareL";
}

std::string tok::DebugStringVisitor::operator()(const tok::SquareR&) const {
  return "SquareR";
}

std::string tok::DebugStringVisitor::operator()(const tok::CurlyL&) const {
  return "CurlyL";
}

std::string tok::DebugStringVisitor::operator()(const tok::CurlyR&) const {
  return "CurlyR";
}

std::string tok::DebugStringVisitor::operator()(
    const tok::ParenthesisL&) const {
  return "ParenthesisL";
}

std::string tok::DebugStringVisitor::operator()(
    const tok::ParenthesisR&) const {
  return "ParenthesisR";
}

std::string tok::DebugStringVisitor::operator()(const tok::Semicol&) const {
  return "Semicol";
}

std::string tok::DebugStringVisitor::operator()(const tok::Colon&) const {
  return "Colon";
}

std::string tok::DebugStringVisitor::operator()(const tok::Eof&) const {
  return "EOF";
}

}  // namespace m6
