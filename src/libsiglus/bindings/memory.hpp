// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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

#include "core/memory_internal/memory.hpp"
#include "core/property_storage.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace serilang {
struct Fiber;
class Value;
}  // namespace serilang
namespace libsiglus {
struct SiglusMachine;
}

namespace libsiglus::binding {

// Owned lists and property aliases keep their backing cell alive independently
// of the VM. Replacing a property's value does not replace its cell.
template <typename T>
struct ListHandle {
  PropertyStorage::Cell cell;

  explicit ListHandle(PropertyStorage::Cell value) : cell(std::move(value)) {
    if (!cell)
      throw std::invalid_argument("null list cell");
  }
  explicit ListHandle(int size) {
    if (size < 0)
      throw std::invalid_argument("negative list size");
    cell = std::make_shared<PropertyStorage::Entry>(std::vector<T>(size), size);
  }
  std::vector<T>& Storage(serilang::Fiber&) const {
    auto* values = std::get_if<std::vector<T>>(&cell->value);
    if (!values)
      throw std::runtime_error(
          "cell no longer contains the expected list type");
    return *values;
  }
  int InitialSize() const { return cell->initial_size; }
};
using IntListHandle = ListHandle<int>;
using StrListHandle = ListHandle<std::string>;

// Banks resolve through the executing fiber, rather than owning vector data.
struct IntBankHandle {
  Memory* memory;
  IntBank bank;
  std::vector<int>& Storage(serilang::Fiber&) const;
  int InitialSize() const { return 0; }
};
struct StrBankHandle {
  Memory* memory;
  StrBank bank;
  std::vector<std::string>& Storage(serilang::Fiber&) const;
  int InitialSize() const { return 0; }
};

// Runtime-only views capture their C++ owner, never a GC-managed wrapper.
struct IntListRef {
  std::function<std::vector<int>&(serilang::Fiber&)> access;
  std::vector<int>& Storage(serilang::Fiber& fiber) const {
    return access(fiber);
  }
  int InitialSize() const { return 0; }
};
struct IntBitView {
  IntListRef owner;
  std::uint8_t bits;
  int Get(serilang::Fiber&, int index) const;
  void Set(serilang::Fiber&, int index, int value) const;
};

// Return null for values outside the supported native list classes.
std::vector<int>* IntListData(SiglusMachine&,
                              serilang::Fiber&,
                              const serilang::Value&);
std::vector<std::string>* StrListData(SiglusMachine&,
                                      serilang::Fiber&,
                                      const serilang::Value&);

// Initial banks for a fresh host-invoked command or farcall fiber. Their
// lifetime is the fiber's; nested script calls manage additional banks inline.
MemoryHandle MakeCallMemory(Memory& memory,
                            const std::vector<int>& integers = {},
                            const std::vector<std::string>& strings = {});

}  // namespace libsiglus::binding
