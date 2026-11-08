// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 RLVM contributors
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

#include "srbind/module.hpp"
#include "vm/object.hpp"

#include <any>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace srbind {

class ClassRegistry {
 public:
  ClassRegistry() = default;

  template <typename T>
  void Register(class_<T> klass) {
    std::type_index tidx = typeid(T);
    klass_map_.insert_or_assign(std::move(tidx), std::move(klass));
  }

  template <typename T>
  class_<T>& GetClass() {
    const std::type_index tidx = typeid(T);
    auto it = klass_map_.find(tidx);
    auto* klass = it == klass_map_.end()
                      ? nullptr
                      : std::any_cast<class_<T>>(&it->second);
    if (!klass)
      throw std::runtime_error("class registry not found: " +
                               std::string(tidx.name()));
    return *klass;
  }

  template <typename T, typename... Ts>
  serilang::NativeInstance* MakeInst(Ts&&... args) {
    return GetClass<T>().make_inst(std::forward<Ts>(args)...);
  }

 private:
  std::unordered_map<std::type_index, std::any> klass_map_;
};

}  // namespace srbind
