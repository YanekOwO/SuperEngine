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

#include "core/grprect.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace g00 {

// The engine's decoded atlas, including separate rendering and sampling data.
struct Asset {
  int width = 0;
  int height = 0;
  std::vector<char> bgra;
  std::vector<GrpRect> regions;
};

Asset DecodeG00(std::string_view bytes);
Asset DecodePNG(std::string_view bytes);
// Writes lossless type 2 cuts; an empty region table becomes one full cut.
std::vector<char> EncodeG00(const Asset& asset);
std::vector<char> EncodePNG(const Asset& asset);

std::vector<GrpRect> MakeGrid(int width,
                              int height,
                              int cell_width,
                              int cell_height);
Asset ExtractRegion(const Asset& asset, std::size_t index);

// Versioned JSON uses x/y/width/height rectangles and engine atlas coordinates.
std::string Metadata(const Asset& asset);
void ApplyMetadata(Asset& asset, std::string_view json);

}  // namespace g00
