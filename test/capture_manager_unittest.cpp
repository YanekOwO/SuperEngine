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

#include "core/capture.hpp"
#include "serialization_test_helpers.hpp"
#include "systems/sdl/sdl_surface.hpp"

#include <SDL3/SDL.h>

#include "png.h"

namespace {

class CaptureManagerArchiveTest : public ::testing::Test {};

TEST_F(CaptureManagerArchiveTest, RequestRetainsCaptureParameters) {
  // A deferred capture retains its size, sorting range and export parameters.
  serialization_test::ForEachFormat([&](auto format) {
    CaptureRequest source{.kind = CaptureKind::Buffer,
                          .end = {9, -2},
                          .display = false,
                          .size = Size(8, 6),
                          .offset = Point(3, 4),
                          .png_name = "capture",
                          .priority = 2},
        target;
    serialization_test::RoundTrip(source, target, format);
    EXPECT_EQ(target.kind, source.kind);
    EXPECT_EQ(target.end, source.end);
    EXPECT_FALSE(target.display);
    EXPECT_EQ(target.size, source.size);
    EXPECT_EQ(target.offset, source.offset);
    EXPECT_EQ(target.png_name, "capture");
    EXPECT_EQ(target.priority, 2);
  });
}

TEST_F(CaptureManagerArchiveTest, RejectsInvalidCaptureKind) {
  // An unknown deferred capture kind rejects loading.
  serialization_test::ForEachFormat([&](auto format) {
    CaptureRequest source, target;
    source.kind = static_cast<CaptureKind>(99);
    auto bytes = serialization_test::Save(source, format);
    EXPECT_THROW(serialization_test::Load(bytes, target, format),
                 std::runtime_error);
  });
}

// ------------------------------------------------------------------------------

class CaptureThumbnailTest : public ::testing::Test {
 protected:
  CaptureManager MakeManager(bool png, bool enabled = true) {
    return CaptureManager({.save_thumbnail = enabled,
                           .thumbnail_size = Size(2, 2),
                           .png_thumbnail = png,
                           .screen_size = Size(2, 2)},
                          {},
                          [](const CaptureRequest&) {
                            auto image =
                                std::make_shared<SDLSurface>(Size(2, 2));
                            image->Fill(RGBAColour(12, 34, 56, 255));
                            return image;
                          },
                          {});
  }
};

TEST_F(CaptureThumbnailTest, PngContainsCapturedPixels) {
  // A retained save capture encodes a complete PNG with the original pixels.
  auto manager = MakeManager(true);
  manager.Capture({.kind = CaptureKind::Save});
  auto bytes = manager.EncodeSaveThumbnail();
  ASSERT_TRUE(bytes);
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  ASSERT_TRUE(
      png_image_begin_read_from_memory(&image, bytes->data(), bytes->size()));
  image.format = PNG_FORMAT_RGBA;
  std::vector<unsigned char> pixels(PNG_IMAGE_SIZE(image));
  const bool decoded =
      png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr);
  EXPECT_EQ(image.width, 2u);
  EXPECT_EQ(image.height, 2u);
  png_image_free(&image);
  ASSERT_TRUE(decoded);
  EXPECT_EQ(pixels,
            (std::vector<unsigned char>{12, 34, 56, 255, 12, 34, 56, 255, 12,
                                        34, 56, 255, 12, 34, 56, 255}));
}

TEST_F(CaptureThumbnailTest, BmpContainsCapturedPixels) {
  // A retained save capture encodes a complete BMP without temporary files.
  auto manager = MakeManager(false);
  manager.Capture({.kind = CaptureKind::Save});
  auto bytes = manager.EncodeSaveThumbnail();
  ASSERT_TRUE(bytes);
  auto* raw =
      SDL_LoadBMP_IO(SDL_IOFromConstMem(bytes->data(), bytes->size()), true);
  ASSERT_NE(raw, nullptr);
  SDLSurface image(raw);
  EXPECT_EQ(image.GetSize(), Size(2, 2));
  EXPECT_EQ(image.GetPixelAt(Point(1, 1)), RGBAColour(12, 34, 56, 255));
}

TEST_F(CaptureThumbnailTest, MissingCaptureProducesNoThumbnail) {
  // Enabling thumbnails without capturing a frame yields no encoded image.
  auto manager = MakeManager(true);
  EXPECT_FALSE(manager.EncodeSaveThumbnail());
}

TEST_F(CaptureThumbnailTest, DisabledThumbnailsProduceNoThumbnail) {
  // A capture request cannot produce a thumbnail when thumbnails are disabled.
  auto manager = MakeManager(true, false);
  manager.Capture({.kind = CaptureKind::Save});
  EXPECT_FALSE(manager.EncodeSaveThumbnail());
}

}  // namespace
