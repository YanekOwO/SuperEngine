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
#include "core/frame_action_manager.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "serialization_test_helpers.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "utilities/file.hpp"

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <format>

namespace {
using namespace libsiglus;
using namespace libsiglus::binding;

class SiglusObjectCaptureTest : public ::testing::Test {
 protected:
  void SetUp() override {
    runtime.GameplayMachine().vm = std::make_unique<serilang::VM>(
        std::make_shared<serilang::GarbageCollector>());
    runtime.stage = std::make_unique<Stage>(1);
    runtime.counter_list = std::make_shared<CounterList>(0);
    runtime.frame_action_manager = std::make_shared<FrameActionManager>(
        *runtime.stage, runtime.local_config, 0);
    runtime.captures = std::make_shared<CaptureManager>(
        CaptureConfig{}, files.dir(),
        [](const CaptureRequest&) {
          return std::make_shared<SDLSurface>(Size(1, 1));
        },
        [](const std::filesystem::path& path) {
          return std::make_shared<SDLSurface>(SDL_LoadBMP(path.c_str()));
        });
    for (auto key : {"0_memory", "1_counter", "1_frame_action", "1_object"}) {
      auto* bind = SiglusBindingRegistry::Find(key);
      ASSERT_NE(bind, nullptr);
      (*bind)(runtime.GameplayMachine());
    }
    Run("a = Object();");
  }

  void Run(const std::string& code) {
    Execute(*runtime.GameplayMachine().vm, code);
  }

  GraphicsObject& Object() { return *runtime.stage->objects.Get(kLayerFg, 0); }

  void WriteThumbnail(int number, bool local, RGBAColour color) {
    auto filename = local ? std::format("{:010}.bmp", number)
                          : std::format("{:04}.bmp", number);
    SDLSurface image(Size(1, 1));
    image.Fill(color);
    ASSERT_TRUE(
        SDL_SaveBMP(image.RawSurface(), (files.dir() / filename).c_str()));
  }

  ScopedTemporary files;
  SiglusRuntime runtime;
};

TEST_F(SiglusObjectCaptureTest, SaveThumbnailCommandRetainsSourceNumber) {
  // A script-created save thumbnail reloads its current file by slot number.
  serialization_test::ForEachFormat([&](auto format) {
    WriteThumbnail(7, false, RGBAColour(1, 2, 3, 255));
    Run("a.create_save_thumb(7);");
    auto bytes = serialization_test::Save(Object(), format);
    WriteThumbnail(7, false, RGBAColour(90, 80, 70, 255));
    GraphicsObject restored;
    serialization_test::Load(bytes, restored, format,
                             {.captures = runtime.captures.get()});
    ASSERT_TRUE(restored.HasDrawer());
    EXPECT_EQ(restored.GetDrawer().GetPixelColor(Point(), 0),
              RGBAColour(90, 80, 70, 255));
  });
}

TEST_F(SiglusObjectCaptureTest, CaptureThumbnailCommandRetainsLocalSource) {
  // A script-created local thumbnail reloads the local file when numbers
  // collide.
  serialization_test::ForEachFormat([&](auto format) {
    WriteThumbnail(7, false, RGBAColour(1, 2, 3, 255));
    WriteThumbnail(7, true, RGBAColour(90, 80, 70, 255));
    Run("a.create_capture_thumb(7);");
    GraphicsObject restored;
    serialization_test::RoundTrip(Object(), restored, format, {},
                                  {.captures = runtime.captures.get()});
    ASSERT_TRUE(restored.HasDrawer());
    EXPECT_EQ(restored.GetDrawer().GetPixelColor(Point(), 0),
              RGBAColour(90, 80, 70, 255));
  });
}

TEST_F(SiglusObjectCaptureTest, MissingThumbnailRetainsSourceForLaterReload) {
  // A thumbnail absent at creation retains its identifier for a later load.
  serialization_test::ForEachFormat([&](auto format) {
    std::filesystem::remove(files.dir() / "0009.bmp");
    Run("a.create_save_thumb(9);");
    auto bytes = serialization_test::Save(Object(), format);
    WriteThumbnail(9, false, RGBAColour(90, 80, 70, 255));
    GraphicsObject restored;
    serialization_test::Load(bytes, restored, format,
                             {.captures = runtime.captures.get()});
    ASSERT_TRUE(restored.HasDrawer());
    EXPECT_EQ(restored.GetDrawer().GetPixelColor(Point(), 0),
              RGBAColour(90, 80, 70, 255));
  });
}

TEST_F(SiglusObjectCaptureTest, CaptureCommandRestoresWithoutPixels) {
  // A script-created capture retains its pose but not its captured image.
  serialization_test::ForEachFormat([&](auto format) {
    runtime.captures->Capture({.kind = CaptureKind::Object});
    Run("a.create_capture(1, 20, 30);");
    GraphicsObject restored;
    serialization_test::RoundTrip(Object(), restored, format);
    ASSERT_TRUE(restored.HasDrawer());
    EXPECT_EQ(restored.Param().x(), 20);
    EXPECT_EQ(restored.Param().y(), 30);
    EXPECT_FALSE(restored.GetDrawer().CurrentSurface(restored));
  });
}

}  // namespace
