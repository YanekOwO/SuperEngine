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
#include "core/serialization_context.hpp"
#include "utilities/serialization.hpp"

#include "core/colour.hpp"
#include "core/object_internal/objdrawer.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct StringObjectStyle {
  int size = 12;
  int xspace = 0, yspace = 0;
  int count = 0;
  int color = 0, shadow_color = 1, outline_color = 1;
  int shadow_mode = -1;

  bool operator==(const StringObjectStyle&) const = default;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) {
    ar & size & xspace & yspace & count & color & shadow_color & outline_color &
        shadow_mode;
  }
};

struct StringObjectGlyph {
  std::string text;
  Point position;
  int size;
  int color;
};

// Horizontal string-object syntax differs from RealLive's object text syntax.
std::vector<StringObjectGlyph> LayoutStringObject(
    std::string_view text,
    const StringObjectStyle& style);

struct StringObjectFontSettings {
  std::string name;
  bool bold = false;
  int shadow_mode = 2;

  bool operator==(const StringObjectFontSettings&) const = default;
};

// Borrow runtime font services without requiring a GUI for layout/drawer tests.
struct StringObjectRenderContext {
  std::function<std::shared_ptr<SDLSurface>(const std::string&, int, RGBColour)>
      rasterize;
  std::function<RGBColour(int)> color;
  std::function<StringObjectFontSettings()> font_settings;
};

class StringDrawer final : public GraphicsObjectData {
 public:
  StringDrawer(std::string text,
               StringObjectStyle style,
               StringObjectRenderContext context);

  const std::string& text() const { return text_; }
  const StringObjectStyle& style() const { return style_; }
  void SetText(std::string text);
  void SetStyle(StringObjectStyle style);

  // String objects have no script-visible picture dimensions.
  int PixelWidth(const GraphicsObject&) override { return 0; }
  int PixelHeight(const GraphicsObject&) override { return 0; }

  std::unique_ptr<GraphicsObjectData> Clone() const override;
  std::shared_ptr<const SDLSurface> CurrentSurface(
      const GraphicsObject&) const override;
  Rect SrcRect(const GraphicsObject&) const override;
  Point DstOrigin(const GraphicsObject&) const override;

 private:
  void UpdateSurface() const;

  std::string text_;
  StringObjectStyle style_;
  StringObjectRenderContext context_;
  mutable bool dirty_ = true;
  mutable std::optional<StringObjectFontSettings> cached_font_;
  mutable std::shared_ptr<SDLSurface> surface_;
  mutable Point origin_;

 private:
  // Only archive construction may create the inert, unbound drawer.
  StringDrawer() = default;
  friend class boost::serialization::access;
  void save(auto& ar, unsigned int) const {
    ar& boost::serialization::base_object<GraphicsObjectData>(*this) & text_ &
        style_;
  }
  void load(auto& ar, unsigned int) {
    ar& boost::serialization::base_object<GraphicsObjectData>(*this) & text_ &
        style_;
    context_ = SerializationContext::Get(ar).StringRenderer();
    dirty_ = true;
    cached_font_.reset();
    surface_.reset();
  }
  BOOST_SERIALIZATION_SPLIT_MEMBER()
};

BOOST_CLASS_EXPORT_KEY(StringDrawer)
