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

#include "core/name_mapping.hpp"

#include "core/gameexe.hpp"
#include "utilities/string_utilities.hpp"

NameMapping::NameMapping(Gameexe& gexe) {
  for (auto entry : gexe.Filter("NAMAE")) {
    try {
      // NAMAE entries are CP932, independently of NAME_ENC.
      const std::string key = cp932toUTF8(entry.StrAt(0).value(), 0);
      const std::string value = cp932toUTF8(entry.StrAt(1).value(), 0);
      mappings_[key] = value;
    } catch (...) {
      // Ignore malformed entries without discarding other mappings.
    }
  }
}

std::string NameMapping::Map(const std::string& utf8name) const {
  const auto it = mappings_.find(utf8name);
  return it == mappings_.end() ? utf8name : it->second;
}
