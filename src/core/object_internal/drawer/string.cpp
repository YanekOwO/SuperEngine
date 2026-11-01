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

#include "core/object_internal/drawer/string.hpp"

#include "systems/sdl/sdl_surface.hpp"
#include "utf8.h"
#include "utilities/string_utilities.hpp"

#include <charconv>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {

int Coordinate(int64_t value) {
  if (value < std::numeric_limits<int>::min() ||
      value > std::numeric_limits<int>::max())
    throw std::out_of_range("Siglus string coordinate is out of range");
  return static_cast<int>(value);
}

void ValidateStyle(const StringObjectStyle& style) {
  if (style.size <= 0)
    throw std::invalid_argument("Siglus string font size must be positive");
  if (style.shadow_mode < -1 || style.shadow_mode > 3)
    throw std::invalid_argument("Siglus string shadow mode must be -1 to 3");
}

std::shared_ptr<SDLSurface> Tint(const SDLSurface& glyph, RGBColour color) {
  auto result = glyph.Clone();
  result->Apply(
      [color](RGBAColour pixel) { return RGBAColour(color, pixel.a()); });
  return result;
}

}  // namespace

std::vector<StringObjectGlyph> LayoutStringObject(
    std::string_view text,
    const StringObjectStyle& style) {
  ValidateStyle(style);
  std::vector<StringObjectGlyph> result;
  int x = 0, y = 0, size = style.size, color = style.color;
  int64_t count = 0;
  auto newline = [&] {
    x = 0;
    y = Coordinate(int64_t(y) + size + style.yspace);
    count = 0;
  };
  for (std::size_t pos = 0; pos < text.size();) {
    char32_t cp = 0;
    std::string glyph(1, '\0');
    if (text[pos] == '#') {
      ++pos;
      if (pos < text.size() && text[pos] == '#') {
        cp = '#';
        glyph = "#";
        ++pos;
      } else if (pos < text.size() && text[pos] == 'D') {
        ++pos;
        newline();
        continue;
      } else {
        std::size_t begin = pos;
        // from_chars accepts a minus sign, but not a leading plus sign.
        if (begin < text.size() && text[begin] == '+' &&
            begin + 1 < text.size() && text[begin + 1] >= '0' &&
            text[begin + 1] <= '9')
          ++begin;
        int value = 0;
        const auto [end, ec] = std::from_chars(
            text.data() + begin, text.data() + text.size(), value);
        if (ec == std::errc()) {
          std::size_t next = end - text.data();
          pos = next;
          const auto suffix = text.substr(next);
          bool consumed = true;
          if (suffix.starts_with("C"))
            color = value;
          else if (suffix.starts_with("S"))
            size = value;
          else if (suffix.starts_with("RX")) {
            x = Coordinate(int64_t(x) + value);
            ++next;
          } else if (suffix.starts_with("RY")) {
            y = Coordinate(int64_t(y) + value);
            ++next;
          } else if (suffix.starts_with("X"))
            x = value;
          else if (suffix.starts_with("Y"))
            y = value;
          else
            consumed = false;
          if (consumed) {
            pos = next + 1;
            continue;
          }
        }
      }
    } else {
      const auto begin = text.begin() + pos;
      auto end = begin;
      cp = utf8::next(end, text.end());
      pos = end - text.begin();
      glyph.assign(begin, end);
    }

    const bool halfwidth = IsSiglusHalfwidth(cp);
    const int cells = halfwidth ? 1 : 2;
    if (style.count > 0 && count + cells > int64_t(style.count) * 2)
      newline();
    result.push_back({std::move(glyph), Point(x, y), size, color});
    count += cells;
    x = Coordinate(int64_t(x) + (halfwidth ? (int64_t(size) + 1) / 2 : size) +
                   style.xspace);
  }
  return result;
}

StringDrawer::StringDrawer(std::string text,
                           StringObjectStyle style,
                           StringObjectRenderContext context)
    : text_(std::move(text)), style_(style), context_(std::move(context)) {
  ValidateStyle(style_);
}

