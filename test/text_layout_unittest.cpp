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

#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<Point> Positions(const TextLayout::Result& result) {
  std::vector<Point> positions;
  for (const auto& glyph : result.glyphs)
    positions.push_back(glyph.position);
  return positions;
}

TextLayout::FontMetrics Proportional(int advance) {
  return {.monospace = false, .advance = [advance](int) { return advance; }};
}

}  // namespace

class SiglusMessageFitTest : public ::testing::Test {
 protected:
  TextLayout layout{
      {.engine = TextLayout::Engine::Siglus, .font_size = 20, .y_spacing = 5},
      Size(100, 100),
      120};
};

TEST_F(SiglusMessageFitTest, ReachingTheReserveCountsAsOverflow) {
  // A cursor immediately above the reserve fits; exact equality must trigger
  // a page boundary according to the legacy conservative comparison.
  layout.SetInsertionPoint(Point(0, 74));
  EXPECT_TRUE(layout.CanFitMessage(25, false));
  layout.SetInsertionPoint(Point(0, 75));
  EXPECT_FALSE(layout.CanFitMessage(25, false));
}

TEST_F(SiglusMessageFitTest, ProjectsTheNextParagraphBeforeChecking) {
  // The current line fits but a projected 25-pixel newline reaches the reserve.
  layout.SetInsertionPoint(Point(0, 50));
  EXPECT_TRUE(layout.CanFitMessage(25, false));
  EXPECT_FALSE(layout.CanFitMessage(25, true));
}

TEST_F(SiglusMessageFitTest, ProjectsAnAlreadyOverrunLine) {
  // Ending at the extended-width threshold does not wrap; crossing it adds a
  // line before the vertical reserve comparison.
  layout.SetInsertionPoint(Point(100, 50));
  EXPECT_TRUE(layout.CanFitMessage(25, false));
  layout.SetInsertionPoint(Point(101, 50));
  EXPECT_FALSE(layout.CanFitMessage(25, false));
}

TEST_F(SiglusMessageFitTest, UsesCurrentFontSizeWithTheOriginalExtendedWidth) {
  // A larger current font must not enlarge the physical message area or its
  // original extended-width threshold when projecting overflow.
  layout.SetFontSize(30);
  layout.SetInsertionPoint(Point(91, 20));
  EXPECT_FALSE(layout.CanFitMessage(50, false));
}

TEST_F(SiglusMessageFitTest, CheckingFitDoesNotMoveTheCursor) {
  // Fit projections are queries; subsequent text must retain its actual cursor.
  const Point position(101, 50);
  layout.SetInsertionPoint(position);
  layout.CanFitMessage(25, true);
  EXPECT_EQ(layout.GetInsertionPoint(), position);
}

TEST(TextLayoutTest, ValidatesPhysicalExtentAndDefaultLayoutIsFull) {
  EXPECT_THROW(TextLayout({}, Size(10, 10), 9), std::invalid_argument);
  EXPECT_NO_THROW(TextLayout({}, Size(10, 10), 10));
  TextLayout empty;
  EXPECT_TRUE(empty.IsFull());
  EXPECT_TRUE(empty.LayoutText("A", {}).glyphs.empty());
}

TEST(TextLayoutTest, CellGridRulesAndPhysicalExtentSurviveFontChanges) {
  TextLayout::Settings settings{.font_size = 24,
                                .ruby_font_size = 10,
                                .x_spacing = 2,
                                .y_spacing = 3,
                                .ruby_spacing = 2};
  auto real = TextLayout::FromCells(settings, Size(4, 3));
  EXPECT_EQ(real.GetNormalSize(), Size(104, 111));
  EXPECT_EQ(real.GetExtendedSize(), Size(128, 111));
  EXPECT_EQ(real.GetInsertionPoint(), Point(0, 10));
  EXPECT_EQ(real.GetLineHeight(), 37);
  EXPECT_EQ(real.GetRubyTopPadding(), 0);

  settings.engine = TextLayout::Engine::Siglus;
  auto siglus = TextLayout::FromCells(settings, Size(4, 3));
  EXPECT_EQ(siglus.GetNormalSize(), Size(102, 78));
  EXPECT_EQ(siglus.GetInsertionPoint(), Point(0, 0));
  EXPECT_EQ(siglus.GetLineHeight(), 27);
  EXPECT_EQ(siglus.GetRubyTopPadding(), 12);
  siglus.SetFontSize(40);
  EXPECT_EQ(siglus.GetNormalSize(), Size(102, 78));
  EXPECT_EQ(siglus.GetExtendedSize(), Size(126, 78));
  EXPECT_EQ(siglus.GetLineHeight(), 43);
  EXPECT_EQ(TextLayout::FromCells(settings, Size(0, -2)).GetNormalSize(),
            Size(0, 0));
}

