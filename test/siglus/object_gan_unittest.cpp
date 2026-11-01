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

#include "core/frame_action_manager.hpp"
#include "core/object_internal/drawer/gan.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "mock_graphics_backend.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "utilities/bytestream.hpp"
#include "utilities/file.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace libsiglus::binding {

class SiglusObjectGanTest : public ::testing::Test {
 protected:
  ScopedTemporary files;
  SiglusMachine runtime;
  std::shared_ptr<SDLSurface> atlas = std::make_shared<SDLSurface>(Size(4, 3));
  std::shared_ptr<::testing::StrictMock<MockGraphicsBackend>> backend =
      std::make_shared<::testing::StrictMock<MockGraphicsBackend>>();

  void SetUp() override {
    runtime.vm = std::make_unique<serilang::VM>(
        std::make_shared<serilang::GarbageCollector>());
    runtime.stage = std::make_unique<Stage>(1);
    runtime.counter_list = std::make_shared<CounterList>(0);
    runtime.frame_action_manager = std::make_shared<FrameActionManager>(
        *runtime.stage, runtime.local_config, 0);
    runtime.asset_scanner = std::make_shared<AssetScanner>();
    runtime.asset_scanner->filesystem_cache_.emplace(
        "display_atlas", std::make_pair("g00", "display_atlas.g00"));
    runtime.images = std::make_shared<ImageResources>(runtime.asset_scanner,
                                                      backend, ToneCurve());
    atlas->Fill(RGBAColour(10, 20, 30, 255));
    EXPECT_CALL(*backend,
                LoadSurface(std::filesystem::path("display_atlas.g00")))
        .WillOnce(::testing::Return(atlas));
    for (auto key : {"0_memory", "1_counter", "1_frame_action", "1_object"}) {
      auto* bind = SiglusBindingRegistry::Find(key);
      ASSERT_NE(bind, nullptr);
      (*bind)(runtime);
    }

    Run("object = Object(); object.create(\"display_atlas\", 1, 20, 30);");

    MakeGan("motion", "editor_atlas");
  }

  void MakeGan(const std::string& name, const std::string& editor_image) {
    // One set with a single opaque frame, independent of game assets.
    oBytestream data;
    data << 10000 << 10000 << 10100 << static_cast<int>(editor_image.size() + 1)
         << editor_image << '\0' << 20000 << 1 << 30000 << 1 << 30100 << 0
         << 30101 << 0 << 30102 << 0 << 30103 << 100 << 30104 << 255 << 30105
         << 0 << 999999;
    const auto path = files.dir() / (name + ".gan");
    WriteFile(path, data.Get());
    runtime.asset_scanner->filesystem_cache_.emplace(
        name, std::make_pair("gan", path));
  }

  inline void Run(const std::string& code) { Execute(*runtime.vm, code); }

  inline GraphicsObject& Object() {
    return *runtime.stage->objects.Get(kLayerFg, 0);
  }
};

TEST_F(SiglusObjectGanTest, MissingEditorImageDoesNotReplaceAssignedImage) {
  // The GAN names a nonexistent editor image; loading it must animate the
  // image assigned by create rather than attempting that stale asset lookup.
  ASSERT_NO_THROW(Run("object.load_gan(\"motion\");"));

  auto* gan = Object().GetDrawer<GanGraphicsObjectData>();
  ASSERT_NE(gan, nullptr);
  EXPECT_EQ(gan->GetPixelColor(Point(0, 0), 0), RGBAColour(10, 20, 30, 255));
  EXPECT_EQ(Object().PixelWidth(), 4);
}

TEST_F(SiglusObjectGanTest, AvailableEditorImageDoesNotOverrideAssignedImage) {
  // Even when the header's editor image exists, loading the GAN must keep the
  // explicitly assigned atlas and never request the editor image's surface.
  runtime.asset_scanner->filesystem_cache_.emplace(
      "editor_atlas", std::make_pair("g00", "editor_atlas.g00"));
  EXPECT_CALL(*backend, LoadSurface(std::filesystem::path("editor_atlas.g00")))
      .Times(0);

  ASSERT_NO_THROW(Run("object.load_gan(\"motion\");"));

  auto* gan = Object().GetDrawer<GanGraphicsObjectData>();
  ASSERT_NE(gan, nullptr);
  EXPECT_EQ(gan->GetPixelColor(Point(0, 0), 0), RGBAColour(10, 20, 30, 255));
}

TEST_F(SiglusObjectGanTest, ReloadRetainsAssignedImageIdentity) {
  // Loading a second animation with another name must reuse the original
  // object image instead of losing its identity on the first GAN load.
  MakeGan("another_motion", "another_editor_atlas");
  ASSERT_NO_THROW(Run("object.load_gan(\"motion\");"));

  ASSERT_NO_THROW(Run("object.load_gan(\"another_motion\");"));

  EXPECT_EQ(Object().FilePath(), "display_atlas");
  EXPECT_EQ(Object().PixelWidth(), 4);
}

TEST_F(SiglusObjectGanTest, LoadingAnimationPreservesObjectState) {
  // Adding animation must retain the object's children, pose, and frame
  // action rather than treating load_gan as object recreation.
  Run("object.child.resize(1); object.child[0].create_string(\"child\"); "
      "object.frame_action.counter.set(29);");
  auto child = Object().GetChildren()[0];

  ASSERT_NO_THROW(Run("object.load_gan(\"motion\");"));

  EXPECT_EQ(Object().GetChild(0), child.get());
  EXPECT_EQ(Object().Param().x(), 20);
  EXPECT_EQ(Object().Param().y(), 30);
  EXPECT_EQ(Object().Param().visible(), 1);
  EXPECT_EQ(Object().GetFrameAction()->GetCounter()->Get(), 29);
}

TEST_F(SiglusObjectGanTest, MissingAnimationPreservesAssignedImage) {
  // Failure to resolve the GAN itself must report an error and leave the
  // current image intact, without suppressing genuine missing-asset errors.
  auto* original = Object().GetDrawer<>();

  EXPECT_THROW(Run("object.load_gan(\"missing_motion\");"), std::exception);

  EXPECT_EQ(Object().GetDrawer<>(), original);
  EXPECT_EQ(Object().FilePath(), "display_atlas");
}

}  // namespace libsiglus::binding
