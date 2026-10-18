// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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

#include <gtest/gtest.h>

#include "core/text_layout.hpp"
#include "utilities/string_utilities.hpp"

class TextLayoutEngineTest : public ::testing::Test {
 protected:
  static constexpr char kinsoku_char = 0x21;

  static constexpr int font_size = 24;
  static constexpr int ruby_size = 0;
  static constexpr int x_spacing = 2, y_spacing = 3;
  static constexpr int x_win_size = 4, y_win_size = 3;

  // - x_window_size_in_chars = 4  => normal_width = 4*(24+2) = 104
  // - extended_width = normal_width + default_font_size = 104 + 24 = 128
  static constexpr int win_height =
      y_win_size * (font_size + y_spacing + ruby_size);
  static constexpr int win_width_normal = x_win_size * (font_size + x_spacing);
  static constexpr int win_width_extended = win_width_normal + font_size;

  TextLayout engine =
      TextLayout(win_height, win_width_normal, win_width_extended);

  void SetUp() override {
    engine.font_size = font_size;
    engine.ruby_font_size = ruby_size;
    engine.x_spacing = x_spacing;
    engine.y_spacing = y_spacing;
  }
};

TEST_F(TextLayoutEngineTest, ChecksNormalWidth) {
  EXPECT_THROW(TextLayout(10, 10, 9), std::invalid_argument);
  EXPECT_NO_THROW(TextLayout(10, 10, 10));
}

TEST_F(TextLayoutEngineTest, WrappingWidth) {
  {
    const int w = engine.WidthFor(static_cast<int>('A'));  // 65
    EXPECT_EQ(w, 13) << "wrapping width for western char should be half width";
  }
  {  // Any codepoint >= 127 should be treated full-width.
    const int w = engine.WidthFor(0x3042 /* 'あ' */);
    EXPECT_EQ(w, 26) << "wrapping width for non-ascii should be full width";
  }
}

TEST_F(TextLayoutEngineTest, LineHeight) {
  // line_height = font_size + y_spacing + ruby_size
  EXPECT_EQ(engine.GetLineHeight(), ruby_size + font_size + y_spacing);
}

TEST_F(TextLayoutEngineTest, IsFull) {
  engine.insertion_y = 0;
  EXPECT_FALSE(engine.IsFull());

  engine.insertion_y = win_height - 1;
  EXPECT_FALSE(engine.IsFull());

  engine.insertion_y = win_height;
  EXPECT_TRUE(engine.IsFull());
}

TEST_F(TextLayoutEngineTest, HardBreak) {
  engine.SetIndent(50);

  // Change current insertion to something else.
  engine.insertion_x = 5;
  engine.insertion_y = 10;

  engine.HardBreak();
  EXPECT_EQ(engine.insertion_x, 50) << "should jump back to indent_pixels";
  EXPECT_EQ(engine.insertion_y, 10 + engine.GetLineHeight());
}

TEST_F(TextLayoutEngineTest, LineBreak_KinsokuSpace) {
  constexpr int space = 0x20;
  const int cw = engine.WidthFor(space);

  engine.insertion_x = win_width_extended - cw;

  EXPECT_FALSE(engine.MustLineBreakFor(space, /*rest=*/std::string_view{}));
}

TEST_F(TextLayoutEngineTest, LineBreak_ImmediateOverflow) {
  // Non-kinsoku ASCII 'A'
  const int A = static_cast<int>('A');
  const int cw = engine.WidthFor(A);  // 13
  engine.insertion_x = win_width_normal - cw + 1;

  EXPECT_TRUE(engine.MustLineBreakFor(A, /*rest=*/std::string_view{}));
}

TEST_F(TextLayoutEngineTest, LineBreak_PeekOverKinsoku) {
  const int A = static_cast<int>('A');
  const int cw = engine.WidthFor(A);  // 13
  engine.insertion_x = win_width_normal - cw;
  // After putting 'A', final_x == normal_width == 104 (fits).
  // Two more spaces (kinsoku) add 13 + 13 = 26, so 104 + 26 = 130 >
  // extended(128) → break.

  EXPECT_TRUE(IsKinsoku(kinsoku_char));
  std::string rest(2, kinsoku_char);
  EXPECT_TRUE(engine.MustLineBreakFor(A, rest))
      << "Current char fits normally, but following kinsoku (spaces) push "
         "beyond extended width -> should line break";
}

TEST_F(TextLayoutEngineTest, LineBreak_PeekWithinExtended) {
  const int A = static_cast<int>('A');
  const int cw = engine.WidthFor(A);  // 13
  engine.insertion_x = win_width_normal - cw;

  std::string rest(1, kinsoku_char);
  EXPECT_FALSE(engine.MustLineBreakFor(A, rest))
      << "Current char fits normally, and a single space keeps us within "
         "extended width -> no break";
}

TEST_F(TextLayoutEngineTest, RubySpanCoversBaseTextAndIsConsumed) {
  engine.font_size = 35;
  engine.ruby_font_size = 15;
  engine.x_spacing = 0;
  engine.Reset();

  engine.RubyBegin();
  ASSERT_TRUE(engine.PlaceCharacter(0x6319, std::nullopt, {}));  // 挙
  ASSERT_TRUE(engine.PlaceCharacter(0x63aa, std::nullopt, {}));  // 措

  const std::optional<Rect> area = engine.PlaceRubyText("きょそ");
  ASSERT_TRUE(area);
  EXPECT_EQ(*area, Rect(Point(0, 0), Point(70, 15)));
  EXPECT_FALSE(engine.ruby_begin_x.has_value());
}

TEST_F(TextLayoutEngineTest, EmptyRubyConsumesSpanWithoutPlacement) {
  engine.ruby_font_size = 10;
  engine.reserve_ruby_space = false;

  for (int spacing : {0, 2}) {
    for (bool has_base_text : {false, true}) {
      SCOPED_TRACE(::testing::Message() << "spacing=" << spacing
                                        << ", has_base_text=" << has_base_text);
      engine.x_spacing = spacing;
      engine.Reset();
      engine.RubyBegin();
      if (has_base_text) {
        ASSERT_TRUE(engine.PlaceCharacter('x', std::nullopt, {}));
      }

      const Point insertion = engine.GetInsertionPoint();
      EXPECT_FALSE(engine.PlaceRubyText(""));
      EXPECT_FALSE(engine.ruby_begin_x.has_value());
      EXPECT_EQ(engine.GetInsertionPoint(), insertion);

      engine.RubyBegin();
      ASSERT_TRUE(engine.PlaceCharacter('x', std::nullopt, {}));
      const std::optional<Rect> area = engine.PlaceRubyText("ruby");
      ASSERT_TRUE(area);
      EXPECT_EQ(area->origin(),
                Point(insertion.x(), insertion.y() - engine.ruby_font_size));
      EXPECT_GT(area->size().width(), 0);
      EXPECT_FALSE(engine.ruby_begin_x.has_value());
    }
  }
}