TEST(TextLayoutTest, ReturnsUtf8SourceSpansWithLegacyMonospaceAndCjkCells) {
  TextLayout layout({.font_size = 24, .x_spacing = 2}, Size(100, 100), 124);
  int measurements = 0;
  TextLayout::FontMetrics metrics{.monospace = true, .advance = [&](int) {
                                    ++measurements;
                                    return 99;
                                  }};
  const auto result = layout.LayoutText("A漢", metrics);
  ASSERT_EQ(result.glyphs.size(), 2);
  EXPECT_EQ(result.consumed_bytes, 4);
  EXPECT_EQ(result.glyphs[0].byte_offset, 0);
  EXPECT_EQ(result.glyphs[0].byte_count, 1);
  EXPECT_EQ(result.glyphs[1].byte_offset, 1);
  EXPECT_EQ(result.glyphs[1].byte_count, 3);
  EXPECT_EQ(Positions(result), (std::vector<Point>{Point(0, 0), Point(13, 0)}));
  EXPECT_EQ(layout.GetInsertionPoint(), Point(39, 0));
  EXPECT_EQ(measurements, 0);
  EXPECT_FALSE(result.glyphs[0].baseline_from_top);
}

TEST(TextLayoutTest, MeasuresAsciiOnceAndUsesSameAdvanceForWordLookahead) {
  TextLayout layout({.font_size = 20, .x_spacing = 2}, Size(60, 80), 80);
  layout.SetInsertionPoint(Point(20, 0));
  int measurements = 0;
  TextLayout::FontMetrics metrics{.monospace = false, .advance = [&](int cp) {
                                    ++measurements;
                                    return cp == 'i' ? 3 : 17;
                                  }};
  const auto result = layout.LayoutText("iWW", metrics);
  EXPECT_EQ(Positions(result),
            (std::vector<Point>{Point(0, 20), Point(5, 20), Point(24, 20)}));
  EXPECT_EQ(layout.GetInsertionPoint(), Point(43, 20));
  EXPECT_EQ(measurements, 3);
}

TEST(TextLayoutTest, ProportionalFontDoesNotMeasureCjkCells) {
  TextLayout layout({.font_size = 20, .x_spacing = 2}, Size(100, 100), 120);
  int measurements = 0;
  const auto result =
      layout.LayoutText("漢i", {.monospace = false, .advance = [&](int cp) {
                                  EXPECT_EQ(cp, 'i');
                                  ++measurements;
                                  return 3;
                                }});
  EXPECT_EQ(Positions(result), (std::vector<Point>{Point(0, 0), Point(22, 0)}));
  EXPECT_EQ(layout.GetInsertionPoint(), Point(27, 0));
  EXPECT_EQ(measurements, 1);
}

TEST(TextLayoutTest, SpacesAndKinsokuCanUseExtendedWidth) {
  TextLayout layout({.font_size = 20}, Size(40, 60), 60);
  layout.SetInsertionPoint(Point(40, 0));
  const auto result = layout.LayoutText(" !漢", {});
  EXPECT_EQ(Positions(result),
            (std::vector<Point>{Point(40, 0), Point(50, 0), Point(0, 20)}));
}

TEST(TextLayoutTest, KinsokuLookaheadUsesMeasuredAdvances) {
  TextLayout layout({.font_size = 20}, Size(50, 60), 60);
  layout.SetInsertionPoint(Point(30, 0));
  const auto result = layout.LayoutText("A!!", Proportional(12));
  EXPECT_EQ(Positions(result),
            (std::vector<Point>{Point(0, 20), Point(12, 20), Point(24, 20)}));
}

TEST(TextLayoutTest, OversizedRomanWordSplitsWithoutBlankOrOneCharacterLines) {
  TextLayout layout({.font_size = 20}, Size(30, 100), 50);
  const auto result = layout.LayoutText("abcdefgh", Proportional(10));
  EXPECT_EQ(Positions(result),
            (std::vector<Point>{Point(0, 0), Point(10, 0), Point(20, 0),
                                Point(0, 20), Point(10, 20), Point(20, 20),
                                Point(0, 40), Point(10, 40)}));
}

