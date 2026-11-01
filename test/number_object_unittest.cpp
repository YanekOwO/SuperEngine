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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "core/object_internal/drawer/number.hpp"
#include "number_test_support.hpp"

#include <limits>
#include <memory>
#include <stdexcept>

using ::testing::ElementsAre;

TEST(NumberObjectLayoutTest, ZeroAlwaysHasOneDigit) {
  // Default formatting must display one zero even without a configured field.
  EXPECT_THAT(LayoutNumberObject(0, {}, 10),
              ElementsAre(NumberObjectGlyph{0, 0}));
}

TEST(NumberObjectLayoutTest, DigitsRemainInReadingOrder) {
  // A multi-digit value selects atlas cuts from most to least significant.
  EXPECT_THAT(LayoutNumberObject(120, {}, 10),
              ElementsAre(NumberObjectGlyph{1, 0}, NumberObjectGlyph{2, 10},
                          NumberObjectGlyph{0, 20}));
}

TEST(NumberObjectLayoutTest, HiddenPaddingReservesSlots) {
  // A four-slot field right-aligns two visible digits after two hidden slots.
  EXPECT_THAT(LayoutNumberObject(12, {.digits = 4}, 10),
              ElementsAre(NumberObjectGlyph{1, 20}, NumberObjectGlyph{2, 30}));
}

TEST(NumberObjectLayoutTest, LeadingZerosFillField) {
  // Zero padding fills the same field without hiding its leading slots.
  EXPECT_THAT(LayoutNumberObject(12, {.digits = 4, .zero = true}, 10),
              ElementsAre(NumberObjectGlyph{0, 0}, NumberObjectGlyph{0, 10},
                          NumberObjectGlyph{1, 20}, NumberObjectGlyph{2, 30}));
}

TEST(NumberObjectLayoutTest, NarrowFieldsDoNotTruncate) {
  // A value exceeding the field width keeps every significant digit.
  EXPECT_THAT(LayoutNumberObject(123, {.digits = 1}, 10),
              ElementsAre(NumberObjectGlyph{1, 0}, NumberObjectGlyph{2, 10},
                          NumberObjectGlyph{3, 20}));
}

TEST(NumberObjectLayoutTest, NegativeValuesAlwaysShowMinus) {
  // Negative values reserve a sign slot even when sign display is disabled.
  EXPECT_THAT(LayoutNumberObject(-12, {}, 10),
              ElementsAre(NumberObjectGlyph{10, 0}, NumberObjectGlyph{1, 10},
                          NumberObjectGlyph{2, 20}));
}

TEST(NumberObjectLayoutTest, PositiveSignUsesPlusCut) {
  // Explicit sign display selects the plus cut for positive values.
  EXPECT_THAT(LayoutNumberObject(1, {.sign = true}, 10),
              ElementsAre(NumberObjectGlyph{11, 0}, NumberObjectGlyph{1, 10}));
}

TEST(NumberObjectLayoutTest, ZeroSignUsesDedicatedCut) {
  // Explicit sign display selects its separate sign cut for zero.
  EXPECT_THAT(LayoutNumberObject(0, {.sign = true}, 10),
              ElementsAre(NumberObjectGlyph{12, 0}, NumberObjectGlyph{0, 10}));
}

TEST(NumberObjectLayoutTest, UnpackedSignStaysAtFieldStart) {
  // A padded negative field keeps its unpacked sign in the first slot.
  EXPECT_THAT(LayoutNumberObject(-1, {.digits = 4}, 10),
              ElementsAre(NumberObjectGlyph{10, 0}, NumberObjectGlyph{1, 30}));
}

TEST(NumberObjectLayoutTest, PackedSignPrecedesVisibleDigits) {
  // Packing moves the sign past hidden leading slots next to the number.
  EXPECT_THAT(LayoutNumberObject(-1, {.digits = 4, .pack_sign = true}, 10),
              ElementsAre(NumberObjectGlyph{10, 20}, NumberObjectGlyph{1, 30}));
}

TEST(NumberObjectLayoutTest, ZeroPaddingDisablesSignPacking) {
  // Zero padding keeps the sign first even when packing is requested.
  EXPECT_THAT(LayoutNumberObject(
                  -1, {.digits = 4, .zero = true, .pack_sign = true}, 10),
              ElementsAre(NumberObjectGlyph{10, 0}, NumberObjectGlyph{0, 10},
                          NumberObjectGlyph{0, 20}, NumberObjectGlyph{1, 30}));
}

TEST(NumberObjectLayoutTest, ImageSpacingAddsToZeroCutWidth) {
  // AB1B's image-spacing mode adds eleven pixels to the atlas zero width.
  EXPECT_THAT(LayoutNumberObject(12, {.space = 11}, 10),
              ElementsAre(NumberObjectGlyph{1, 0}, NumberObjectGlyph{2, 21}));
}

