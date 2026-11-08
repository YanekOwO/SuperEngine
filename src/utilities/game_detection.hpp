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

#include <filesystem>
#include <string>
#include <vector>

enum class EngineKind { Unknown, RealLive, Siglus };

struct GameLayout {
  EngineKind engine = EngineKind::Unknown;
  std::filesystem::path root;
  std::filesystem::path gameexe_ini;
  // An archive and its adjacent overrides, or a directory of loose scenes.
  std::filesystem::path seen_archive;
  std::filesystem::path seen_directory;
  std::vector<std::string> evidence;
};

// Detects from game files, then tries KINETICDATA and REALLIVEDATA if needed.
// Returns absolute paths. Invalid roots throw
// std::filesystem::filesystem_error; unrecognized or unreadable game files
// yield EngineKind::Unknown.
GameLayout DetectGameRoot(const std::filesystem::path& root);