TEST(TextLayoutTest, OversizedRomanFallbackSurvivesSingleCharacterSubmissions) {
  TextLayout batch({.font_size = 20}, Size(30, 100), 50);
  batch.SetIndent(5);
  batch.SetInsertionPoint(Point(10, 0));
  TextLayout singles = batch;
  const std::string source = "abcdefgh";
  const auto expected = batch.LayoutText(source, Proportional(10));
  std::vector<Point> positions;
  for (std::size_t i = 0; i < source.size(); ++i) {
    const auto result = singles.LayoutText(
        std::string_view(source).substr(i, 1), Proportional(10),
        std::string_view(source).substr(i + 1));
    ASSERT_EQ(result.glyphs.size(), 1);
    positions.push_back(result.glyphs[0].position);
  }
  EXPECT_EQ(positions, Positions(expected));
  EXPECT_EQ(singles.GetInsertionPoint(), batch.GetInsertionPoint());
  EXPECT_EQ(positions.front(), Point(5, 20));
}

TEST(TextLayoutTest, OversizedGlyphIsPlacedOnceAtFreshLineForProgress) {
  TextLayout layout({.font_size = 20}, Size(5, 60), 25);
  const auto result = layout.LayoutText("漢字", {});
  EXPECT_EQ(Positions(result), (std::vector<Point>{Point(0, 0), Point(0, 20)}));
}

TEST(TextLayoutTest, OverflowRejectsUtf8GlyphWithoutChangingCursorOrRuby) {
  TextLayout layout({.font_size = 20}, Size(40, 20), 60);
  layout.SetInsertionPoint(Point(40, 0));
  layout.RubyBegin();
  const auto result = layout.LayoutText("漢", {});
  EXPECT_EQ(result.consumed_bytes, 0);
  EXPECT_TRUE(result.glyphs.empty());
  EXPECT_EQ(layout.GetInsertionPoint(), Point(40, 0));
  EXPECT_TRUE(layout.IsRubyActive());
  layout.SetInsertionPoint(Point(0, 0));
  const auto retry = layout.LayoutText("漢", {});
  ASSERT_EQ(retry.glyphs.size(), 1);
  EXPECT_EQ(retry.consumed_bytes, 3);
}

TEST(TextLayoutTest, AcceptedByteLimitRetainsRejectedSuffixForReplayLookahead) {
  TextLayout layout({.font_size = 20}, Size(80, 40), 100);
  layout.SetIndent(60);
  layout.SetInsertionPoint(Point(20, 0));
  TextLayout replay = layout;
  TextLayout truncated = layout;
  const std::string source = "漢、、、、";
  const auto original = layout.LayoutText(source, {});
  ASSERT_EQ(original.consumed_bytes, 6);
  EXPECT_EQ(Positions(original),
            (std::vector<Point>{Point(60, 20), Point(80, 20)}));
  const auto repeated =
      replay.LayoutText(source, {}, {}, original.consumed_bytes);
  EXPECT_EQ(Positions(repeated), Positions(original));
  EXPECT_EQ(replay.GetInsertionPoint(), layout.GetInsertionPoint());
  EXPECT_NE(Positions(truncated.LayoutText(source.substr(0, 6), {})),
            Positions(original));
}

TEST(TextLayoutTest, ByteLimitNeverSplitsUtf8Character) {
  TextLayout layout({.font_size = 20}, Size(80, 40), 100);
  const auto result = layout.LayoutText("A漢B", {}, {}, 3);
  EXPECT_EQ(result.consumed_bytes, 1);
  ASSERT_EQ(result.glyphs.size(), 1);
  EXPECT_EQ(layout.GetInsertionPoint(), Point(10, 0));
}

TEST(TextLayoutTest, SiglusPositiveSpacingLeavesFinalCellAvailable) {
  auto layout = TextLayout::FromCells(
      {.engine = TextLayout::Engine::Siglus, .font_size = 26, .x_spacing = 3},
      Size(2, 1));
  const auto result = layout.LayoutText("漢字語", {});
  EXPECT_EQ(layout.GetNormalSize(), Size(55, 26));
  EXPECT_EQ(Positions(result), (std::vector<Point>{Point(0, 0), Point(29, 0)}));
  EXPECT_EQ(result.consumed_bytes, 6);
  EXPECT_EQ(layout.GetInsertionPoint(), Point(58, 0));
}

