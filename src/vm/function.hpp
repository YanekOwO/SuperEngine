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

#include "utilities/expected.hpp"
#include "vm/iobject.hpp"
#include "vm/value.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace serilang {
struct Dict;
struct Code;
class GarbageCollector;

struct ArgumentList {
  uint32_t nparam = 0;
  std::unordered_map<std::string, size_t> param_index;
  std::unordered_map<size_t, Value> defaults;
  bool has_vararg = false;
  bool has_kwarg = false;

  expected<void, std::string> Load(
      std::shared_ptr<GarbageCollector> gc,
      std::vector<std::optional<Value>>& local_stack,
      std::vector<Value>& op_stack,
      uint8_t nargs,
      uint8_t nkwargs) const;
};

struct Function : public IObject {
  static constexpr inline ObjType objtype = ObjType::Function;

  Code* chunk;
  uint32_t entry = 0;

  ArgumentList arglist;

  std::shared_ptr<std::unordered_map<std::string, Value>> globals;

  explicit Function(Code* chunk);

  constexpr ObjType Type() const noexcept final { return objtype; }
  constexpr size_t Size() const noexcept final { return sizeof(*this); }

  void MarkRoots(GCVisitor& visitor) override;

  std::string Str() const override;
  std::string Desc() const override;

  void Call(VM& vm,
            Fiber& f,
            uint8_t nargs,
            uint8_t nkwargs,
            CallResultMode mode = CallResultMode::Keep) override;
};

}  // namespace serilang
