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

#include <gtest/gtest.h>

#include "core/object_internal/drawer/string.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "utf8.h"
#include "utilities/string_utilities.hpp"

#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

class StringDrawerTest : public ::testing::Test {
 protected:
  StringObjectFontSettings settings{.shadow_mode = 0};
  int rasterizations = 0;
  GraphicsObject object;

  static RGBColour Color(int id) {
    switch (id) {
      case 1:
        return RGBColour::Black();
      case 2:
        return RGBColour(255, 0, 0);
      case 3:
        return RGBColour(0, 255, 0);
      case 4:
        return RGBColour(0, 0, 255);
      default:
        return RGBColour::White();
    }
  }

  StringDrawer& SetString(std::string text, StringObjectStyle style = {}) {
    StringObjectRenderContext context{
        .rasterize =
            [this](const std::string& glyph, int size, RGBColour color) {
              ++rasterizations;
              const bool halfwidth = IsSiglusHalfwidth(
                  utf8::peek_next(glyph.begin(), glyph.end()));
              auto surface = std::make_shared<SDLSurface>(
                  Size(halfwidth ? size / 2 + size % 2 : size, size));
              surface->Fill(RGBAColour(color));
              return surface;
            },
        .color = Color,
        .font_settings = [this] { return settings; }};
    auto drawer = std::make_unique<StringDrawer>(std::move(text), style,
                                                 std::move(context));
    auto& result = *drawer;
    object.SetDrawer(std::move(drawer));
    return result;
  }
};

TEST_F(StringDrawerTest, RendersDistinctForegroundOutlineAndShadowLayers) {
  auto& drawer =
      SetString("A", {.shadow_color = 2, .outline_color = 4, .shadow_mode = 3});
  const auto surface = drawer.CurrentSurface(object);
  ASSERT_NE(surface, nullptr);
  EXPECT_EQ(surface->GetSize(), Size(10, 16));
  EXPECT_EQ(drawer.DstOrigin(object), Point(1, 1));
  EXPECT_EQ(surface->GetPixelAt(Point(1, 1)), RGBAColour::White());
  EXPECT_EQ(surface->GetPixelAt(Point(0, 0)), RGBAColour(0, 0, 255));
  EXPECT_EQ(surface->GetPixelAt(Point(9, 5)), RGBAColour(255, 0, 0));
}

TEST_F(StringDrawerTest, SupportsAllDecorationModes) {
  for (int mode = 0; mode <= 3; ++mode) {
    SCOPED_TRACE(mode);
    auto& drawer = SetString("A", {.shadow_mode = mode});
    auto surface = drawer.CurrentSurface(object);
    ASSERT_NE(surface, nullptr);
    const int padding = mode == 0 ? 0 : mode == 3 ? 4 : 2;
    EXPECT_EQ(surface->GetSize(), Size(6 + padding, 12 + padding));
    EXPECT_EQ(drawer.DstOrigin(object), mode & 2 ? Point(1, 1) : Point());
  }
}

TEST_F(StringDrawerTest, PreservesNegativeCoordinatesAndParentGeometry) {
  auto& drawer = SetString("#-10X#-4YA", {.shadow_mode = 2});
  object.Param().SetX(100);
  object.Param().SetY(50);
  EXPECT_EQ(drawer.SrcRect(object), Rect::REC(0, 0, 8, 14));
  EXPECT_EQ(drawer.DstOrigin(object), Point(11, 5));
  EXPECT_EQ(drawer.DstRect(object, nullptr), Rect::REC(89, 45, 8, 14));
  GraphicsObject parent;
  parent.Param().SetX(20);
  parent.Param().SetY(30);
  EXPECT_EQ(drawer.DstRect(object, &parent), Rect::REC(109, 75, 8, 14));
  object.Param().SetClipRect(Rect::REC(110, 76, 3, 4));
  auto geometry = drawer.BuildRenderGeometry(
      object, ParentObjState::BuildFrom(parent).render_state);
  auto clipped = ApplyClips(geometry, object);
  ASSERT_TRUE(clipped);
  EXPECT_EQ(clipped->dst, Rect::REC(110, 76, 3, 4));
  EXPECT_EQ(object.PixelWidth(), 0);
  EXPECT_EQ(object.PixelHeight(), 0);
}

