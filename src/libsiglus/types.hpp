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

#pragma once

#include <cstdint>
#include <string>

#include "machine/op.hpp"

namespace libsiglus {

enum class Type : uint32_t {
  // reserved
  Invalid = 0x7FFFFFFF,
  Other = 0x6FFFFFFF,
  Callable = 0x0FFFFFFF,

  System,
  Math,
  FrameActionList,
  FrameAction = 1210,
  CounterList,
  Counter,
  Syscom,
  Screen,
  StageList,
  MwndList,
  Mwnd,
  Msgbk,
  GroupList,
  Group,
  BtnselItemList,
  WorldList,
  EffectList,
  Effect,
  QuakeList,
  Quake,
  Input,
  KeyList,
  Key,
  Mouse,
  Script,
  ScriptExcall,
  Excall,
  MaskList,
  Mask,
  IntEventList,
  IntEvent,
  ObjList,
  Movie,
  BgmTable,
  Bgm,
  Se,
  Pcm,
  Pcmch,
  PcmchList,
  PcmEventList,
  Wipe,
  Database = 0x2000000e,
  DatabaseList = 0x2000000f,

  None = 0x00,
  Int = 0x0a,
  IntList = 0x0b,
  IntRef = 0x0d,
  IntListRef = 0x0e,
  String = 0x14,
  StrList = 0x15,
  StrRef = 0x17,
  StrListRef = 0x18,
  Object = 0x51e,
  Stage = 1300,
  Label = 0x1e,
  List = 0xFFFFFFFF
};
std::string ToString(Type type);

enum class OperatorCode : uint8_t {
  None = 0x00,

  Plus = 0x01,
  Minus = 0x02,
  Mult = 0x03,
  Div = 0x04,
  Mod = 0x05,

  Equal = 0x10,
  Ne = 0x11,
  Gt = 0x12,
  Ge = 0x13,
  Lt = 0x14,
  Le = 0x15,

  LogicalAnd = 0x20,
  LogicalOr = 0x21,

  Inv = 0x30,
  And = 0x31,
  Or = 0x32,
  Xor = 0x33,
  Sl = 0x34,
  Sr = 0x35,
  Sru = 0x36
};

std::string ToString(OperatorCode op);
Op LowerUnaryOperator(OperatorCode op);
Op LowerBinaryOperator(OperatorCode op);

}  // namespace libsiglus
