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

#include "machine/op.hpp"
#include "utilities/mpl.hpp"

#include <concepts>
#include <cstdint>
#include <variant>

namespace serilang {

// ––– 1. Stack manipulation ––––––––––––––––––––––––––––––––––––––––
struct Push {
  uint32_t const_index;
};  // () -> (value)
struct Dup {
  uint8_t top_ofs = 0;
};  // (x...)   -> (x,...,x)
struct Swap {};  // (a,b) -> (b,a)
struct Pop {
  uint8_t count = 1;
};  // (...n values...) ->

// ––– 2. Arithmetic / logic ––––––––––––––––––––––––––––––––––––––––
struct BinaryOp {
  Op op;
};  // (lhs,rhs) -> (result)

struct UnaryOp {
  Op op;
};  // (x) -> (result)

// ––– 3. Local / global / up‑value access ––––––––––––––––––––––––––
struct LoadFast {
  uint16_t slot;
};  // () -> (value)
struct StoreFast {
  uint16_t slot;
};  // (value) ->
struct LoadGlobal {
  uint32_t name_index;
};  // () -> (value)
struct StoreGlobal {
  uint32_t name_index;
};  // (value) ->
struct LoadUpvalue {
  uint8_t slot;
};  // () -> (value)
struct StoreUpvalue {
  uint8_t slot;
};  // (value) ->
struct CloseUpvalues {
  uint8_t from_slot;
};  // close ≥slot up-vals

// ––– 4. Control flow ––––––––––––––––––––––––––––––––––––––––––––––
struct Jump {
  int32_t offset;
};  // ->
struct JumpIfTrue {
  int32_t offset;
};  // (cond) ->
struct JumpIfFalse {
  int32_t offset;
};  // (cond) ->
struct Return {};  // (retval) -> pops frame

// ––– 5. Function & call –––––––––––––––––––––––––––––––––––––––––––
struct MakeFunction {
  uint32_t entry = 0;
  uint32_t nparam = 0;
  uint32_t ndefault = 0;
  bool has_vararg = false;
  bool has_kwarg = false;
};  // (code, def_name1, def_val1, ..., name1, ...) -> (fn)
struct Call {
  uint32_t argcnt = 0;
  uint32_t kwargcnt = 0;
};  // (fn,arg*,kwargs*) -> (ret)

// ––– 6. Object / container ops ––––––––––––––––––––––––––––––––––––
struct MakeList {
  uint32_t nelms;
};  // (elm,...) -> (list)
struct MakeDict {
  uint32_t nelms;
};  // (key,val,...) -> (dict)
struct MakeClass {
  uint32_t name_index;
  uint16_t nmemfn, nstaticfn;
  uint16_t nproperty = 0;
};  // (mem_fn*, static_fn*, (name, getter, setter)*) -> (class)
struct GetField {
  uint32_t name_index;
};  // (inst) -> (value)
struct SetField {
  uint32_t name_index;
};  // (inst,val) ->
struct GetItem {};  // (container,idx) -> (val)
struct SetItem {};  // (container,idx,val) ->

// ––– 7. Coroutine / fiber support ––––––––––––––––––––––––––––––––
struct MakeFiber {
  uint32_t argcnt;
  uint32_t kwargcnt;
};  // (…) -> (fiber)
struct Await {};  // (fiber,arg) -> (result|exc)
struct Yield {};  // (value) -> (yielded)
//  ‣ `Yield` suspends the *current* fiber and returns control to its resumer
//  ‣ `Resume` runs the fiber until next `Yield` or `Return`

// ––– 8. Exception handling -------------––––––––––––––––––---------
struct Throw {};  // (exc) -> never
struct TryBegin {
  int32_t handler_rel_ofs;
};  // mark try scope
struct TryEnd {};  // pop handler

// ––– 9. Debug -------------––––––––––––––––––---------
struct Nop {};
struct DebugValue {};  // (val) -> ()

using Instruction = std::variant<Nop,
                                 Push,
                                 Dup,
                                 Swap,
                                 Pop,
                                 BinaryOp,
                                 UnaryOp,
                                 LoadFast,
                                 StoreFast,
                                 LoadGlobal,
                                 StoreGlobal,
                                 LoadUpvalue,
                                 StoreUpvalue,
                                 CloseUpvalues,
                                 Jump,
                                 JumpIfTrue,
                                 JumpIfFalse,
                                 Return,
                                 MakeFunction,
                                 Call,
                                 MakeList,
                                 MakeDict,
                                 MakeClass,
                                 GetField,
                                 SetField,
                                 GetItem,
                                 SetItem,
                                 MakeFiber,
                                 Await,
                                 Yield,
                                 Throw,
                                 TryBegin,
                                 TryEnd,
                                 DebugValue>;

enum class OpCode : uint8_t {
  Nop,
  Push,
  Dup,
  Swap,
  Pop,
  BinaryOp,
  UnaryOp,
  LoadFast,
  StoreFast,
  LoadGlobal,
  StoreGlobal,
  LoadUpvalue,
  StoreUpvalue,
  CloseUpvalues,
  Jump,
  JumpIfTrue,
  JumpIfFalse,
  Return,
  MakeFunction,
  Call,
  MakeList,
  MakeDict,
  MakeClass,
  GetField,
  SetField,
  GetItem,
  SetItem,
  MakeFiber,
  Await,
  Yield,
  Throw,
  TryBegin,
  TryEnd,
  DebugValue
};

template <typename T>
  requires std::constructible_from<Instruction, T>
constexpr inline OpCode GetOpcode() {
  if constexpr (false)
    ;
  else if constexpr (std::same_as<T, Push>)
    return OpCode::Push;
  else if constexpr (std::same_as<T, Dup>)
    return OpCode::Dup;
  else if constexpr (std::same_as<T, Swap>)
    return OpCode::Swap;
  else if constexpr (std::same_as<T, Pop>)
    return OpCode::Pop;
  else if constexpr (std::same_as<T, BinaryOp>)
    return OpCode::BinaryOp;
  else if constexpr (std::same_as<T, UnaryOp>)
    return OpCode::UnaryOp;
  else if constexpr (std::same_as<T, LoadFast>)
    return OpCode::LoadFast;
  else if constexpr (std::same_as<T, StoreFast>)
    return OpCode::StoreFast;
  else if constexpr (std::same_as<T, LoadGlobal>)
    return OpCode::LoadGlobal;
  else if constexpr (std::same_as<T, StoreGlobal>)
    return OpCode::StoreGlobal;
  else if constexpr (std::same_as<T, LoadUpvalue>)
    return OpCode::LoadUpvalue;
  else if constexpr (std::same_as<T, StoreUpvalue>)
    return OpCode::StoreUpvalue;
  else if constexpr (std::same_as<T, CloseUpvalues>)
    return OpCode::CloseUpvalues;
  else if constexpr (std::same_as<T, Jump>)
    return OpCode::Jump;
  else if constexpr (std::same_as<T, JumpIfTrue>)
    return OpCode::JumpIfTrue;
  else if constexpr (std::same_as<T, JumpIfFalse>)
    return OpCode::JumpIfFalse;
  else if constexpr (std::same_as<T, Return>)
    return OpCode::Return;
  else if constexpr (std::same_as<T, MakeFunction>)
    return OpCode::MakeFunction;
  else if constexpr (std::same_as<T, Call>)
    return OpCode::Call;
  else if constexpr (std::same_as<T, MakeList>)
    return OpCode::MakeList;
  else if constexpr (std::same_as<T, MakeDict>)
    return OpCode::MakeDict;
  else if constexpr (std::same_as<T, MakeClass>)
    return OpCode::MakeClass;
  else if constexpr (std::same_as<T, GetField>)
    return OpCode::GetField;
  else if constexpr (std::same_as<T, SetField>)
    return OpCode::SetField;
  else if constexpr (std::same_as<T, GetItem>)
    return OpCode::GetItem;
  else if constexpr (std::same_as<T, SetItem>)
    return OpCode::SetItem;
  else if constexpr (std::same_as<T, MakeFiber>)
    return OpCode::MakeFiber;
  else if constexpr (std::same_as<T, Await>)
    return OpCode::Await;
  else if constexpr (std::same_as<T, Yield>)
    return OpCode::Yield;
  else if constexpr (std::same_as<T, Throw>)
    return OpCode::Throw;
  else if constexpr (std::same_as<T, TryBegin>)
    return OpCode::TryBegin;
  else if constexpr (std::same_as<T, TryEnd>)
    return OpCode::TryEnd;
  else if constexpr (std::same_as<T, Nop>)
    return OpCode::Nop;
  else if constexpr (std::same_as<T, DebugValue>)
    return OpCode::DebugValue;

  else
    static_assert(always_false<T>);
}

}  // namespace serilang
