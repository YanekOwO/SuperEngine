// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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
//
// -----------------------------------------------------------------------

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "core/image_resources.hpp"
#include "libsiglus/bindings/flow.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "m6/compiler_pipeline.hpp"
#include "m6/vm_factory.hpp"
#include "mock_asset_scanner.hpp"
#include "mock_graphics_backend.hpp"
#include "siglus/fake_scene.hpp"
#include "systems/sdl/sdl_surface.hpp"

#include <filesystem>
#include <memory>
#include <set>
#include <string>

namespace libsiglus::binding {
namespace sr = serilang;
using ::testing::Return;
using ::testing::StrictMock;

class SiglusG00bufBindingTest : public ::testing::Test {
 protected:
  SiglusMachine runtime;
  Archive archive{std::string(sizeof(Pack_hdr), '\0'), xorkey_t{}};
  std::shared_ptr<StrictMock<MockAssetScanner>> scanner =
      std::make_shared<StrictMock<MockAssetScanner>>();
  std::shared_ptr<StrictMock<MockGraphicsBackend>> backend =
      std::make_shared<StrictMock<MockGraphicsBackend>>();

  void SetUp() override {
    runtime.gameexe = std::make_shared<Gameexe>();
    runtime.gameexe->SetIntAt("G00BUF.CNT", 3);
    runtime.images =
        std::make_shared<ImageResources>(scanner, backend, ToneCurve{});
    runtime.vm = std::make_unique<sr::VM>(m6::VMFactory::Create());
    runtime.loader = std::make_unique<Loader>(archive, *runtime.vm, false);
    for (const auto* name : {"0_memory", "flow", "1_g00buf"}) {
      const auto* bind = SiglusBindingRegistry::Find(name);
      ASSERT_NE(bind, nullptr) << name;
      (*bind)(runtime);
    }
  }

  void Run(std::string source) {
    m6::CompilerPipeline compiler(runtime.vm->gc_);
    compiler.compile(m6::SourceBuffer::Create(std::move(source), "<g00buf>"));
    ASSERT_TRUE(compiler.Ok()) << compiler.FormatErrors();
    runtime.vm->Evaluate(compiler.Get());
  }

  void ExpectLoad(const std::string& name) {
    const auto path = std::filesystem::path("assets") / (name + ".g00");
    EXPECT_CALL(*scanner, FindFile(name, (std::set<std::string>{"g00", "pdt"})))
        .WillOnce(Return(path));
    EXPECT_CALL(*backend, LoadSurface(path)).WillOnce([](const auto&) {
      return std::make_shared<SDLSurface>(Size(1, 1));
    });
  }
};

TEST_F(SiglusG00bufBindingTest, ExactLoadElementRetainsImageForNamedLookup) {
  // Serialized LOAD receives a string argument; the loaded image must remain
  // alive and serve later rendering lookups without another decode.
  FakeScene scene{.scene_name = "preload",
                  .command_name = "preload",
                  .args = {Type::String}};
  scene.AppendElement({124, -1, 0, 0});
  scene.AppendElement({83, 0x7d000000});
  scene.AppendByte(ByteCode::Property);
  scene.AppendByte(ByteCode::Cmd);
  for (int value :
       {0, 1, static_cast<int>(Type::String), 0, static_cast<int>(Type::None)})
    scene.AppendInt(value);
  scene.AddTo(archive);
  (*runtime.vm->globals_)["preload"] =
      ResolveSceneCommand(*runtime.loader, "preload", "preload");
  ExpectLoad("line");

  Run("preload(\"line\");");

  auto image = runtime.images->FindPreloaded("line");
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(runtime.images->GetSurfaceNamed("line"), image);
}

TEST_F(SiglusG00bufBindingTest, SizeUsesConfiguredCapacity) {
  // Three configured slots are exposed without loading any image.
  Run("count = g00buf.size();");
  EXPECT_EQ(runtime.vm->globals_->at("count"), sr::Value(3));
}

TEST_F(SiglusG00bufBindingTest, ReplacingSlotReleasesPreviousImage) {
  // Load two images into one slot; only the replacement remains retained.
  ExpectLoad("old");
  Run("g00buf[0].load(\"old\");");
  std::weak_ptr<SDLSurface> old = runtime.images->FindPreloaded("old");
  ExpectLoad("new");
  Run("g00buf[0].load(\"new\");");
  EXPECT_TRUE(old.expired());
  EXPECT_NE(runtime.images->FindPreloaded("new"), nullptr);
}

TEST_F(SiglusG00bufBindingTest, FreeReleasesOnlySelectedSlot) {
  // Populate two slots then free the first; the second image remains retained.
  ExpectLoad("first");
  ExpectLoad("second");
  Run("g00buf[0].load(\"first\"); g00buf[1].load(\"second\");");
  Run("g00buf[0].free();");
  EXPECT_EQ(runtime.images->FindPreloaded("first"), nullptr);
  EXPECT_NE(runtime.images->FindPreloaded("second"), nullptr);
}

TEST_F(SiglusG00bufBindingTest, FreeAllReleasesAllSlots) {
  // Populate two slots then free all; both image references must be released.
  ExpectLoad("first");
  ExpectLoad("second");
  Run("g00buf[0].load(\"first\"); g00buf[1].load(\"second\");");
  Run("g00buf.free_all();");
  EXPECT_EQ(runtime.images->FindPreloaded("first"), nullptr);
  EXPECT_EQ(runtime.images->FindPreloaded("second"), nullptr);
}

TEST_F(SiglusG00bufBindingTest, LocalResetReleasesPreloads) {
  // A preloaded image is held across a local reset; reset must release it.
  ExpectLoad("line");
  Run("g00buf[0].load(\"line\");");
  for (auto& reset : runtime.reset_local_memory)
    reset();
  EXPECT_EQ(runtime.images->FindPreloaded("line"), nullptr);
}

TEST_F(SiglusG00bufBindingTest, InvalidIndicesAreRejectedBeforeLoading) {
  // Negative and capacity-sized indices cannot address slots or load images.
  for (int index : {-1, 3}) {
    Run("caught = 0; try { g00buf[" + std::to_string(index) +
        "].load(\"line\"); } catch (e) { caught = 1; }");
    EXPECT_EQ(runtime.vm->globals_->at("caught"), sr::Value(1));
  }
}

}  // namespace libsiglus::binding
