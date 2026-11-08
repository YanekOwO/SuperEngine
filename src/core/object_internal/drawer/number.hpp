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

#include <boost/serialization/export.hpp>
#include "utilities/serialization.hpp"

#include "core/object_internal/object_layers.hpp"
#include "core/serialization_context.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

struct NumberObjectStyle {
  int digits = 0;
  bool zero = false, sign = false, pack_sign = false;
  int space_mode = 0, space = 0;

  bool operator==(const NumberObjectStyle&) const = default;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) {
    ar & digits & zero & sign & pack_sign & space_mode & space;
  }
};

struct NumberObjectGlyph {
  int pattern;
  int x;

  bool operator==(const NumberObjectGlyph&) const = default;
};

// Hidden leading slots still contribute to the position of visible glyphs.
std::vector<NumberObjectGlyph> LayoutNumberObject(
    int value,
    const NumberObjectStyle& style,
    int digit_width,
    int pattern_offset = 0);

// Number objects use a picture atlas with cuts 0-9 for digits.
class NumberDrawer final : public GraphicsObjectData {
 public:
  NumberDrawer(std::string filename, std::shared_ptr<SDLSurface> atlas);

  inline int value() const { return value_; }
  inline const NumberObjectStyle& style() const { return style_; }
  void SetNumber(int value);
  void SetStyle(NumberObjectStyle style);
  Size PatternSize(int cut_no) const;

  void Render(const GraphicsObject& go,
              std::optional<ParentObjState> parent = {},
              std::optional<ObjectMask> mask = {}) override;
  int PixelWidth(const GraphicsObject& go) override;
  int PixelHeight(const GraphicsObject& go) override;
  RGBAColour GetPixelColor(Point point, int cut_no) const override;
  std::unique_ptr<GraphicsObjectData> Clone() const override;
  std::shared_ptr<const SDLSurface> CurrentSurface(
      const GraphicsObject& go) const override;
  Rect SrcRect(const GraphicsObject& go) const override;
  Point DstOrigin(const GraphicsObject& go) const override;

 protected:
  bool HitTestAtSource(const GraphicsObject& go,
                       const Point& source_point,
                       bool alpha_test) const override;

 private:
  void UpdateLayers(const GraphicsObject& go) const;

  std::string filename_;
  std::shared_ptr<SDLSurface> atlas_;
  int value_ = 0;
  NumberObjectStyle style_;
  mutable ObjectLayers layers_;
  mutable bool dirty_ = true;
  mutable std::optional<int> cached_pattern_;

 private:
  // The public constructor validates an atlas immediately. Archive loading
  // fills this inert instance, then enforces the same invariant.
  NumberDrawer() = default;
  friend class boost::serialization::access;
  void save(auto& ar, unsigned int) const {
    ar& boost::serialization::base_object<GraphicsObjectData>(*this) &
        filename_ & value_ & style_;
  }
  void load(auto& ar, unsigned int) {
    ar& boost::serialization::base_object<GraphicsObjectData>(*this) &
        filename_ & value_ & style_;
    atlas_ = SerializationContext::Get(ar).LoadSurface(filename_);
    ValidateCheckpoint();
    layers_ = {};
    dirty_ = true;
    cached_pattern_.reset();
  }
  BOOST_SERIALIZATION_SPLIT_MEMBER()
  void ValidateCheckpoint() const;
};

BOOST_CLASS_EXPORT_KEY(NumberDrawer)
