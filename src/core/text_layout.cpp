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

#include "core/text_layout.hpp"
#include "log/domain_logger.hpp"
#include "utilities/string_utilities.hpp"

#include <cmath>
#include <format>
#include <stdexcept>
#include <utility>

TextLayout::TextLayout(int window_height,
                       int window_width_normal,
                       int window_width_extended)
    : window_height_(window_height),
      window_width_normal_(window_width_normal),
      window_width_extended_(window_width_extended) {
  if (window_width_extended_ < window_width_normal_) {
    throw std::invalid_argument(
        std::format("extended width must not be less than normal width ({}<{})",
                    window_width_normal_, window_width_extended_));
  }
}

void TextLayout::Reset() {
  insertion_x = 0;
  insertion_y = reserve_ruby_space ? ruby_font_size : 0;
  indent_pixels = 0;
  ruby_begin_x.reset();
}

int TextLayout::WidthFor(int codepoint) const {
  if (codepoint < 127) {
    return static_cast<int>(std::floor((font_size + x_spacing) / 2.0f));
  }
  return font_size + x_spacing;
}

void TextLayout::SetIndent(int indent) { indent_pixels = indent; }

std::optional<Point> TextLayout::PlaceCharacter(int cur_codepoint,
                                                std::optional<int> width,
                                                std::string_view peek) {
  if (!width.has_value())
    width = WidthFor(cur_codepoint);

  if (MustLineBreakFor(cur_codepoint, peek))
    HardBreak();

  if (IsFull())
    return std::nullopt;

  Point result = GetInsertionPoint();
  insertion_x += *width;
  return result;
}

void TextLayout::RubyBegin() { ruby_begin_x = insertion_x; }
void TextLayout::RubyEnd() { ruby_begin_x.reset(); }
std::optional<Rect> TextLayout::PlaceRubyText(std::string_view ruby) {
  static DomainLogger logger("TextLayout");

  if (!ruby_begin_x.has_value()) {
    logger(Severity::Warn) << "Cannot place ruby '" << ruby
                           << "': ruby begin not set";
    return std::nullopt;
  }

  int ruby_end = insertion_x - x_spacing;
  int ruby_begin = std::exchange(ruby_begin_x, std::nullopt).value();
  if (ruby_begin > ruby_end) {
    // TODO: this is not a reliable way of detecting line break
    logger(Severity::Error)
        << "We don't handle ruby across line breaks yet! text = " << ruby;
    return std::nullopt;
  }

  Point topleft(ruby_begin, insertion_y - ruby_font_size - ruby_spacing);
  Point downright(ruby_end, insertion_y - ruby_spacing);
  return Rect(topleft, downright);
}

bool TextLayout::MustLineBreakFor(int cur_codepoint,
                                  std::string_view peek) const {
  const int char_width = WidthFor(cur_codepoint);
  const bool cur_is_kinsoku = cur_codepoint == 0x20 || IsKinsoku(cur_codepoint);
  // space(0x20) is not a kinsoku character, but if we can squeeze the space
  // into end of current line, prefer not to put them at the beginning of the
  // next line.

  // If this is a kinsoku (or space) and it still fits when squeezed, do not
  // break.
  if (cur_is_kinsoku && (insertion_x + char_width <= window_width_extended_)) {
    return false;
  }

  // If it does not fit under normal rules, break immediately.
  if (insertion_x + char_width > window_width_normal_) {
    return true;
  }

  // Otherwise, if it fits now but the following run of kinsoku or wrapping
  // roman would overflow, then decide to break before this character.
  if (!cur_is_kinsoku && !peek.empty()) {
    int final_x = insertion_x + char_width;

    auto it = peek.begin();
    auto end = peek.end();
    while (it != end) {
      int point = utf8::next(it, end);

      if (IsKinsoku(point)) {
        final_x += WidthFor(point);
        if (final_x > window_width_extended_) {
          return true;
        }
      } else if (IsWrappingRomanCharacter(point)) {
        final_x += WidthFor(point);
        if (final_x > window_width_normal_) {
          return true;
        }
      } else {
        break;  // stop on first non-kinsoku/non-wrapping-roman
      }
    }
  }

  return false;
}

void TextLayout::HardBreak() {
  insertion_x = indent_pixels;
  insertion_y += GetLineHeight();
}
