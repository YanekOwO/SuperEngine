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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <utility>

namespace {

// Prefix extents required by a following kinsoku/Roman run. Summaries avoid
// rescanning and remeasuring the remaining word for every character.
struct Constraints {
  std::optional<std::int64_t> normal;
  std::optional<std::int64_t> extended;
};

void Include(std::optional<std::int64_t>& extent, std::int64_t value) {
  extent = extent ? std::max(*extent, value) : value;
}

struct Character {
  int codepoint;
  std::size_t offset;
  std::size_t bytes;
  int advance;
  int spacing;
  Constraints following_run;
  Constraints following_kinsoku;
  std::int64_t roman_advance = 0;
  int roman_trailing_spacing = 0;
};

}  // namespace

TextLayout::TextLayout(Settings settings, Size normal_size, int extended_width)
    : settings_(settings),
      normal_size_(normal_size),
      extended_width_(extended_width) {
  if (extended_width_ < normal_size_.width()) {
    throw std::invalid_argument(
        std::format("extended width must not be less than normal width ({}<{})",
                    extended_width_, normal_size_.width()));
  }
  Reset();
}

TextLayout TextLayout::FromCells(Settings settings, Size cell_count) {
  const auto extent = [&](int count, int spacing, int ruby) {
    count = std::max(count, 0);
    if (settings.engine == Engine::Siglus) {
      return count == 0
                 ? 0
                 : std::max(count * settings.font_size + (count - 1) * spacing,
                            0);
    }
    return count * std::max(settings.font_size + spacing + ruby, 0);
  };
  const int width = extent(cell_count.width(), settings.x_spacing, 0);
  const int height =
      extent(cell_count.height(), settings.y_spacing, settings.ruby_font_size);
  return TextLayout(settings, Size(width, height),
                    width + std::max(settings.font_size, 0));
}

int TextLayout::AdvanceFor(int codepoint, const FontMetrics& metrics) const {
  if (codepoint < 127) {
    if (!metrics.monospace && metrics.advance)
      return metrics.advance(codepoint) + settings_.x_spacing;
    return static_cast<int>(
        std::floor((settings_.font_size + settings_.x_spacing) / 2.0));
  }
  return settings_.font_size + settings_.x_spacing;
}

TextLayout::Result TextLayout::LayoutText(std::string_view source,
                                          const FontMetrics& metrics,
                                          std::string_view final_lookahead,
                                          std::size_t byte_limit) {
  Result result;
  if (source.empty() || byte_limit == 0 || IsFull())
    return result;

  const bool exclude_trailing_spacing = settings_.engine == Engine::Siglus;
  const auto decode = [&](std::string_view text) {
    std::vector<Character> characters;
    auto current = text.begin();
    while (current != text.end()) {
      const auto begin = current;
      const int codepoint = utf8::next(current, text.end());
      const int advance = AdvanceFor(codepoint, metrics);
      const int spacing =
          codepoint < 127 && (metrics.monospace || !metrics.advance)
              ? advance -
                    static_cast<int>(std::floor(settings_.font_size / 2.0))
              : settings_.x_spacing;
      characters.push_back({codepoint,
                            static_cast<std::size_t>(begin - text.begin()),
                            static_cast<std::size_t>(current - begin),
                            advance,
                            spacing,
                            {},
                            {}});
    }

    Constraints run, kinsoku;
    std::int64_t roman_advance = 0;
    int roman_trailing_spacing = 0;
    bool next_is_roman = false;
    for (auto it = characters.rbegin(); it != characters.rend(); ++it) {
      const bool is_kinsoku = IsKinsoku(it->codepoint);
      const bool is_roman = IsWrappingRomanCharacter(it->codepoint);
      const auto prepend = [&](Constraints tail, bool kinsoku_character) {
        if (tail.normal)
          *tail.normal += it->advance;
        if (tail.extended)
          *tail.extended += it->advance;
        Include(kinsoku_character ? tail.extended : tail.normal,
                std::int64_t(it->advance) -
                    (exclude_trailing_spacing ? it->spacing : 0));
        return tail;
      };
      run = is_kinsoku || is_roman ? prepend(run, is_kinsoku) : Constraints{};
      kinsoku = is_kinsoku ? prepend(kinsoku, true) : Constraints{};
      roman_advance = is_roman ? it->advance + roman_advance : 0;
      roman_trailing_spacing =
          is_roman ? (next_is_roman ? roman_trailing_spacing : it->spacing) : 0;
      next_is_roman = is_roman;
      it->following_run = run;
      it->following_kinsoku = kinsoku;
      it->roman_advance = roman_advance;
      it->roman_trailing_spacing = roman_trailing_spacing;
    }
    return characters;
  };

  const auto characters = decode(source);
  const auto lookahead = decode(final_lookahead);
  const auto overflows = [&](const Constraints& constraints, int x,
                             int advance) {
    const std::int64_t end = std::int64_t(x) + advance;
    return (constraints.normal &&
            end + *constraints.normal > normal_size_.width()) ||
           (constraints.extended &&
            end + *constraints.extended > extended_width_);
  };

  for (std::size_t i = 0; i < characters.size(); ++i) {
    const auto& character = characters[i];
    if (character.offset + character.bytes > byte_limit)
      break;
    const Character* next = i + 1 < characters.size()
                                ? &characters[i + 1]
                                : (lookahead.empty() ? nullptr : &lookahead[0]);
    const bool is_roman = IsWrappingRomanCharacter(character.codepoint);
    const bool is_kinsoku =
        character.codepoint == 0x20 || IsKinsoku(character.codepoint);
    bool splitting = is_roman && splitting_roman_run_;
    bool break_line = false;

    // Move an overlong word to a fresh line at most once, then split it by
    // characters. Keep the decision across the single-character adapter.
    const std::int64_t roman_width =
        std::int64_t(character.advance) +
        (next && IsWrappingRomanCharacter(next->codepoint) ? next->roman_advance
                                                           : 0) -
        (exclude_trailing_spacing
             ? (next && IsWrappingRomanCharacter(next->codepoint)
                    ? next->roman_trailing_spacing
                    : character.spacing)
             : 0);
    if (is_roman && !splitting &&
        std::int64_t(indent_pixels_) + roman_width > normal_size_.width()) {
      splitting = true;
      break_line = insertion_.x() != indent_pixels_;
    }

    const std::int64_t occupied_end =
        std::int64_t(insertion_.x()) + character.advance -
        (exclude_trailing_spacing ? character.spacing : 0);
    const int own_limit = is_kinsoku ? extended_width_ : normal_size_.width();
    if (occupied_end > own_limit) {
      break_line = true;
    } else if (!is_kinsoku && next) {
      const auto& constraints =
          splitting ? next->following_kinsoku : next->following_run;
      break_line = break_line ||
                   overflows(constraints, insertion_.x(), character.advance);
    }

    // An impossible run on an already fresh line must not add a blank row.
    // Likewise place a single oversized glyph once to guarantee progress.
    break_line = break_line && insertion_.x() != indent_pixels_;
    const Point position =
        break_line ? Point(indent_pixels_, insertion_.y() + GetLineHeight())
                   : insertion_;
    if (position.y() >= normal_size_.height())
      break;

    std::optional<int> baseline;
    if (settings_.engine == Engine::Siglus)
      baseline = static_cast<int>(std::round(settings_.font_size * 0.86));
    result.glyphs.push_back(
        {character.offset, character.bytes,
         position + glyph_render_offset_ + Point(0, GetRubyTopPadding()),
         baseline});

    if (ruby_) {
      ruby_->crossed_line = ruby_->crossed_line || break_line;
      ruby_->has_base_text = true;
    }
    insertion_ = Point(position.x() + character.advance, position.y());
    if (last_token_was_name_ && name_mode_ != NameMode::SeparateWindow &&
        IsOpeningQuoteMark(character.codepoint))
      SetIndentation();
    last_token_was_name_ = false;
    splitting_roman_run_ = splitting;
    last_trailing_spacing_ = character.spacing;
    result.consumed_bytes = character.offset + character.bytes;
  }
  return result;
}

