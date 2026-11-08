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

#include "core/frame_action_manager.hpp"
#include "core/object_internal/drawer/file.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "systems/sdl/sdl_surface.hpp"

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace libsiglus;
using namespace libsiglus::binding;

class SiglusObjectSizeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    runtime.GameplayMachine().vm = std::make_unique<serilang::VM>(
        std::make_shared<serilang::GarbageCollector>());
    runtime.stage = std::make_unique<Stage>(4);
    runtime.counter_list = std::make_shared<CounterList>(0);
    runtime.frame_action_manager = std::make_shared<FrameActionManager>(
        *runtime.stage, runtime.local_config, 0);
    for (auto key : {"0_memory", "1_counter", "1_frame_action", "1_object"}) {
      auto* bind = SiglusBindingRegistry::Find(key);
      ASSERT_NE(bind, nullptr);
      (*bind)(runtime.GameplayMachine());
    }
    Run("a = Object();");
    SetPicture(MakeSurface(Size(64, 48), {{.rect = Rect::REC(0, 0, 12, 8)},
                                          {.rect = Rect::REC(20, 4, 17, 11)}}));
  }

  std::shared_ptr<SDLSurface> MakeSurface(Size size,
                                          std::vector<GrpRect> regions) {
    auto* raw = SDL_CreateSurface(size.width(), size.height(),
                                  SDL_PIXELFORMAT_ARGB8888);
    if (!raw)
      throw std::runtime_error(SDL_GetError());
    return std::make_shared<SDLSurface>(raw, std::move(regions));
  }

  void SetPicture(std::shared_ptr<SDLSurface> surface, int index = 0) {
    Object(index).SetDrawer(
        std::make_unique<GraphicsObjectOfFile>("picture", std::move(surface)));
  }

  void Run(const std::string& source) {
    Execute(*runtime.GameplayMachine().vm, source);
  }

  GraphicsObject& Object(int index = 0) {
    return *runtime.stage->objects.Get(kLayerFg, index);
  }

  int Int(std::string_view name) {
    return *runtime.GameplayMachine()
                .vm->globals_->at(std::string(name))
                .Get_if<int>();
  }

  SiglusRuntime runtime;
};

TEST_F(SiglusObjectSizeTest, PictureSizeIgnoresObjectScaling) {
  // A two-cut picture changes scale; its size queries retain the original
  // dimensions even when rendering is reduced, enlarged, collapsed, or flipped.
  for (const auto& [x, y] :
       {std::pair{750, 500}, {2000, 3000}, {0, 0}, {-1000, -2000}}) {
    SCOPED_TRACE(std::format("scale({}, {})", x, y));
    Run(
        std::format("a.set_scale({}, {}); width = a.get_size_x(); "
                    "height = a.get_size_y();",
                    x, y));
    EXPECT_EQ(Int("width"), 12);
    EXPECT_EQ(Int("height"), 8);
  }
}

TEST_F(SiglusObjectSizeTest, ExplicitCutSelectsItsOwnDimensions) {
  // Cut zero is displayed while cut one is queried; the requested cut's
  // dimensions must be returned instead of the displayed pattern's size.
  Run("a.patno = 0; width = a.get_size_x(1); height = a.get_size_y(1);");

  EXPECT_EQ(Int("width"), 17);
  EXPECT_EQ(Int("height"), 11);
}

TEST_F(SiglusObjectSizeTest, OmittedCutSelectsZeroDespiteDisplayedPattern) {
  // Cut one is displayed; omitting the size-query argument still selects zero.
  Run("a.patno = 1; width = a.get_size_x(); height = a.get_size_y();");

  EXPECT_EQ(Int("width"), 12);
  EXPECT_EQ(Int("height"), 8);
}

TEST_F(SiglusObjectSizeTest, PictureSizeIncludesTransparentPadding) {
  // A small stored crop has a larger logical cut and a nonzero origin; queries
  // must include its transparent padding rather than the crop or origin.
  SetPicture(MakeSurface(
      Size(64, 48),
      {{.rect = Rect::REC(4, 5, 12, 8),
        .originX = 7,
        .originY = -3,
        .pixel_region = PixelRegion{Rect::REC(6, 6, 3, 2), Point(1, 1)}}}));
  Run("width = a.get_size_x(); height = a.get_size_y();");

  EXPECT_EQ(Int("width"), 12);
  EXPECT_EQ(Int("height"), 8);
}

TEST_F(SiglusObjectSizeTest, MissingPictureSurfaceHasZeroSize) {
  // A picture drawer has no loaded surface; both queries safely return zero.
  SetPicture(nullptr);
  Run("width = a.get_size_x(); height = a.get_size_y();");

  EXPECT_EQ(Int("width"), 0);
  EXPECT_EQ(Int("height"), 0);
}

TEST_F(SiglusObjectSizeTest, NegativeCutHasZeroSize) {
  // A negative cut is absent; queries must return zero without selecting zero.
  Run("width = a.get_size_x(-1); height = a.get_size_y(-1);");

  EXPECT_EQ(Int("width"), 0);
  EXPECT_EQ(Int("height"), 0);
}

TEST_F(SiglusObjectSizeTest, CutPastTheEndHasZeroSize) {
  // A two-cut picture is queried at the first invalid index; it has no size.
  Run("width = a.get_size_x(2); height = a.get_size_y(2);");

  EXPECT_EQ(Int("width"), 0);
  EXPECT_EQ(Int("height"), 0);
}

TEST_F(SiglusObjectSizeTest, DividedBackgroundCutsRemainAligned) {
  // Four high-resolution cuts are laid out using scaled size-query results;
  // applying the script's 75% scale once must leave their edges contiguous.
  auto surface = MakeSurface(Size(3840, 2160),
                             {{.rect = Rect::REC(0, 0, 1920, 1080)},
                              {.rect = Rect::REC(1920, 0, 1920, 1080)},
                              {.rect = Rect::REC(0, 1080, 1920, 1080)},
                              {.rect = Rect::REC(1920, 1080, 1920, 1080)}});
  Run("pictures = [a, Object(0, 1), Object(0, 2), Object(0, 3)];");
  for (int i = 0; i < 4; ++i)
    SetPicture(surface, i);

  Run(R"(
    for (i = 0; i < 4; i += 1) {
      picture = pictures[i];
      picture.set_scale(750, 750);
      if (i == 0) {
        tile_width = picture.get_size_x() * 750 / 1000;
        tile_height = picture.get_size_y() * 750 / 1000;
      }
      picture.patno = i;
      picture.x = tile_width * (i % 2);
      picture.y = tile_height * (i / 2);
      picture.x_rep.resize(2); picture.y_rep.resize(2);
      picture.x_rep[0] = -(tile_width * 2 - 1920) / 2;
      picture.y_rep[0] = -(tile_height * 2 - 1080) / 2;
      picture.x_rep[1] = -150; picture.y_rep[1] = 270;
    }
  )");

  for (int i = 0; i < 4; ++i) {
    SCOPED_TRACE(i);
    auto& object = Object(i);
    EXPECT_EQ(object.GetDrawer().BuildRenderGeometry(object, {}).dst,
              Rect::REC(-630 + (i % 2) * 1440, (i / 2) * 810, 1440, 810));
  }
}

}  // namespace
