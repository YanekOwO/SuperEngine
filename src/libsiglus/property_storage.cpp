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

#include "libsiglus/property_storage.hpp"

namespace libsiglus {

std::shared_ptr<PropertyStorage::Table> PropertyStorage::Get(
    int scene,
    std::span<const Property> properties) {
  if (auto found = tables_.find(scene); found != tables_.end())
    return found->second;
  auto table = std::make_shared<Table>();
  table->reserve(properties.size());
  for (const auto& property : properties) {
    switch (property.form) {
      case Type::Int:
        table->emplace_back(0);
        break;
      case Type::String:
        table->emplace_back(std::string{});
        break;
      case Type::IntList: {
        auto storage = std::make_shared<std::vector<int>>();
        table->emplace_back(std::make_shared<IntListFacade<serilang::Fiber&>>(
            [storage](serilang::Fiber&) -> std::vector<int>& {
              return *storage;
            },
            property.size));
        break;
      }
      case Type::StrList: {
        auto storage = std::make_shared<std::vector<std::string>>();
        table->emplace_back(std::make_shared<StrListFacade<serilang::Fiber&>>(
            [storage](serilang::Fiber&) -> std::vector<std::string>& {
              return *storage;
            },
            property.size));
        break;
      }
      default:
        table->emplace_back(std::monostate{});
        break;
    }
  }
  tables_.emplace(scene, table);
  return table;
}

}  // namespace libsiglus
