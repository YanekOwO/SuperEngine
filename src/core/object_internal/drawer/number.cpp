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

#include "core/object_internal/drawer/number.hpp"

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

#include "core/object.hpp"
#include "systems/sdl/sdl_surface.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

static void ValidateStyle(const NumberObjectStyle& style) {
  if (style.digits < 0 || style.digits > 16)
    throw std::invalid_argument(
        "Number object digit count must be 0 through 16");
}

std::vector<NumberObjectGlyph> LayoutNumberObject(
    int value,
    const NumberObjectStyle& style,
    int digit_width,
    int pattern_offset) {
  ValidateStyle(style);
  const std::int64_t magnitude =
      value < 0 ? -static_cast<std::int64_t>(value) : value;
  const auto digits = std::to_string(magnitude);
  const bool show_sign = style.sign || value < 0;
  int number_pos = std::max(style.digits - static_cast<int>(digits.size()), 0);
  int sign_pos = 0;
  if (show_sign) {
    if (style.pack_sign && !style.zero)
      sign_pos = std::max(number_pos - 1, 0);
    number_pos = std::max(number_pos, sign_pos + 1);
  }

  std::array<std::optional<int>, 16> slots;
  if (style.zero) {
    for (int i = 0; i < style.digits; ++i)
      slots[i] = 0;
  }
  for (std::size_t i = 0; i < digits.size(); ++i)
    slots[number_pos + i] = digits[i] - '0';
  if (show_sign)
    slots[sign_pos] = value < 0 ? 10 : value > 0 ? 11 : 12;

  const std::int64_t advance = static_cast<std::int64_t>(style.space) +
                               (style.space_mode == 0 ? digit_width : 0);
  std::vector<NumberObjectGlyph> glyphs;
  for (std::size_t i = 0; i < slots.size(); ++i) {
    if (!slots[i])
      continue;
    auto checked = [](std::int64_t value) -> int {
      if (value < -std::numeric_limits<int>::max() ||
          value > std::numeric_limits<int>::max())
        throw std::out_of_range("Number object coordinate overflow");
      return static_cast<int>(value);
    };
    glyphs.push_back(
        {checked(static_cast<std::int64_t>(pattern_offset) + *slots[i]),
         checked(static_cast<std::int64_t>(i) * advance)});
  }
  return glyphs;
}

NumberDrawer::NumberDrawer(std::string filename,
                           std::shared_ptr<SDLSurface> atlas)
    : filename_(std::move(filename)), atlas_(std::move(atlas)) {
  if (!atlas_ || atlas_->GetNumPatterns() == 0)
    throw std::invalid_argument("Number object requires a picture atlas");
}

void NumberDrawer::SetNumber(int value) {
  if (value_ != value) {
    value_ = value;
    dirty_ = true;
  }
}

void NumberDrawer::SetStyle(NumberObjectStyle style) {
  ValidateStyle(style);
  if (style_ != style) {
    style_ = style;
    dirty_ = true;
  }
}

Size NumberDrawer::PatternSize(int cut_no) const {
  if (cut_no < 0 || cut_no >= atlas_->GetNumPatterns())
    return {};
  return atlas_->GetPattern(cut_no).rect.size();
}

void NumberDrawer::UpdateLayers(const GraphicsObject& go) const {
  const int pattern = go.Param().GetPattNo();
  if (!dirty_ && cached_pattern_ == pattern)
    return;

  std::vector<ObjectLayer> layers;
  for (auto glyph :
       LayoutNumberObject(value_, style_, PatternSize(0).width(), pattern)) {
    if (glyph.pattern < 0 || glyph.pattern >= atlas_->GetNumPatterns())
      continue;
    const auto& cut = atlas_->GetPattern(glyph.pattern);
    if (cut.rect.is_degenerate())
      continue;
    layers.push_back({.surface = atlas_,
                      .offset = Point(glyph.x, 0),
                      .cut_no = glyph.pattern,
                      .filename = filename_});
  }
  layers_.SetLayers(std::move(layers));
  cached_pattern_ = pattern;
  dirty_ = false;
}

void NumberDrawer::Render(const GraphicsObject& go,
                          std::optional<ParentObjState> parent,
                          std::optional<ObjectMask> mask) {
  UpdateLayers(go);
  layers_.Render(*this, go, std::move(parent), std::move(mask));
}

int NumberDrawer::PixelWidth(const GraphicsObject& go) {
  UpdateLayers(go);
  return int(go.Param().GetWidthScaleFactor() * layers_.Bounds().width());
}

int NumberDrawer::PixelHeight(const GraphicsObject& go) {
  UpdateLayers(go);
  return int(go.Param().GetHeightScaleFactor() * layers_.Bounds().height());
}

RGBAColour NumberDrawer::GetPixelColor(Point point, int cut_no) const {
  return atlas_->GetPixelColor(point, cut_no);
}

std::unique_ptr<GraphicsObjectData> NumberDrawer::Clone() const {
  return std::make_unique<NumberDrawer>(*this);
}

std::shared_ptr<const SDLSurface> NumberDrawer::CurrentSurface(
    const GraphicsObject& go) const {
  return {};
}

Rect NumberDrawer::SrcRect(const GraphicsObject& go) const {
  UpdateLayers(go);
  return Rect(Point(), layers_.Bounds().size());
}

Point NumberDrawer::DstOrigin(const GraphicsObject& go) const {
  UpdateLayers(go);
  return Point(-layers_.Bounds().x(), -layers_.Bounds().y());
}

bool NumberDrawer::HitTestAtSource(const GraphicsObject& go,
                                   const Point& source_point,
                                   bool alpha_test) const {
  UpdateLayers(go);
  return layers_.HitTest(source_point, alpha_test);
}

void NumberDrawer::ValidateCheckpoint() const {
  if (!atlas_ || atlas_->GetNumPatterns() == 0)
    throw std::runtime_error("Number object requires a picture atlas");
  ValidateStyle(style_);
}

BOOST_CLASS_EXPORT_IMPLEMENT(NumberDrawer)
