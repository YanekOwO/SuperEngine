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

#include "vm/iobject.hpp"
#include "vm/exception.hpp"
#include "vm/value.hpp"
#include "vm/vm.hpp"

namespace serilang {

std::string IObject::Str() const { return "<str: ?>"; }
std::string IObject::Desc() const { return "<desc: ?>"; }

void IObject::Call(VM& vm,
                   Fiber& f,
                   uint8_t nargs,
                   uint8_t nkwargs,
                   CallResultMode mode) {
  vm.Error(f, '\'' + Desc() + "' object is not callable.");
}

void IObject::GetItem(VM& vm, Fiber& f) {
  vm.Error(f, '\'' + Desc() + "' object is not subscriptable.");
}

void IObject::SetItem(VM& vm, Fiber& f) {
  vm.Error(f, '\'' + Desc() + "' object does not support item assignment.");
}

TempValue IObject::GetMember(std::string_view mem) {
  throw RuntimeError('\'' + Desc() + "' object has no member '" +
                     std::string(mem) + '\'');
}

void IObject::SetMember(std::string_view mem, Value value) {
  throw RuntimeError('\'' + Desc() +
                     "' object does not support member assignment.");
}

void IObject::GetMember(VM& vm, Fiber& f, std::string_view mem) {
  Value result = vm.AddTrack(GetMember(mem));
  f.op_stack.back() = std::move(result);
}

void IObject::SetMember(VM& vm, Fiber& f, std::string_view mem) {
  SetMember(mem, f.op_stack.back());
  f.op_stack.resize(f.op_stack.size() - 2);
}

std::size_t IObject::Hash() const {
  throw RuntimeError('\'' + Desc() + "' object is not hashable.");
}

std::optional<bool> IObject::Bool() const { return std::nullopt; }

}  // namespace serilang
