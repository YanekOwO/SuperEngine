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

#include "core/object_internal/objdrawer.hpp"

#include <memory>
#include <optional>
#include <vector>

struct ObjectLayer {
  std::shared_ptr<SDLSurface> surface;
  Point offset;
  int cut_no = 0;
  int blend_type = 0;
};

// A collection of picture cuts positioned relative to an object's anchor.
class ObjectLayers {
 public:
  // Empty collections are valid. Replacements validate bounds before
  // committing.
  void SetLayers(std::vector<ObjectLayer> layers);
  const Rect& Bounds() const { return bounds_; }

  void Render(const GraphicsObjectData& drawer,
              const GraphicsObject& go,
              std::optional<ParentObjState> parent = {},
              std::optional<ObjectMask> mask = {}) const;
  // Points are relative to the top-left corner of Bounds().
  bool HitTest(Point source_point, bool alpha_test) const;
  // Composite readback uses the first cut's canvas and overlays later cuts.
  RGBAColour GetPixelColor(Point point) const;

 private:
  std::vector<ObjectLayer> layers_;
  Rect bounds_;
};