TEST_F(StringDrawerTest, CachesUntilTextStyleOrEffectiveFontChanges) {
  auto& drawer = SetString("A");
  auto first = drawer.CurrentSurface(object);
  EXPECT_EQ(drawer.CurrentSurface(object), first);
  EXPECT_EQ(rasterizations, 1);
  drawer.SetText("A");
  EXPECT_EQ(drawer.CurrentSurface(object), first);
  drawer.SetText("B");
  EXPECT_NE(drawer.CurrentSurface(object), first);
  EXPECT_EQ(rasterizations, 2);
  auto style = drawer.style();
  style.color = 3;
  drawer.SetStyle(style);
  auto green = drawer.CurrentSurface(object);
  EXPECT_EQ(green->GetPixelAt(Point()), RGBAColour(0, 255, 0));
  EXPECT_EQ(rasterizations, 3);
  settings.name = "another font";
  drawer.CurrentSurface(object);
  settings.bold = true;
  drawer.CurrentSurface(object);
  settings.shadow_mode = 2;
  EXPECT_EQ(drawer.CurrentSurface(object)->GetSize(), Size(8, 14));
  EXPECT_EQ(rasterizations, 6);
}

TEST_F(StringDrawerTest, InlineColorChangesReachTheRasterizer) {
  auto& drawer = SetString("A#3CB");
  auto surface = drawer.CurrentSurface(object);
  ASSERT_NE(surface, nullptr);
  EXPECT_EQ(surface->GetPixelAt(Point()), RGBAColour::White());
  EXPECT_EQ(surface->GetPixelAt(Point(6, 0)), RGBAColour(0, 255, 0));
}

TEST_F(StringDrawerTest, EmptyStringsHaveNoSurfaceAndRemainUpdatable) {
  auto& drawer = SetString("");
  EXPECT_TRUE(object.HasDrawer());
  EXPECT_EQ(drawer.CurrentSurface(object), nullptr);
  EXPECT_EQ(drawer.SrcRect(object), Rect());
  EXPECT_FALSE(drawer.HitTest(object, Point()));
  drawer.SetText("A");
  EXPECT_NE(drawer.CurrentSurface(object), nullptr);
  drawer.SetText("");
  EXPECT_EQ(drawer.CurrentSurface(object), nullptr);
}

TEST_F(StringDrawerTest, ClonesHaveIndependentTextStyleAndSurfaces) {
  auto& original = SetString("A");
  auto surface = original.CurrentSurface(object);
  auto copy = object.Clone();
  auto* drawer = copy.GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  drawer->SetText("BC");
  auto style = drawer->style();
  style.size = 20;
  drawer->SetStyle(style);
  EXPECT_EQ(original.text(), "A");
  EXPECT_EQ(original.style().size, 12);
  EXPECT_EQ(original.CurrentSurface(object), surface);
  EXPECT_EQ(drawer->CurrentSurface(copy)->GetSize(), Size(20, 20));
  EXPECT_EQ(surface->GetSize(), Size(6, 12));
}

// ------------------------------------------------------------------------------

class StringLayoutTest : public ::testing::Test {
 protected:
  static void ExpectGlyphs(const std::vector<StringObjectGlyph>& actual,
                           std::initializer_list<StringObjectGlyph> expected) {
    ASSERT_EQ(actual.size(), expected.size());
    std::size_t i = 0;
    for (const auto& glyph : expected) {
      SCOPED_TRACE(i);
      EXPECT_EQ(actual[i].text, glyph.text);
      EXPECT_EQ(actual[i].position, glyph.position);
      EXPECT_EQ(actual[i].size, glyph.size);
      EXPECT_EQ(actual[i].color, glyph.color);
      ++i;
    }
  }
};

TEST_F(StringLayoutTest, HalfwidthCellsWrapIndependentlyOfPixelSpacing) {
  const auto glyphs = LayoutStringObject(
      "ab界c", {.size = 13, .xspace = 2, .yspace = 3, .count = 1});
  ASSERT_EQ(glyphs.size(), 4);
  EXPECT_EQ(glyphs[0].position, Point(0, 0));
  EXPECT_EQ(glyphs[1].position, Point(9, 0));
  EXPECT_EQ(glyphs[2].position, Point(0, 16));
  EXPECT_EQ(glyphs[3].position, Point(0, 32));
}

