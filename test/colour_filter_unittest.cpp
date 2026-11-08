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

#include "core/object_internal/drawer/colour_filter.hpp"
#include "core/object.hpp"
#include "serialization_test_helpers.hpp"

namespace {

class ColourFilterArchiveTest : public ::testing::Test {};

TEST_F(ColourFilterArchiveTest, RetainsFilterRectangle) {
  // A polymorphic colour filter retains its nondefault rectangle.
  serialization_test::ForEachFormat([&](auto format) {
    GraphicsObject source, target;
    source.SetDrawer(
        std::make_unique<ColourFilterObjectData>(Rect::REC(7, 9, 23, 31)));
    serialization_test::RoundTrip(source, target, format);
    auto* drawer = target.GetDrawer<ColourFilterObjectData>();
    ASSERT_NE(drawer, nullptr);
    EXPECT_EQ(drawer->PixelWidth(target), 23);
    EXPECT_EQ(drawer->PixelHeight(target), 31);
    EXPECT_EQ(target.GetDrawer().DstRect(target, nullptr),
              source.GetDrawer().DstRect(source, nullptr));
  });
}

}  // namespace
