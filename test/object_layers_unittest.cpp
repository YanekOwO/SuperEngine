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

#include "core/object_internal/object_layers.hpp"
#include "image_archive_test_support.hpp"
#include "number_test_support.hpp"
#include "serialization_test_helpers.hpp"

#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {

class ObjectLayersTest : public ::testing::Test {
 protected:
  std::shared_ptr<SDLSurface> atlas = MakeNumberTestAtlas();
  ObjectLayers layers;
};

TEST_F(ObjectLayersTest, ReplacementRebuildsBoundsFromNewLayers) {
  // Replacing cuts changes the cached bounds instead of retaining the old
  // union.
  layers.SetLayers({{.surface = atlas, .offset = Point(-20, 0), .cut_no = 1}});
  layers.SetLayers({{.surface = atlas, .offset = Point(30, 5), .cut_no = 2}});
  EXPECT_EQ(layers.Bounds(), Rect::REC(28, 2, 12, 14));
}

TEST_F(ObjectLayersTest, EmptyReplacementClearsPreviousGeometry) {
  // Clearing populated layers removes their bounds, sampled pixels, and hits.
  layers.SetLayers({{.surface = atlas, .cut_no = 1}});
  layers.SetLayers({});
  EXPECT_EQ(layers.Bounds(), Rect());
  EXPECT_EQ(layers.GetPixelColor(Point()), RGBAColour::Clear());
  EXPECT_FALSE(layers.HitTest(Point(), false));
  EXPECT_FALSE(layers.HitTest(Point(), true));
}

TEST_F(ObjectLayersTest, FailedReplacementPreservesPreviousLayers) {
  // Rejecting overflowing bounds leaves both cached geometry and pixels intact.
  layers.SetLayers({{.surface = atlas, .cut_no = 1}});
  EXPECT_THROW(
      layers.SetLayers({{.surface = atlas,
                         .offset = Point(std::numeric_limits<int>::max(), 0),
                         .cut_no = 2}}),
      std::out_of_range);
  EXPECT_EQ(layers.Bounds(), Rect::REC(-2, -3, 11, 15));
  EXPECT_EQ(layers.GetPixelColor(Point()), RGBAColour(2, 100, 200, 255));
  EXPECT_TRUE(layers.HitTest(Point(1, 1), true));
}

TEST_F(ObjectLayersTest, CopiesKeepIndependentLayerLists) {
  // Replacing a copied collection must not modify the original cached layers.
  layers.SetLayers({{.surface = atlas, .cut_no = 1}});
  ObjectLayers copy = layers;
  copy.SetLayers({{.surface = atlas, .offset = Point(30, 5), .cut_no = 2}});
  EXPECT_EQ(layers.Bounds(), Rect::REC(-2, -3, 11, 15));
  EXPECT_EQ(layers.GetPixelColor(Point()), RGBAColour(2, 100, 200, 255));
  EXPECT_EQ(copy.Bounds(), Rect::REC(28, 2, 12, 14));
}

TEST_F(ObjectLayersTest, HitTestsRejectPointsOutsideSourceBounds) {
  // Out-of-bounds source points miss even when alpha testing is disabled.
  layers.SetLayers({{.surface = atlas, .cut_no = 1}});
  EXPECT_FALSE(layers.HitTest(Point(-1, 0), false));
  EXPECT_FALSE(layers.HitTest(Point(11, 0), false));
  EXPECT_FALSE(layers.HitTest(Point(0, 15), true));
  EXPECT_TRUE(layers.HitTest(Point(10, 14), false));
}

// ------------------------------------------------------------------------------

class ObjectLayersArchiveTest : public ::testing::Test {};

TEST_F(ObjectLayersArchiveTest, RebuildsBoundsFromDestinationAsset) {
  // Different destination pattern geometry replaces the source layer bounds.
  serialization_test::ForEachFormat([&](auto format) {
    ImageArchiveHost loading;
    loading.size = Size(4, 3);
    loading.regions = {
        {.rect = Rect::REC(0, 0, 4, 3), .originX = 7, .originY = 8}};
    ObjectLayers source, target;
    source.SetLayers({{.surface = std::make_shared<SDLSurface>(Size(1, 1)),
                       .offset = Point(10, 20),
                       .filename = "layer"}});
    serialization_test::RoundTrip(source, target, format, {},
                                  {.images = &loading.images});
    EXPECT_EQ(target.Bounds(), Rect::REC(3, 12, 4, 3));
  });
}

TEST_F(ObjectLayersArchiveTest, RebuildsBoundsAndHitGeometry) {
  // Loading layers replaces stale bounds and retains alpha-sensitive hit
  // geometry.
  serialization_test::ForEachFormat([&](auto format) {
    ImageArchiveHost loading;
    loading.make_surface = [] { return MakeNumberTestAtlas(); };
    auto image = MakeNumberTestAtlas();
    ObjectLayers source, target;
    source.SetLayers({{image, Point(-7, 4), 0, 0, "atlas"},
                      {image, Point(19, 8), 1, 0, "atlas"}});
    target.SetLayers({{image, Point(200, 300), 0, 0}});
    serialization_test::RoundTrip(source, target, format, {},
                                  {.images = &loading.images});
    EXPECT_EQ(target.Bounds(), source.Bounds());
    for (Point point : {Point(), Point(5, 5), Point(20, 5), Point(100, 100)})
      EXPECT_EQ(target.HitTest(point, true), source.HitTest(point, true));
  });
}

}  // namespace
