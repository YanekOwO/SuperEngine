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

#include "core/object.hpp"
#include "core/object_internal/drawer/gan.hpp"
#include "image_archive_test_support.hpp"
#include "mock_clock.hpp"
#include "number_test_support.hpp"
#include "serialization_test_helpers.hpp"

namespace {

class GanDrawerArchiveTest : public ::testing::Test {};

TEST_F(GanDrawerArchiveTest, ReloadsNamedAnimationImage) {
  // A GAN drawer reconstructs its explicitly assigned image from the loading
  // host.
  serialization_test::ForEachFormat([&](auto format) {
    ImageArchiveHost loading;
    loading.color = RGBAColour(90, 80, 70, 255);
    GanGraphicsObjectData source("assigned_image", MakeNumberTestAtlas(), {}),
        target("old", MakeNumberTestAtlas(), {});
    serialization_test::RoundTrip(source, target, format, {},
                                  {.images = &loading.images});
    EXPECT_EQ(loading.loaded_path, loading.root / "assigned_image.g00");
    EXPECT_EQ(target.GetPixelColor(Point(), 0), loading.color);
  });
}

TEST_F(GanDrawerArchiveTest, RestoresFrameAndRemainder) {
  // A GAN animation crosses its next frame boundary after only the saved
  // remaining time.
  serialization_test::ForEachFormat([&](auto format) {
    using namespace std::chrono_literals;
    ImageArchiveHost loading;
    loading.make_surface = [] { return MakeNumberTestAtlas(); };
    auto clock = std::make_shared<MockClock>();
    std::vector<std::vector<GanDecoder::Frame>> frames{
        {{0, 3, 4, 50, 128, 9}, {1, 7, 8, 70, 255, 2}}};
    std::unique_ptr<GraphicsObjectData>
        source = std::make_unique<GanGraphicsObjectData>(
            "atlas", MakeNumberTestAtlas(), frames, clock),
        target;
    source->PlaySet(0);
    clock->AdvanceTime(30ms);
    source->Execute();
    GraphicsObject object;
    auto rect = source->SrcRect(object);
    auto loading_clock = std::make_shared<MockClock>();
    loading_clock->SetElapsed(9000ms);
    serialization_test::RoundTrip(
        source, target, format, {},
        {.images = &loading.images, .clock = loading_clock});
    ASSERT_NE(dynamic_cast<GanGraphicsObjectData*>(target.get()), nullptr);
    EXPECT_EQ(target->SrcRect(object), rect);
    loading_clock->AdvanceTime(19ms);
    target->Execute();
    EXPECT_EQ(target->SrcRect(object), rect);
    loading_clock->AdvanceTime(1ms);
    target->Execute();
    EXPECT_NE(target->SrcRect(object), rect);
    EXPECT_EQ(target->SrcRect(object),
              MakeNumberTestAtlas()->GetPattern(1).rect);
  });
}

}  // namespace