TEST(NumberObjectLayoutTest, FixedSpacingIgnoresImageWidth) {
  // Nonzero spacing modes use the requested advance, including negative steps.
  EXPECT_THAT(LayoutNumberObject(12, {.space_mode = 1, .space = -4}, 10),
              ElementsAre(NumberObjectGlyph{1, 0}, NumberObjectGlyph{2, -4}));
}

TEST(NumberObjectLayoutTest, PatternOffsetAppliesToEveryGlyph) {
  // A changed base pattern selects the corresponding bank for signs and digits.
  EXPECT_THAT(LayoutNumberObject(-1, {}, 10, 13),
              ElementsAre(NumberObjectGlyph{23, 0}, NumberObjectGlyph{14, 10}));
}

TEST(NumberObjectLayoutTest, MinimumIntegerHasSafeMagnitude) {
  // The minimum signed integer must retain all digits without abs overflow.
  const auto glyphs =
      LayoutNumberObject(std::numeric_limits<int>::min(), {}, 10);
  ASSERT_EQ(glyphs.size(), 11);
  EXPECT_EQ(glyphs.front().pattern, 10);
  EXPECT_EQ(glyphs[1].pattern, 2);
  EXPECT_EQ(glyphs.back().pattern, 8);
}

TEST(NumberObjectLayoutTest, DigitCountIsBoundedBySpriteCapacity) {
  // The legacy capacity accepts sixteen slots and rejects unsafe field sizes.
  EXPECT_EQ(LayoutNumberObject(0, {.digits = 16, .zero = true}, 10).size(), 16);
  EXPECT_THROW(LayoutNumberObject(0, {.digits = -1}, 10),
               std::invalid_argument);
  EXPECT_THROW(LayoutNumberObject(0, {.digits = 17}, 10),
               std::invalid_argument);
}

TEST(NumberObjectLayoutTest, CoordinateOverflowIsRejected) {
  // Excessive spacing must fail before integer arithmetic can wrap positions.
  EXPECT_THROW(
      LayoutNumberObject(
          123, {.space_mode = 1, .space = std::numeric_limits<int>::max()}, 10),
      std::out_of_range);
}

// ------------------------------------------------------------------------------

class NumberDrawerTest : public ::testing::Test {
 protected:
  std::shared_ptr<SDLSurface> atlas = MakeNumberTestAtlas();
  GraphicsObject object;

  void SetUp() override {
    object.SetDrawer(std::make_unique<NumberDrawer>(atlas));
  }
  NumberDrawer& Drawer() { return *object.GetDrawer<NumberDrawer>(); }
};

TEST_F(NumberDrawerTest, GlyphOriginsAnchorTheNumber) {
  // A two-digit number keeps each atlas origin relative to the object's anchor.
  Drawer().SetNumber(12);
  EXPECT_EQ(Drawer().SrcRect(object), Rect::REC(0, 0, 22, 15));
  EXPECT_EQ(Drawer().DstOrigin(object), Point(2, 3));
}

TEST_F(NumberDrawerTest, ValueChangesRefreshCachedGeometry) {
  // After caching one digit, a longer value must rebuild the number's bounds.
  Drawer().SetNumber(1);
  EXPECT_EQ(Drawer().SrcRect(object).width(), 11);
  Drawer().SetNumber(12);
  EXPECT_EQ(Drawer().SrcRect(object).width(), 22);
}

TEST_F(NumberDrawerTest, FormattingChangesRefreshCachedGeometry) {
  // Changing spacing alone must invalidate an already composed number.
  Drawer().SetNumber(12);
  EXPECT_EQ(Drawer().SrcRect(object).width(), 22);
  Drawer().SetStyle({.space_mode = 1, .space = 30});
  EXPECT_EQ(Drawer().SrcRect(object).width(), 42);
}

TEST_F(NumberDrawerTest, PatternChangesRefreshCachedGeometry) {
  // Changing the object's pattern bank must rebuild glyph dimensions.
  Drawer().SetNumber(1);
  EXPECT_EQ(Drawer().SrcRect(object).width(), 11);
  object.Param().SetPattNo(13);
  EXPECT_EQ(Drawer().SrcRect(object).width(), 12);
}

TEST_F(NumberDrawerTest, MissingPatternsProduceEmptyGeometry) {
  // An unavailable bank draws nothing instead of silently substituting zero.
  object.Param().SetPattNo(100);
  EXPECT_TRUE(Drawer().SrcRect(object).is_degenerate());
  EXPECT_EQ(Drawer().DstOrigin(object), Point());
  EXPECT_FALSE(Drawer().HitTest(object, Point(0, 0)));
}

