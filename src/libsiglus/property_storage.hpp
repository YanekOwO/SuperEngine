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

#include "core/memory_internal/facade.hpp"
#include "libsiglus/property.hpp"

#include <memory>
#include <span>
#include <unordered_map>
#include <variant>

namespace serilang {
struct Fiber;
}

namespace libsiglus {

// Property values and list backing belong to the game, independently of any VM.
class PropertyStorage {
 public:
  using Integers = std::shared_ptr<IntListFacade<serilang::Fiber&>>;
  using Strings = std::shared_ptr<StrListFacade<serilang::Fiber&>>;
  using Value = std::variant<std::monostate,
                             bool,
                             int,
                             double,
                             std::string,
                             Integers,
                             Strings>;
  using Table = std::vector<Value>;

  std::shared_ptr<Table> Get(int scene, std::span<const Property> properties);

 private:
  std::unordered_map<int, std::shared_ptr<Table>> tables_;
};

}  // namespace libsiglus
