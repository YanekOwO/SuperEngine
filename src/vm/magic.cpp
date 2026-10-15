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

#include "vm/magic.hpp"

#include "vm/exception.hpp"
#include "vm/iobject.hpp"

namespace serilang::magic {

std::optional<TempValue> CallMagicIfPresent(VM& vm,
                                            Fiber& f,
                                            Value& receiver,
                                            std::string_view name,
                                            std::initializer_list<Value> args) {
  (void)vm;
  (void)f;
  (void)args;

  IObject* obj = receiver.Get_if<IObject>();
  if (!obj)
    return std::nullopt;

  try {
    // Return the member value to the caller; execution is the caller's choice.
    return obj->GetMember(name);
  } catch (RuntimeError&) {
    return std::nullopt;
  }
}

}  // namespace serilang::magic
