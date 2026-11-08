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

#include "libsiglus/archive.hpp"

#include <format>
#include <span>
#include <utility>

namespace libsiglus {

static PropertyStorage::Table MakeTable(std::span<const Property> properties) {
  PropertyStorage::Table table;
  table.reserve(properties.size());
  for (const auto& property : properties) {
    PropertyStorage::Entry entry;
    switch (property.form) {
      case Type::Int:
        entry.value = 0;
        break;
      case Type::String:
        entry.value = std::string{};
        break;
      case Type::IntList:
      case Type::StrList:
        if (property.size < 0)
          throw std::invalid_argument("negative property array size");
        entry.initial_size = property.size;
        if (property.form == Type::IntList)
          entry.value = PropertyStorage::Integers(property.size);
        else
          entry.value = PropertyStorage::Strings(property.size);
        break;
      default:
        break;
    }
    table.push_back(std::move(entry));
  }
  return table;
}

void PropertyStorage::Initialize(const Archive& archive) {
  decltype(tables_) tables;
  tables.emplace(-1, MakeTable(archive.prop_));
  for (std::size_t i = 0; i < archive.GetScenarioCount(); ++i)
    tables.emplace(static_cast<int>(i),
                   MakeTable(archive.ParseScene(static_cast<int>(i)).property));
  tables_.swap(tables);
}

const PropertyStorage::Entry& PropertyStorage::Get(int scene, int index) const {
  const auto it = tables_.find(scene);
  if (it == tables_.end())
    throw std::out_of_range(std::format("property scene {} not found", scene));
  if (index < 0 || static_cast<std::size_t>(index) >= it->second.size())
    throw std::out_of_range(std::format(
        "property index {} out of range for scene {}", index, scene));
  return it->second[index];
}

PropertyStorage::Entry& PropertyStorage::Get(int scene, int index) {
  return const_cast<Entry&>(std::as_const(*this).Get(scene, index));
}

}  // namespace libsiglus
