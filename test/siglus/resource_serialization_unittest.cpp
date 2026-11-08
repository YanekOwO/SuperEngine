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

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <sstream>
#include "core/capture.hpp"
#include "core/counter.hpp"
#include "core/frame_action_manager.hpp"
#include "core/mask.hpp"
#include "core/object_internal/drawer/file.hpp"
#include "core/object_internal/drawer/number.hpp"
#include "core/object_internal/object_mutator.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "systems/sdl/sdl_surface.hpp"

#include <chrono>
#include <memory>
#include <string>

namespace libsiglus {
namespace {
using namespace std::chrono_literals;

class SiglusSaveThumbnailTest : public ::testing::Test {
 protected:
  std::unique_ptr<CaptureManager> MakeManager(bool png) {
    return std::make_unique<CaptureManager>(
        CaptureConfig{.save_thumbnail = true,
                      .thumbnail_size = Size(2, 2),
                      .png_thumbnail = png,
                      .screen_size = Size(2, 2)},
        std::filesystem::path{},
        [](const CaptureRequest&) {
          auto image = std::make_shared<SDLSurface>(Size(2, 2));
          image->Fill(RGBAColour(12, 34, 56, 255));
          return image;
        },
        CaptureManager::Load{});
  }
};

TEST_F(SiglusSaveThumbnailTest, EncodesRetainedSaveCaptureAsPng) {
  // PNG mode writes a complete in-memory image from the retained save capture.
  auto manager = MakeManager(true);
  manager->Capture(CaptureRequest{.kind = CaptureKind::Save});
  auto bytes = manager->EncodeSaveThumbnail();
  ASSERT_TRUE(bytes);
  EXPECT_EQ(bytes->substr(0, 8), std::string("\x89PNG\r\n\x1a\n", 8));
}

TEST_F(SiglusSaveThumbnailTest, EncodesRetainedSaveCaptureAsBmp) {
  // BMP mode writes its native thumbnail format without a temporary file.
  auto manager = MakeManager(false);
  manager->Capture(CaptureRequest{.kind = CaptureKind::Save});
  auto bytes = manager->EncodeSaveThumbnail();
  ASSERT_TRUE(bytes);
  EXPECT_EQ(bytes->substr(0, 2), "BM");
}

TEST_F(SiglusSaveThumbnailTest, MissingSaveCaptureHasNoThumbnail) {
  // An enabled thumbnail configuration alone does not invent an image.
  auto manager = MakeManager(true);
  EXPECT_FALSE(manager->EncodeSaveThumbnail());
}

}  // namespace
}  // namespace libsiglus
