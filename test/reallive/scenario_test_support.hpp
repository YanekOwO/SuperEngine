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

#include <cstddef>
#include <cstdint>
#include <string>

namespace reallive_test {

inline void PutU32(std::string& bytes, std::size_t at, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i)
    bytes.at(at + i) = static_cast<char>(value >> (i * 8));
}

// A valid scenario header with an empty script and no metadata.
inline std::string MakeScenario(std::uint32_t header_size = 0x1d0) {
  std::string scene(0x1d0, '\0');
  PutU32(scene, 0, header_size);
  PutU32(scene, 4, 10002);
  PutU32(scene, 0x14, scene.size());
  PutU32(scene, 0x20, scene.size());
  return scene;
}

inline std::string MakeArchive(const std::string& scene = MakeScenario(),
                               int scene_id = 1) {
  std::string archive(10000 * 8, '\0');
  PutU32(archive, scene_id * 8, archive.size());
  PutU32(archive, scene_id * 8 + 4, scene.size());
  archive += scene;
  return archive;
}

}  // namespace reallive_test