TEST(TextLayoutTest, SiglusMonospaceFitRemovesActualHalfCellSpacing) {
  TextLayout layout(
      {.engine = TextLayout::Engine::Siglus, .font_size = 24, .x_spacing = 2},
      Size(35, 60), 59);
  layout.SetInsertionPoint(Point(24, 0));
  const auto result = layout.LayoutText("A", {});
  ASSERT_EQ(result.glyphs.size(), 1);
  EXPECT_EQ(result.glyphs[0].position, Point(0, 24));
}

TEST(TextLayoutTest, SiglusMonospaceRubyUsesOddAndNegativeSpacingContribution) {
  for (int spacing : {3, -3}) {
    SCOPED_TRACE(spacing);
    TextLayout layout({.engine = TextLayout::Engine::Siglus,
                       .font_size = 24,
                       .ruby_font_size = 10,
                       .x_spacing = spacing},
                      Size(100, 100), 124);
    layout.RubyBegin();
    const auto result = layout.LayoutText("A", {});
    ASSERT_EQ(result.glyphs.size(), 1);
    // Native ASCII cell is 12px regardless of the rounded spacing contribution.
    EXPECT_EQ(layout.RubyEnd("ruby", Size(8, 10)), Rect::REC(2, 0, 8, 10));
    const int advance = spacing == 3 ? 13 : 10;
    EXPECT_EQ(layout.GetInsertionPoint(), Point(advance, 0));
  }
}

TEST(TextLayoutTest, SiglusNegativeSpacingFitsMeasuredAsciiAndCjk) {
  auto layout = TextLayout::FromCells(
      {.engine = TextLayout::Engine::Siglus, .font_size = 20, .x_spacing = -3},
      Size(2, 1));
  const auto result = layout.LayoutText("漢AA", Proportional(10));
  EXPECT_EQ(Positions(result),
            (std::vector<Point>{Point(0, 0), Point(17, 0), Point(24, 0)}));
  EXPECT_EQ(layout.GetInsertionPoint(), Point(31, 0));
}

TEST(TextLayoutTest, BaselineAndRenderOffsetArePrecomputedWithoutMovingCursor) {
  TextLayout layout({.engine = TextLayout::Engine::Siglus,
                     .font_size = 26,
                     .ruby_font_size = 10,
                     .ruby_spacing = 3},
                    Size(100, 100), 126);
  layout.SetGlyphRenderOffset(Point(5, -2));
  const auto result = layout.LayoutText("漢", {});
  ASSERT_EQ(result.glyphs.size(), 1);
  EXPECT_EQ(result.glyphs[0].position, Point(5, 11));
  EXPECT_EQ(result.glyphs[0].baseline_from_top, 22);
  EXPECT_EQ(layout.GetInsertionPoint(), Point(26, 0));
}

TEST(TextLayoutTest, InlineNameAndFollowingQuoteDetermineIndentation) {
  TextLayout layout({.font_size = 20}, Size(100, 100), 120);
  const auto name = layout.LayoutName("AB", {});
  EXPECT_EQ(name.consumed_bytes, 2);
  EXPECT_EQ(layout.GetIndentation(), 20);
  const auto quote = layout.LayoutText("「漢", {});
  EXPECT_EQ(Positions(quote), (std::vector<Point>{Point(20, 0), Point(40, 0)}));
  EXPECT_EQ(layout.GetIndentation(), 40);
  layout.HardBreak();
  EXPECT_EQ(layout.GetInsertionPoint(), Point(40, 20));
}

TEST(TextLayoutTest, QuoteIndentationUsesCurrentNameModeAndSurvivesRejection) {
  TextLayout layout({.font_size = 20}, Size(40, 20), 60);
  layout.SetNameMode(TextLayout::NameMode::SeparateWindow);
  EXPECT_TRUE(layout.LayoutName("AB", {}).glyphs.empty());
  layout.SetInsertionPoint(Point(40, 0));
  layout.SetNameMode(TextLayout::NameMode::Disable);
  EXPECT_TRUE(layout.LayoutText("「", {}).glyphs.empty());
  EXPECT_EQ(layout.GetIndentation(), 0);
  layout.SetInsertionPoint(Point(0, 0));
  ASSERT_EQ(layout.LayoutText("「", {}).glyphs.size(), 1);
  EXPECT_EQ(layout.GetIndentation(), 20);
}

