// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
// Copyright (C) 2025 Serina Sakurai
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

#include "core/colour.hpp"
#include "core/rect.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

class SDLSurface;

struct IFont {
  virtual ~IFont() = default;
  virtual bool IsMonospace() const { return false; }
  virtual int GetCharWidth(uint16_t codepoint) = 0;
  virtual int GetSize() const = 0;
};

struct FontFace {
  std::shared_ptr<IFont> font;
  bool is_italic = false;
  bool is_shadow = true;
  bool is_futidoru = false;
};

class ITextSystem {
 public:
  virtual ~ITextSystem() = default;

  virtual void InitSystem() = 0;
  virtual void QuitSystem() = 0;

  virtual std::shared_ptr<IFont> GetFont(const std::filesystem::path& font_file,
                                         int font_size,
                                         bool bold = false,
                                         int face_index = 0) = 0;

  virtual Size RenderGlyphOnto(const std::string& text,
                               FontFace font,
                               RGBColour font_color,
                               std::optional<RGBColour> shadow_color,
                               Point insertion,
                               std::shared_ptr<SDLSurface> dst) = 0;

  virtual std::shared_ptr<SDLSurface> RenderText(
      const std::string& text /* utf8 */,
      FontFace font,
      RGBColour color) = 0;
};
