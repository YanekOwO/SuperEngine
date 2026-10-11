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

#include "libsiglus/types.hpp"

#include <stdexcept>

namespace libsiglus {

std::string ToString(Type type) {
  switch (type) {
    case Type::None:
      return "null_t";
    case Type::Int:
      return "int";
    case Type::IntList:
      return "int[]";
    case Type::IntRef:
      return "int&";
    case Type::IntListRef:
      return "int[]&";

    case Type::String:
      return "str";
    case Type::StrList:
      return "str[]";
    case Type::StrRef:
      return "str&";
    case Type::StrListRef:
      return "str[]&";

    case Type::Label:
      return "label";
    case Type::List:
      return "list";

    case Type::Object:
      return "object";
    case Type::ObjList:
      return "object[]";

    case Type::Stage:
      return "stage";
    case Type::StageList:
      return "stage[]";
    case Type::Msgbk:
      return "msgbk";
    case Type::Group:
      return "group";
    case Type::GroupList:
      return "group[]";

    case Type::Screen:
      return "screen";
    case Type::EffectList:
      return "effect[]";
    case Type::Effect:
      return "effect";
    case Type::QuakeList:
      return "quake[]";
    case Type::Quake:
      return "quake";
    case Type::Input:
      return "input";
    case Type::KeyList:
      return "key[]";
    case Type::Key:
      return "key";
    case Type::Mouse:
      return "mouse";

    case Type::FrameAction:
      return "frame_action";
    case Type::FrameActionList:
      return "frame_action[]";

    case Type::Invalid:
      return "invalid";
    case Type::Other:
      return "other";
    default:
      return "typeid:" + std::to_string(static_cast<uint32_t>(type));
  }
}

std::string ToString(OperatorCode op) {
  switch (op) {
    case OperatorCode::None:
      return "<none>";
    case OperatorCode::Plus:
      return "+";
    case OperatorCode::Minus:
      return "-";
    case OperatorCode::Mult:
      return "*";
    case OperatorCode::Div:
      return "/";
    case OperatorCode::Mod:
      return "%";
    case OperatorCode::Equal:
      return "==";
    case OperatorCode::Ne:
      return "!=";
    case OperatorCode::Gt:
      return ">";
    case OperatorCode::Ge:
      return ">=";
    case OperatorCode::Lt:
      return "<";
    case OperatorCode::Le:
      return "<=";
    case OperatorCode::LogicalAnd:
      return "&&";
    case OperatorCode::LogicalOr:
      return "||";
    case OperatorCode::Inv:
      return "~";
    case OperatorCode::And:
      return "&";
    case OperatorCode::Or:
      return "|";
    case OperatorCode::Xor:
      return "^";
    case OperatorCode::Sl:
      return "<<";
    case OperatorCode::Sr:
      return ">>";
    case OperatorCode::Sru:
      return "u>>";
    default:
      return "<?>";
  }
}

Op LowerUnaryOperator(OperatorCode op) {
  switch (op) {
    case OperatorCode::Plus:
      return Op::Add;
    case OperatorCode::Minus:
      return Op::Sub;
    case OperatorCode::Inv:
      return Op::Tilde;
    default:
      throw std::runtime_error("unsupported unary operator: " + ToString(op));
  }
}

Op LowerBinaryOperator(OperatorCode op) {
  switch (op) {
    case OperatorCode::Plus:
      return Op::Add;
    case OperatorCode::Minus:
      return Op::Sub;
    case OperatorCode::Mult:
      return Op::Mul;
    case OperatorCode::Div:
      return Op::Div;
    case OperatorCode::Mod:
      return Op::Mod;
    case OperatorCode::Equal:
      return Op::Equal;
    case OperatorCode::Ne:
      return Op::NotEqual;
    case OperatorCode::Gt:
      return Op::Greater;
    case OperatorCode::Ge:
      return Op::GreaterEqual;
    case OperatorCode::Lt:
      return Op::Less;
    case OperatorCode::Le:
      return Op::LessEqual;
    case OperatorCode::LogicalAnd:
      return Op::LogicalAnd;
    case OperatorCode::LogicalOr:
      return Op::LogicalOr;
    case OperatorCode::And:
      return Op::BitAnd;
    case OperatorCode::Or:
      return Op::BitOr;
    case OperatorCode::Xor:
      return Op::BitXor;
    case OperatorCode::Sl:
      return Op::ShiftLeft;
    case OperatorCode::Sr:
      return Op::ShiftRight;
    case OperatorCode::Sru:
      return Op::ShiftUnsignedRight;
    default:
      throw std::runtime_error("unsupported binary operator: " + ToString(op));
  }
}

}  // namespace libsiglus