TEST(TextLayoutTest, SeparateNameDoesNotIndentFollowingQuote) {
  TextLayout layout({.font_size = 20}, Size(100, 100), 120);
  layout.SetNameMode(TextLayout::NameMode::SeparateWindow);
  EXPECT_TRUE(layout.LayoutName("AB", {}).glyphs.empty());
  ASSERT_EQ(layout.LayoutText("「", {}).glyphs.size(), 1);
  EXPECT_EQ(layout.GetIndentation(), 0);
}

TEST(TextLayoutTest,
     RubyUsesMeasuredSpanAndNativeRasterSizeWithoutGlyphOffset) {
  TextLayout layout({.engine = TextLayout::Engine::Siglus,
                     .font_size = 20,
                     .ruby_font_size = 10,
                     .x_spacing = 2,
                     .ruby_spacing = 3},
                    Size(100, 100), 120);
  layout.SetGlyphRenderOffset(Point(30, 40));
  layout.RubyBegin();
  const auto base = layout.LayoutText("AB", Proportional(7));
  EXPECT_EQ(layout.GetInsertionPoint(), Point(18, 0));
  EXPECT_EQ(base.glyphs[0].position, Point(30, 53));
  EXPECT_EQ(layout.RubyEnd("ruby", Size(6, 10)), Rect::REC(5, 0, 6, 10));
  EXPECT_FALSE(layout.IsRubyActive());
}

TEST(TextLayoutTest, EmptyRubyConsumesSpanAndCrossLineRubyIsRejected) {
  TextLayout layout({.font_size = 20, .ruby_font_size = 10}, Size(100, 100),
                    120);
  layout.RubyBegin();
  EXPECT_FALSE(layout.RubyEnd("", Size()));
  EXPECT_FALSE(layout.IsRubyActive());
  layout.RubyBegin();
  ASSERT_EQ(layout.LayoutText("漢", {}).glyphs.size(), 1);
  layout.HardBreak();
  // Repositioning to the original row must not hide a crossed line.
  layout.SetInsertionPoint(Point(40, 10));
  EXPECT_FALSE(layout.RubyEnd("ruby", Size(20, 10)));
  EXPECT_FALSE(layout.IsRubyActive());
}

TEST(TextLayoutTest, ResetPreservesOffsetStyleAndNameFollowingState) {
  TextLayout layout({.font_size = 20, .ruby_font_size = 10}, Size(100, 100),
                    120);
  layout.SetGlyphRenderOffset(Point(4, 5));
  layout.NameWithoutDisplay();
  layout.SetIndent(50);
  layout.RubyBegin();
  layout.Reset();
  EXPECT_EQ(layout.GetInsertionPoint(), Point(0, 10));
  EXPECT_EQ(layout.GetIndentation(), 0);
  EXPECT_FALSE(layout.IsRubyActive());
  const auto quote = layout.LayoutText("「", {});
  EXPECT_EQ(quote.glyphs[0].position, Point(4, 15));
  EXPECT_EQ(layout.GetIndentation(), 20);
}

TEST(TextLayoutTest, NextMessageRestoresDefaultsAndPreservesCursorAndNameMode) {
  TextLayout defaults({.font_size = 20}, Size(100, 100), 120);
  TextLayout layout = defaults;
  layout.SetNameMode(TextLayout::NameMode::Disable);
  layout.SetFontSize(30);
  layout.SetInsertionPoint(Point(10, 20));
  layout.SetGlyphRenderOffset(Point(5, 7));
  layout.NameWithoutDisplay();
  layout.SetIndent(10);
  layout.RubyBegin();
  layout.NextMessage(defaults);
  EXPECT_EQ(layout.GetInsertionPoint(), Point(10, 20));
  EXPECT_EQ(layout.GetNameMode(), TextLayout::NameMode::Disable);
  EXPECT_EQ(layout.GetFontSize(), 20);
  EXPECT_EQ(layout.GetGlyphRenderOffset(), Point(0, 0));
  EXPECT_EQ(layout.GetIndentation(), 0);
  EXPECT_FALSE(layout.IsRubyActive());
  ASSERT_EQ(layout.LayoutText("「", {}).glyphs.size(), 1);
  EXPECT_EQ(layout.GetIndentation(), 0);
}