TextLayout::Result TextLayout::LayoutName(std::string_view source,
                                          const FontMetrics& metrics,
                                          std::string_view next) {
  Result result;
  if (name_mode_ == NameMode::Inline) {
    result = LayoutText(source, metrics, next);
    SetIndentation();
  }
  NameWithoutDisplay();
  return result;
}

void TextLayout::NameWithoutDisplay() {
  last_token_was_name_ = true;
  splitting_roman_run_ = false;
}

void TextLayout::SetFontSize(int size) {
  settings_.font_size = size;
  splitting_roman_run_ = false;
}

void TextLayout::SetInsertionPoint(Point point) {
  if (ruby_ && point.y() != insertion_.y())
    ruby_->crossed_line = true;
  insertion_ = point;
  splitting_roman_run_ = false;
}

void TextLayout::RubyBegin() { ruby_ = RubySpan{insertion_}; }

std::optional<Rect> TextLayout::RubyEnd(std::string_view text,
                                        Size rendered_size) {
  static DomainLogger logger("TextLayout");
  last_token_was_name_ = false;
  const auto ruby = std::exchange(ruby_, std::nullopt);
  if (text.empty())
    return std::nullopt;
  if (!ruby) {
    logger(Severity::Warn) << "Cannot place ruby '" << text
                           << "': ruby begin not set";
    return std::nullopt;
  }
  const int end_x = insertion_.x() - last_trailing_spacing_;
  if (ruby->crossed_line || ruby->begin.y() != insertion_.y() ||
      ruby->begin.x() > end_x) {
    logger(Severity::Error)
        << "We don't handle ruby across line breaks yet! text = " << text;
    return std::nullopt;
  }
  if (!ruby->has_base_text)
    return std::nullopt;

  const int x =
      ruby->begin.x() + (end_x - ruby->begin.x() - rendered_size.width()) / 2;
  const int y = insertion_.y() - settings_.ruby_font_size -
                settings_.ruby_spacing + GetRubyTopPadding();
  return Rect(Point(x, y), rendered_size);
}

void TextLayout::Reset() {
  insertion_ = Point(
      0, settings_.engine == Engine::RealLive ? settings_.ruby_font_size : 0);
  indent_pixels_ = 0;
  ruby_.reset();
  splitting_roman_run_ = false;
  last_trailing_spacing_ = 0;
}

void TextLayout::HardBreak() {
  insertion_ = Point(indent_pixels_, insertion_.y() + GetLineHeight());
  if (ruby_)
    ruby_->crossed_line = true;
  splitting_roman_run_ = false;
}

void TextLayout::NextMessage(const TextLayout& defaults) {
  const Point position = insertion_;
  const NameMode mode = name_mode_;
  *this = defaults;
  insertion_ = position;
  name_mode_ = mode;
  indent_pixels_ = 0;
  ruby_.reset();
  glyph_render_offset_ = Point();
  last_token_was_name_ = false;
  splitting_roman_run_ = false;
  last_trailing_spacing_ = 0;
}

int TextLayout::GetLineHeight() const {
  return settings_.font_size + settings_.y_spacing +
         (settings_.engine == Engine::RealLive ? settings_.ruby_font_size : 0);
}

int TextLayout::GetRubyTopPadding() const {
  return settings_.engine == Engine::Siglus
             ? settings_.ruby_font_size + settings_.ruby_spacing
             : 0;
}
