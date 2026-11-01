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

#include <cstddef>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

// Message-text geometry, independent of fonts, surfaces and reveal timing.
// Copies include all state needed to continue layout or replay a page.
class TextLayout {
 public:
  enum class Engine { RealLive, Siglus };
  enum class NameMode { Inline, SeparateWindow, Disable };

  struct Settings {
    Engine engine = Engine::RealLive;
    int font_size = 25;
    int ruby_font_size = 0;
    int x_spacing = 0;
    int y_spacing = 0;
    int ruby_spacing = 0;
  };

  // Supplies bare font advance; layout adds spacing for proportional ASCII.
  struct FontMetrics {
    bool monospace = true;
    std::function<int(int)> advance;
  };

  struct GlyphPlacement {
    std::size_t byte_offset;
    std::size_t byte_count;
    Point position;
    std::optional<int> baseline_from_top;
  };

  struct Result {
    std::vector<GlyphPlacement> glyphs;
    std::size_t consumed_bytes = 0;
  };

  TextLayout() = default;
  TextLayout(Settings settings, Size normal_size, int extended_width);
  static TextLayout FromCells(Settings settings, Size cell_count);

  // All accepted positions are determined before returning. Source beyond
  // byte_limit remains available for lookahead, preserving partial replay.
  // final_lookahead is used only at the last source character.
  [[nodiscard]] Result LayoutText(
      std::string_view source,
      const FontMetrics& metrics,
      std::string_view final_lookahead = {},
      std::size_t byte_limit = std::string_view::npos);
  [[nodiscard]] Result LayoutName(std::string_view source,
                                  const FontMetrics& metrics,
                                  std::string_view next = {});
  void NameWithoutDisplay();

  void SetNameMode(NameMode mode) { name_mode_ = mode; }
  NameMode GetNameMode() const { return name_mode_; }
  void SetFontSize(int size);
  int GetFontSize() const { return settings_.font_size; }
  int GetRubyFontSize() const { return settings_.ruby_font_size; }
  int GetXSpacing() const { return settings_.x_spacing; }
  int GetYSpacing() const { return settings_.y_spacing; }

  void SetInsertionPoint(Point point);
  inline Point GetInsertionPoint() const { return insertion_; }
  inline void SetIndent(int indent) { indent_pixels_ = indent; }
  inline void SetIndentation() { SetIndent(insertion_.x()); }
  inline void ClearIndentation() { SetIndent(0); }
  inline int GetIndentation() const { return indent_pixels_; }

  inline void SetGlyphRenderOffset(Point offset) {
    glyph_render_offset_ = offset;
  }
  inline Point GetGlyphRenderOffset() const { return glyph_render_offset_; }

  void RubyBegin();
  // Native-size raster rectangle centered over a same-line base span.
  // Empty annotations close the span without placement.
  [[nodiscard]] std::optional<Rect> RubyEnd(std::string_view text,
                                            Size rendered_size);
  bool IsRubyActive() const { return ruby_.has_value(); }

  // Clear cursor, indentation and ruby, retaining style and name-following
  // state as the window-clear operation historically does.
  void Reset();
  void HardBreak();
  // Default style/geometry, at the previous cursor and current name mode.
  void NextMessage(const TextLayout& defaults);

  int GetLineHeight() const;
  int GetRubyTopPadding() const;
  bool IsFull() const { return insertion_.y() >= normal_size_.height(); }
  Size GetNormalSize() const { return normal_size_; }
  Size GetExtendedSize() const {
    return Size(extended_width_, normal_size_.height());
  }

 private:
  struct RubySpan {
    Point begin;
    bool crossed_line = false;
    bool has_base_text = false;
  };

  int AdvanceFor(int codepoint, const FontMetrics& metrics) const;

  Settings settings_;
  Size normal_size_;
  int extended_width_ = 0;
  Point insertion_;
  int indent_pixels_ = 0;
  Point glyph_render_offset_;
  NameMode name_mode_ = NameMode::Inline;
  bool last_token_was_name_ = false;
  bool splitting_roman_run_ = false;
  int last_trailing_spacing_ = 0;
  std::optional<RubySpan> ruby_;
};
