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

#include "core/object.hpp"
#include "core/object_internal/drawer/file.hpp"
#include "image_archive_test_support.hpp"
#include "number_test_support.hpp"
#include "serialization_test_helpers.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

namespace {

TEST(SiglusCompositeObjectTest, DetectsCompositeNames) {
  EXPECT_TRUE(IsCompositeObjectName("base | face"));
  EXPECT_TRUE(IsCompositeObjectName("||"));
  EXPECT_FALSE(IsCompositeObjectName("base"));
}

TEST(SiglusCompositeObjectTest, ParsesSimpleCompositeAndRemovesSpaces) {
  std::vector<CompositeObjectPart> parts =
      ParseCompositeObjectName("bs2_jd01_base01 | bs2_jd_f01_03");

  ASSERT_EQ(parts.size(), 2);
  EXPECT_EQ(parts[0].file_name, "bs2_jd01_base01");
  EXPECT_EQ(parts[0].x, 0);
  EXPECT_EQ(parts[0].y, 0);
  EXPECT_EQ(parts[0].cut_no, 0);
  EXPECT_EQ(parts[0].blend_type, 0);
  EXPECT_EQ(parts[1].file_name, "bs2_jd_f01_03");
  EXPECT_EQ(parts[1].x, 0);
  EXPECT_EQ(parts[1].y, 0);
  EXPECT_EQ(parts[1].cut_no, 0);
  EXPECT_EQ(parts[1].blend_type, 0);
}

TEST(SiglusCompositeObjectTest, ParsesOffsetsCutAndBlendZero) {
  std::vector<CompositeObjectPart> parts =
      ParseCompositeObjectName("base(10,-2)|face(3,4,1)|mask(0,0,blend=0)");

  ASSERT_EQ(parts.size(), 3);
  EXPECT_EQ(parts[0].file_name, "base");
  EXPECT_EQ(parts[0].x, 10);
  EXPECT_EQ(parts[0].y, -2);
  EXPECT_EQ(parts[0].cut_no, 0);
  EXPECT_EQ(parts[1].file_name, "face");
  EXPECT_EQ(parts[1].x, 3);
  EXPECT_EQ(parts[1].y, 4);
  EXPECT_EQ(parts[1].cut_no, 1);
  EXPECT_EQ(parts[2].file_name, "mask");
  EXPECT_EQ(parts[2].blend_type, 0);
}

TEST(SiglusCompositeObjectTest, RejectsMalformedLayers) {
  EXPECT_THROW(ParseCompositeObjectName("base|"), std::runtime_error);
  EXPECT_THROW(ParseCompositeObjectName("base||face"), std::runtime_error);
  EXPECT_THROW(ParseCompositeObjectName("base(1)|face"), std::runtime_error);
  EXPECT_THROW(ParseCompositeObjectName("base(1,2"), std::runtime_error);
}

TEST(SiglusCompositeObjectTest, RejectsUnsupportedNonzeroBlend) {
  EXPECT_THROW(ParseCompositeObjectName("base|face(0,0,blend=1)"),
               std::runtime_error);
}

// ------------------------------------------------------------------------------

class CompositeObjectArchiveTest : public ::testing::Test {};

TEST_F(CompositeObjectArchiveTest, RebuildsCompositeGeometry) {
  // A restored composite drawer rebuilds bounds and pixel sampling from its
  // layers.
  serialization_test::ForEachFormat([&](auto format) {
    ImageArchiveHost loading;
    loading.make_surface = [] { return MakeNumberTestAtlas(); };
    auto image = MakeNumberTestAtlas();
    std::unique_ptr<GraphicsObjectData>
        source = std::make_unique<CompositeGraphicsObject>(
            std::vector<ObjectLayer>{{image, Point(-7, 4), 0, 0, "atlas"},
                                     {image, Point(19, 8), 1, 0, "atlas"}}),
        target;
    GraphicsObject object;
    serialization_test::RoundTrip(source, target, format, {},
                                  {.images = &loading.images});
    ASSERT_NE(dynamic_cast<CompositeGraphicsObject*>(target.get()), nullptr);
    EXPECT_EQ(target->SrcRect(object), source->SrcRect(object));
    EXPECT_EQ(target->DstOrigin(object), source->DstOrigin(object));
    EXPECT_EQ(target->GetPixelColor(Point(3, 4), 0),
              source->GetPixelColor(Point(3, 4), 0));
  });
}

}  // namespace
