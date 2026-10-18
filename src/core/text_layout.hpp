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

#include "core/rect.hpp"

#include <optional>
#include <string_view>

class TextLayout {
 public:
  int font_size = 25, ruby_font_size = 0;

  // RealLive reserves a ruby band within every line. Siglus draws ruby above
  // the character cell without changing the first row or the line advance.
  bool reserve_ruby_space = true;
  int ruby_spacing = 0;

  int x_spacing = 0, y_spacing = 0;

  // Mutable runtime layout state
  //  Insertion point (relative to text area origin)
  int insertion_x = 0, insertion_y = 0;

  int indent_pixels = 0;

  std::optional<int> ruby_begin_x;

 public:
  TextLayout(int window_height,
             int window_width_normal,
             int window_width_extended);

  void Reset();

  [[nodiscard]] std::optional<Point> PlaceCharacter(int cur_codepoint,
                                                    std::optional<int> width,
                                                    std::string_view peek);

  void RubyBegin();
  void RubyEnd();
  [[nodiscard]] std::optional<Rect> PlaceRubyText(std::string_view utf8_ruby);

  int WidthFor(int codepoint) const;

  // Original MustLineBreak() logic, including:
  // - Kinsoku/space squeeze allowance up to extended_width
  // - Lookahead over subsequent kinsoku/roman-wrapping characters
  bool MustLineBreakFor(int cur_codepoint, std::string_view peek) const;

  // Advance to next line
  void HardBreak();

  // Indentation snapshot from current insertion/wrapping X
  void SetIndent(int indent);

  [[nodiscard]] inline int GetLineHeight() const {
    return font_size + y_spacing + (reserve_ruby_space ? ruby_font_size : 0);
  }

  // Extra surface storage above the logical text area for Siglus ruby.
  [[nodiscard]] inline int GetRubyTopPadding() const {
    return reserve_ruby_space ? 0 : ruby_font_size + ruby_spacing;
  }

  [[nodiscard]] inline bool IsFull() const {
    return insertion_y >= window_height_;
  }

  inline Size GetNormalSize() const {
    return Size(window_width_normal_, window_height_);
  }
  inline Size GetExtendedSize() const {
    return Size(window_width_extended_, window_height_);
  }

  inline Point GetInsertionPoint() const {
    return Point(insertion_x, insertion_y);
  }

 private:
  int window_height_, window_width_normal_, window_width_extended_;
};
