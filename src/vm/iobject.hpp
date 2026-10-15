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

#include "vm/gc.hpp"
#include "vm/objtype.hpp"
#include "vm/value_fwd.hpp"

#include <optional>
#include <string>

namespace serilang {
struct Fiber;

struct GCVisitor;

class IObject {
 public:
  GCHeader hdr_;

  virtual ~IObject() = default;
  constexpr virtual ObjType Type() const noexcept = 0;
  constexpr virtual size_t Size() const noexcept = 0;

  virtual void MarkRoots(GCVisitor& visitor) = 0;

  virtual std::string Str() const;
  virtual std::string Desc() const;

  virtual void Call(VM& vm,
                    Fiber& f,
                    uint8_t nargs,
                    uint8_t nkwargs,
                    CallResultMode mode = CallResultMode::Keep);

  virtual void GetItem(VM& vm, Fiber& f);
  virtual void SetItem(VM& vm, Fiber& f);

  virtual TempValue GetMember(std::string_view mem);
  virtual void SetMember(std::string_view mem, Value value);

  // Stack contracts: (receiver) -> (value), (receiver, value) -> ().
  // These operations may enter a script accessor on the current fiber.
  virtual void GetMember(VM& vm, Fiber& f, std::string_view mem);
  virtual void SetMember(VM& vm, Fiber& f, std::string_view mem);

  virtual std::size_t Hash() const;

  // Optional truthiness hook. If not provided, non-null objects are truthy.
  virtual std::optional<bool> Bool() const;
};

}  // namespace serilang
