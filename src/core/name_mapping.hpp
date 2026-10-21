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

#include <map>
#include <string>

class Gameexe;

// RealLive's #NAMAE replacements for speaker names in Clannad English Edition.
class NameMapping {
 public:
  NameMapping() = default;
  explicit NameMapping(Gameexe& gexe);

  // Both input and output are UTF-8. Unmapped names are returned unchanged.
  std::string Map(const std::string& utf8name) const;

 private:
  std::map<std::string, std::string> mappings_;
};
