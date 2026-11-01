// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2007 Elliot Glaysher
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
//
// -----------------------------------------------------------------------

#pragma once

#include "core/rect.hpp"

#include <cstdint>
#include <optional>

// A stored image cut, with its origin relative to the cropped pixel rectangle.
struct PixelRegion {
  Rect rect;
  Point origin;

  bool Contains(Point point) const {
    const auto x = static_cast<std::int64_t>(point.x()) + origin.x();
    const auto y = static_cast<std::int64_t>(point.y()) + origin.y();
    return x >= 0 && x < rect.width() && y >= 0 && y < rect.height();
  }
};

struct GrpRect {
  Rect rect;

  // Describes an offset to rect. Why VisualArts threw this in is
  // unknown.
  int originX = 0, originY = 0;

  // Sampling metadata can differ from the rectangle used for rendering.
  std::optional<PixelRegion> pixel_region;
};