TEST_F(StringLayoutTest, SupportsHalfwidthKanaAndUnboundedLines) {
  const auto glyphs = LayoutStringObject("ｱ漢B", {});
  ASSERT_EQ(glyphs.size(), 3);
  EXPECT_EQ(glyphs[1].position, Point(6, 0));
  EXPECT_EQ(glyphs[2].position, Point(18, 0));
}

TEST_F(StringLayoutTest, HandlesLegacyCommandsAndEscapedHash) {
  const auto glyphs =
      LayoutStringObject("A##B#D#3C#20S#-4X#5YQ#+2RX#-3RYR", {});
  ASSERT_EQ(glyphs.size(), 5);
  EXPECT_EQ(glyphs[1].text, "#");
  EXPECT_EQ(glyphs[2].position, Point(12, 0));
  EXPECT_EQ(glyphs[3].position, Point(-4, 5));
  EXPECT_EQ(glyphs[3].size, 20);
  EXPECT_EQ(glyphs[3].color, 3);
  EXPECT_EQ(glyphs[4].position, Point(8, 2));
}

TEST_F(StringLayoutTest, WrappingUsesCurrentSizeAndResetsCountAtNewlines) {
  const auto glyphs = LayoutStringObject("A#24SB界#DC", {.count = 1});
  ASSERT_EQ(glyphs.size(), 4);
  EXPECT_EQ(glyphs[1].position, Point(6, 0));
  EXPECT_EQ(glyphs[2].position, Point(0, 24));
  EXPECT_EQ(glyphs[3].position, Point(0, 48));
}

TEST_F(StringLayoutTest, EmptyAndNewlineOnlyStringsProduceNoGlyphs) {
  EXPECT_TRUE(LayoutStringObject("#D", {}).empty());
  EXPECT_TRUE(LayoutStringObject("", {}).empty());
}

TEST_F(StringLayoutTest, TreatsUtf8AndRawLineBreaksAsGlyphs) {
  const auto glyphs = LayoutStringObject("日本\r\nA\nB", {});
  ExpectGlyphs(glyphs, {{"日", Point(0, 0), 12, 0},
                        {"本", Point(12, 0), 12, 0},
                        {"\r", Point(24, 0), 12, 0},
                        {"\n", Point(30, 0), 12, 0},
                        {"A", Point(36, 0), 12, 0},
                        {"\n", Point(42, 0), 12, 0},
                        {"B", Point(48, 0), 12, 0}});
}

TEST_F(StringLayoutTest, RejectsInvalidStyleAndCoordinateOverflow) {
  EXPECT_THROW(LayoutStringObject("a", {.size = 0}), std::invalid_argument);
  EXPECT_THROW(LayoutStringObject("a", {.shadow_mode = 4}),
               std::invalid_argument);
  EXPECT_THROW(LayoutStringObject("#2147483647X#1RXa", {}), std::out_of_range);
}

TEST_F(StringLayoutTest,
       UnknownCommandProducesZeroCodeGlyphBeforeRemainingSuffix) {
  // Arrange: an editable comment contains an unrecognized hash command.
  const std::string text = "#X";
  const StringObjectStyle style{.size = 12, .color = 0};

  // Act: target the public string-object layout boundary.
  const auto glyphs = LayoutStringObject(text, style);

  // Assert: legacy consumes '#', emits code zero, then processes 'X'.
  ExpectGlyphs(glyphs, {{std::string(1, '\0'), Point(0, 0), 12, 0},
                        {"X", Point(6, 0), 12, 0}});
}

TEST_F(StringLayoutTest,
       UnknownNumericCommandConsumesHashAndDigitsBeforeRemainingSuffix) {
  // Arrange: a generated label contains an integer with an unknown suffix.
  const std::string text = "#12Q";
  const StringObjectStyle style{.size = 12, .color = 0};

  // Act.
  const auto glyphs = LayoutStringObject(text, style);

  // Assert: the integer was consumed; only code zero and 'Q' become glyphs.
  ExpectGlyphs(glyphs, {{std::string(1, '\0'), Point(0, 0), 12, 0},
                        {"Q", Point(6, 0), 12, 0}});
}

