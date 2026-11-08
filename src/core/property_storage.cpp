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

#include "core/property_storage.hpp"

#include <format>
#include <utility>

void PropertyStorage::Initialize(Tables tables) {
  for (const auto& [id, table] : tables) {
    for (const auto& cell : table) {
      if (!cell)
        throw std::invalid_argument("null property cell");
      if (cell->initial_size < 0)
        throw std::invalid_argument("negative property initial size");
    }
  }
  tables_.swap(tables);
}

PropertyStorage::Cell PropertyStorage::GetCell(int table, int index) const {
  const auto it = tables_.find(table);
  if (it == tables_.end())
    throw std::out_of_range(std::format("property table {} not found", table));
  if (index < 0 || static_cast<std::size_t>(index) >= it->second.size())
    throw std::out_of_range(std::format(
        "property index {} out of range for table {}", index, table));
  return it->second[index];
}

const PropertyStorage::Entry& PropertyStorage::Get(int table, int index) const {
  return *GetCell(table, index);
}

PropertyStorage::Entry& PropertyStorage::Get(int table, int index) {
  return const_cast<Entry&>(std::as_const(*this).Get(table, index));
}
