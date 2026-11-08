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

#include <string>
#include <variant>
#include <vector>

struct CmdTarget {
  int scene_id;
  int entry;
  bool operator==(const CmdTarget&) const = default;
};

struct SceneTarget {
  std::variant<int, std::string> scene = 0;
  int zlabel = 0;
  bool operator==(const SceneTarget&) const = default;
};

// Calls between execution instances contain engine data, never VM objects.
struct ScriptCall {
  std::variant<SceneTarget, CmdTarget> target = SceneTarget{};
  std::vector<int> integers;
  std::vector<std::string> strings;
  bool farcall = false;
  bool operator==(const ScriptCall&) const = default;
};