void StringDrawer::SetText(std::string text) {
  if (text_ != text) {
    text_ = std::move(text);
    dirty_ = true;
  }
}

void StringDrawer::SetStyle(StringObjectStyle style) {
  ValidateStyle(style);
  if (style_ != style) {
    style_ = style;
    dirty_ = true;
  }
}

std::unique_ptr<GraphicsObjectData> StringDrawer::Clone() const {
  return std::make_unique<StringDrawer>(*this);
}

void StringDrawer::UpdateSurface() const {
  const auto font = context_.font_settings();
  if (!dirty_ && cached_font_ == font)
    return;

  struct RasterGlyph {
    Point position;
    std::shared_ptr<SDLSurface> face;
  };
  std::vector<RasterGlyph> glyphs;
  std::optional<Rect> bounds;
  const int mode =
      style_.shadow_mode < 0 ? font.shadow_mode : style_.shadow_mode;
  for (const auto& glyph : LayoutStringObject(text_, style_)) {
    auto face =
        context_.rasterize(glyph.text, glyph.size, context_.color(glyph.color));
    if (!face || face->GetSize().is_empty())
      continue;
    const int left = mode & 2 ? -1 : 0;
    const int right = mode & 1 ? (mode & 2 ? 3 : 2) : (mode & 2 ? 1 : 0);
    const Point first(Coordinate(int64_t(glyph.position.x()) + left),
                      Coordinate(int64_t(glyph.position.y()) + left));
    const Point last(Coordinate(int64_t(glyph.position.x()) +
                                face->GetSize().width() + right),
                     Coordinate(int64_t(glyph.position.y()) +
                                face->GetSize().height() + right));
    Rect extent(first, last);
    bounds = bounds ? bounds->Union(extent) : extent;
    glyphs.push_back({glyph.position, std::move(face)});
  }

  std::shared_ptr<SDLSurface> surface;
  Point origin;
  if (bounds) {
    origin = Point(Coordinate(-int64_t(bounds->x())),
                   Coordinate(-int64_t(bounds->y())));
    surface = std::make_shared<SDLSurface>(bounds->size());
    surface->Fill(RGBAColour::Clear());
    const auto blit = [&](const SDLSurface& image, Point pos) {
      image.BlitToSurface(*surface, image.GetRect(),
                          Rect(pos + origin, image.GetSize()));
    };
    // Draw whole layers in legacy order, so adjacent glyphs cannot cover
    // another glyph's foreground with their shadows or outlines.
    if (mode & 1) {
      for (const auto& glyph : glyphs) {
        auto shadow = Tint(*glyph.face, context_.color(style_.shadow_color));
        const Point pos = glyph.position + Point(2, 2);
        if (mode & 2)
          for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
              if (x || y)
                blit(*shadow, pos + Point(x, y));
        blit(*shadow, pos);
      }
    }
    if (mode & 2) {
      for (const auto& glyph : glyphs) {
        auto outline = Tint(*glyph.face, context_.color(style_.outline_color));
        for (int y = -1; y <= 1; ++y)
          for (int x = -1; x <= 1; ++x)
            if (x || y)
              blit(*outline, glyph.position + Point(x, y));
      }
    }
    for (const auto& glyph : glyphs)
      blit(*glyph.face, glyph.position);
  }
  surface_ = std::move(surface);
  origin_ = origin;
  cached_font_ = font;
  dirty_ = false;
}

std::shared_ptr<const SDLSurface> StringDrawer::CurrentSurface(
    const GraphicsObject&) const {
  UpdateSurface();
  return surface_;
}

Rect StringDrawer::SrcRect(const GraphicsObject& go) const {
  auto surface = CurrentSurface(go);
  return surface ? surface->GetRect() : Rect();
}

Point StringDrawer::DstOrigin(const GraphicsObject&) const {
  UpdateSurface();
  return origin_;
}