TEST_F(StringLayoutTest, TrailingHashProducesZeroCodeGlyph) {
  // Arrange: a user-entered save comment ends with an unmatched hash.
  const std::string text = "#";
  const StringObjectStyle style{.size = 12, .color = 0};

  // Act.
  const auto glyphs = LayoutStringObject(text, style);

  // Assert: legacy emits a zero-code glyph, rather than an empty or '#' glyph.
  ExpectGlyphs(glyphs, {{std::string(1, '\0'), Point(0, 0), 12, 0}});
}

TEST_F(StringLayoutTest, RawCrLfRemainOrdinaryGlyphsInEditableComment) {
  // Arrange: raw control characters differ from the explicit '#D' command.
  const std::string text = "A\r\nB";
  const StringObjectStyle style{.size = 12, .color = 0};

  // Act.
  const auto glyphs = LayoutStringObject(text, style);

  // Assert: each TCHAR reaches the normal-glyph branch and advances one cell.
  ExpectGlyphs(glyphs, {{"A", Point(0, 0), 12, 0},
                        {"\r", Point(6, 0), 12, 0},
                        {"\n", Point(12, 0), 12, 0},
                        {"B", Point(18, 0), 12, 0}});
}

TEST_F(StringLayoutTest, ZeroInlineSizeIsRetainedInGeneratedLabel) {
  // Arrange: legacy accepts the integer value of the inline size command.
  const std::string text = "#0SA";
  const StringObjectStyle style{.size = 12, .color = 0};

  // Act.
  const auto glyphs = LayoutStringObject(text, style);

  // Assert: '#0S' consumes the command and records 'A' with size zero.
  ExpectGlyphs(glyphs, {{"A", Point(0, 0), 0, 0}});
}

TEST_F(StringLayoutTest, NegativeInlineSizeUsesSignedHalfwidthAdvance) {
  // Arrange: odd negative sizes exercise division toward zero.
  const std::string text = "#-3SAB";
  const StringObjectStyle style{.size = 12, .color = 0};

  // Act.
  const auto glyphs = LayoutStringObject(text, style);

  // Assert: the native halfwidth advance is (size + 1) / 2.
  ExpectGlyphs(glyphs, {{"A", Point(0, 0), -3, 0}, {"B", Point(-1, 0), -3, 0}});
}

TEST_F(StringLayoutTest, EscapedHashRendersOneOrdinaryWidthGlyph) {
  // Arrange: a dynamic label escapes a literal hash using the native syntax.
  const std::string text = "A##B";
  const StringObjectStyle style{.size = 12, .color = 0};

  // Act.
  const auto glyphs = LayoutStringObject(text, style);

  // Assert: one escaped hash takes one halfwidth cell.
  ExpectGlyphs(glyphs, {{"A", Point(0, 0), 12, 0},
                        {"#", Point(6, 0), 12, 0},
                        {"B", Point(12, 0), 12, 0}});
}

TEST_F(StringLayoutTest,
       SignedAbsoluteAndRelativeOffsetsAffectFollowingGlyphs) {
  // Arrange: a generated overlay combines absolute and signed relative moves.
  const std::string text = "A#-10X#-4YB#+3RX#-2RYC";
  const StringObjectStyle style{.size = 12, .color = 0};

  // Act.
  const auto glyphs = LayoutStringObject(text, style);

  // Assert: B's advance precedes the relative move to C at (-1,-6).
  ExpectGlyphs(glyphs, {{"A", Point(0, 0), 12, 0},
                        {"B", Point(-10, -4), 12, 0},
                        {"C", Point(-1, -6), 12, 0}});
}

TEST_F(StringLayoutTest, AutomaticWrapAndExplicitNewlineUseCurrentInlineSize) {
  // Arrange: a multiline dynamic label changes size before a fullwidth glyph.
  // ASCII weighs one cell and Japanese '界' weighs two under the native
  // Japanese codepage. Legacy's count budget is twice the fullwidth count.
  const std::string text = "A#24SB界#DC";
  const StringObjectStyle style{.size = 12, .count = 1, .color = 0};

  // Act.
  const auto glyphs = LayoutStringObject(text, style);

  // Assert: wrapping and '#D' both use the current size 24, not initial 12.
  ExpectGlyphs(glyphs, {{"A", Point(0, 0), 12, 0},
                        {"B", Point(6, 0), 24, 0},
                        {"界", Point(0, 24), 24, 0},
                        {"C", Point(0, 48), 24, 0}});
}