TEST_F(NumberDrawerTest, NegativePatternOffsetSkipsOnlyMissingGlyphs) {
  // A negative bank offset omits invalid cuts while preserving valid digits.
  Drawer().SetNumber(12);
  object.Param().SetPattNo(-2);
  EXPECT_EQ(Drawer().SrcRect(object), Rect::REC(0, 0, 10, 14));
  EXPECT_EQ(Drawer().DstOrigin(object), Point(-8, 3));
}

TEST_F(NumberDrawerTest, SizeQueriesDescribeRequestedAtlasCut) {
  // Atlas dimensions stay independent of value length and reject missing cuts.
  Drawer().SetNumber(12345);
  EXPECT_EQ(Drawer().PatternSize(0), Size(10, 14));
  EXPECT_EQ(Drawer().PatternSize(1), Size(11, 15));
  EXPECT_EQ(Drawer().PatternSize(-1), Size());
  EXPECT_EQ(Drawer().PatternSize(100), Size());
}

TEST_F(NumberDrawerTest, PixelQueriesSampleAtlasRatherThanNumberLayout) {
  // Pixel readback addresses the requested original cut, regardless of value.
  Drawer().SetNumber(99);
  EXPECT_EQ(Drawer().GetPixelColor(Point(0, 0), 1),
            RGBAColour(2, 100, 200, 255));
}

TEST_F(NumberDrawerTest, ClonesKeepIndependentFormattingAndValue) {
  // A clone shares immutable atlas pixels while later number edits stay local.
  Drawer().SetNumber(12);
  Drawer().SrcRect(object);
  auto copy = Drawer().Clone();
  auto* number = dynamic_cast<NumberDrawer*>(copy.get());
  ASSERT_NE(number, nullptr);
  number->SetNumber(7);
  number->SetStyle({.digits = 3, .zero = true});
  EXPECT_EQ(Drawer().value(), 12);
  EXPECT_EQ(Drawer().style(), NumberObjectStyle());
  EXPECT_EQ(Drawer().SrcRect(object).width(), 22);
  EXPECT_EQ(number->SrcRect(object).width(), 31);
}

TEST_F(NumberDrawerTest, InvalidFormattingPreservesPreviousStyle) {
  // Rejecting an oversized field must leave the previous valid style intact.
  Drawer().SetStyle({.digits = 4});
  EXPECT_THROW(Drawer().SetStyle({.digits = 17}), std::invalid_argument);
  EXPECT_EQ(Drawer().style().digits, 4);
}

TEST_F(NumberDrawerTest, ParentPositionFoldsIntoNumberGeometry) {
  // Parent translation preserves the number's local atlas anchor.
  Drawer().SetNumber(12);
  object.Param().SetX(100);
  object.Param().SetY(50);
  GraphicsObject parent;
  parent.Param().SetX(20);
  parent.Param().SetY(30);
  EXPECT_EQ(Drawer().DstRect(object, &parent), Rect::REC(118, 77, 22, 15));
}

TEST_F(NumberDrawerTest, NumberGeometryUsesSharedClipping) {
  // Destination clipping cuts the number through the standard object pipeline.
  Drawer().SetNumber(12);
  object.Param().SetX(100);
  object.Param().SetY(50);
  object.Param().SetClipRect(Rect::REC(100, 50, 3, 4));
  const auto clipped =
      ApplyClips(Drawer().BuildRenderGeometry(object, {}), object);
  ASSERT_TRUE(clipped);
  EXPECT_EQ(clipped->dst, Rect::REC(100, 50, 3, 4));
}

TEST_F(NumberDrawerTest, MissingSignCutPreservesDigitPositions) {
  // A digits-only atlas omits its unavailable sign without moving the digits.
  NumberDrawer digits_only(MakeNumberTestAtlas(10));
  digits_only.SetNumber(-12);
  EXPECT_EQ(digits_only.SrcRect(object), Rect::REC(0, 0, 22, 15));
  EXPECT_EQ(digits_only.DstOrigin(object), Point(-8, 3));
}

TEST_F(NumberDrawerTest, GlyphBoundsOverflowIsRejected) {
  // A valid slot position cannot let the glyph endpoint overflow renderer ints.
  Drawer().SetNumber(12);
  Drawer().SetStyle(
      {.space_mode = 1, .space = std::numeric_limits<int>::max() - 1});
  EXPECT_THROW(Drawer().SrcRect(object), std::out_of_range);
}

TEST_F(NumberDrawerTest, HiddenPaddingDoesNotBecomeClickable) {
  // The leading hidden slots reserve position without contributing a hit area.
  object.Param().SetVisible(1);
  object.Param().SetX(100);
  object.Param().SetY(100);
  Drawer().SetNumber(1);
  Drawer().SetStyle({.digits = 4});
  EXPECT_FALSE(Drawer().HitTest(object, Point(100, 100)));
  EXPECT_TRUE(Drawer().HitTest(object, Point(130, 100)));
}
