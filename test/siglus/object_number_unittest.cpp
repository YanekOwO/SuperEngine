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

#include "core/asset_scanner.hpp"
#include "core/frame_action_manager.hpp"
#include "core/object_internal/drawer/number.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "mock_graphics_backend.hpp"
#include "number_test_support.hpp"
#include "systems/igraphics_backend.hpp"
#include "vm/string.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace libsiglus::binding {

class SiglusObjectNumberTest : public ::testing::Test {
 protected:
  SiglusMachine runtime;
  std::shared_ptr<SDLSurface> atlas = MakeNumberTestAtlas();
  std::shared_ptr<::testing::NiceMock<::MockGraphicsBackend>> backend =
      std::make_shared<::testing::NiceMock<::MockGraphicsBackend>>();

  void SetUp() override {
    runtime.vm = std::make_unique<serilang::VM>(
        std::make_shared<serilang::GarbageCollector>());
    runtime.stage = std::make_unique<Stage>(3);
    runtime.counter_list = std::make_shared<CounterList>(0);
    runtime.frame_action_manager = std::make_shared<FrameActionManager>(
        *runtime.stage, runtime.local_config, 0);
    auto scanner = std::make_shared<AssetScanner>();
    scanner->filesystem_cache_.emplace("font",
                                       std::make_pair("g00", "font.g00"));
    runtime.images =
        std::make_shared<ImageResources>(scanner, backend, ToneCurve());
    ON_CALL(*backend, LoadSurface(::testing::_))
        .WillByDefault(::testing::Return(atlas));
    for (auto key : {"0_memory", "1_counter", "1_frame_action", "1_object"}) {
      auto* bind = SiglusBindingRegistry::Find(key);
      ASSERT_NE(bind, nullptr);
      (*bind)(runtime);
    }

    Run("a = Object();");
  }

  void Run(const std::string& code) { Execute(*runtime.vm, code); }

  GraphicsObject& Object(int index = 0) {
    return *runtime.stage->objects.Get(kLayerFg, index);
  }

  NumberDrawer& Number(int index = 0) {
    return *Object(index).GetDrawer<NumberDrawer>();
  }

  int Int(std::string_view name) {
    return *runtime.vm->globals_->at(std::string(name)).Get_if<int>();
  }
};

TEST_F(SiglusObjectNumberTest, SupportsAllCreateForms) {
  // Every arity creates a number and applies its optional initial pose.
  Run(R"(
    a.create_number("font");
    b = Object(0, 1); b.create_number("font", 1);
    c = Object(0, 2); c.create_number("font", 1, 490, 305);
  )");
  ASSERT_NE(Object().GetDrawer<NumberDrawer>(), nullptr);
  ASSERT_NE(Object(1).GetDrawer<NumberDrawer>(), nullptr);
  ASSERT_NE(Object(2).GetDrawer<NumberDrawer>(), nullptr);
  EXPECT_EQ(Object().Param().visible(), 0);
  EXPECT_EQ(Object(1).Param().visible(), 1);
  EXPECT_EQ(Object(2).Param().x(), 490);
  EXPECT_EQ(Object(2).Param().y(), 305);
}

TEST_F(SiglusObjectNumberTest, NewObjectsHaveLegacyNumberDefaults) {
  // Creation resets the numeric value and all formatting fields to zero.
  Run("a.create_number(\"font\"); value = a.get_number();");
  EXPECT_EQ(Int("value"), 0);
  EXPECT_EQ(Number().style(), NumberObjectStyle());
}

TEST_F(SiglusObjectNumberTest, DayOverlaySequenceUpdatesNestedObject) {
  Run(R"(
    a.child.resize(6); day = a.child[5];
    day.create_number("font", 1, 490, 305);
    day.set_number_param(1, 1, 0, 1, 0, 11);
    day.set_number(2); value = day.get_number();
  )");
  const auto* day = Object().GetChild(5);
  ASSERT_NE(day, nullptr);
  const auto* drawer = day->GetDrawer<NumberDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(Int("value"), 2);
  EXPECT_EQ(drawer->style(), (NumberObjectStyle{1, true, false, true, 0, 11}));
  EXPECT_EQ(day->Param().x(), 490);
  EXPECT_EQ(day->Param().y(), 305);
}

TEST_F(SiglusObjectNumberTest, UpdatesPreserveChildrenPoseAndActions) {
  // Value and style changes retain the number's existing runtime state.
  Run(R"(
    a.create_number("font", 1, 20, 30);
    a.child.resize(1); a.child[0].create_string("child");
    action = a.frame_action; action.counter.set(29);
    a.set_number(-12); a.set_number_param(4, 0, 1, 1, 1, 10);
  )");
  EXPECT_EQ(Object().Param().x(), 20);
  EXPECT_EQ(Object().Param().y(), 30);
  EXPECT_TRUE(Object().HasChildren());
  EXPECT_EQ(Object().GetFrameAction()->GetCounter()->Get(), 29);
}

TEST_F(SiglusObjectNumberTest, RecreatingSameFilenameResetsState) {
  // Recreating an identical atlas resets value, formatting, pose, and children.
  Run(R"(
    a.create_number("font", 1, 20, 30); a.set_number(123);
    a.set_number_param(4, 1, 1, 1, 1, 10);
    a.child.resize(1); a.child[0].create_string("child");
    a.create_number("font");
  )");
  EXPECT_EQ(Number().value(), 0);
  EXPECT_EQ(Number().style(), NumberObjectStyle());
  EXPECT_EQ(Object().Param().visible(), 0);
  EXPECT_EQ(Object().Param().x(), 0);
  EXPECT_EQ(Object().Param().y(), 0);
  EXPECT_FALSE(Object().HasChildren());
}

TEST_F(SiglusObjectNumberTest, InvalidCreateArgumentsPreserveObject) {
  // Each malformed creation is rejected before replacing the existing number.
  Run("a.create_number(\"font\", 1, 20, 30); a.set_number(12);");
  for (const auto* code :
       {"a.create_number();", "a.create_number(1);", "a.create_number(\"\");",
        "a.create_number(\"font\", 1, 2);",
        "a.create_number(\"font\", 1, 2, 3, 4);",
        "a.create_number(\"font\", \"bad\");"}) {
    SCOPED_TRACE(code);
    Run("caught = 0; try { " + std::string(code) +
        " } catch(e) { caught = 1; }");
    EXPECT_EQ(Int("caught"), 1);
    EXPECT_EQ(Number().value(), 12);
    EXPECT_EQ(Object().Param().x(), 20);
  }
}

TEST_F(SiglusObjectNumberTest, MissingAtlasPreservesObject) {
  // A failed asset lookup leaves the previous atlas, value, and pose intact.
  Run(R"(
    a.create_number("font", 1, 20, 30); a.set_number(12);
    caught = 0;
    try { a.create_number("missing"); } catch(e) { caught = 1; }
  )");
  EXPECT_EQ(Int("caught"), 1);
  EXPECT_EQ(Number().value(), 12);
  EXPECT_EQ(Object().FilePath(), "font");
  EXPECT_EQ(Object().Param().x(), 20);
}

TEST_F(SiglusObjectNumberTest, InvalidFormattingPreservesNumberState) {
  // Invalid counts, arities, and types cannot partially update numeric state.
  Run("a.create_number(\"font\"); a.set_number(12);");
  for (const auto* code :
       {"a.set_number_param(17, 0, 0, 0, 0, 0);",
        "a.set_number_param(-1, 0, 0, 0, 0, 0);", "a.set_number_param(1, 0);",
        "a.set_number_param(1, 0, 0, 0, 0, \"bad\");",
        "a.set_number(\"bad\");"}) {
    SCOPED_TRACE(code);
    Run("caught = 0; try { " + std::string(code) +
        " } catch(e) { caught = 1; }");
    EXPECT_EQ(Int("caught"), 1);
    EXPECT_EQ(Number().style(), NumberObjectStyle());
    EXPECT_EQ(Number().value(), 12);
  }
}

TEST_F(SiglusObjectNumberTest, CopyFromKeepsIndependentNumericState) {
  // Copying a number shares atlas storage while keeping future edits separate.
  Run(R"(
    a.create_number("font"); a.set_number(12);
    b = Object(0, 1); b.copy_from(a);
    b.set_number(7); b.set_number_param(3, 1, 0, 0, 0, 0);
  )");
  EXPECT_EQ(Number().value(), 12);
  EXPECT_EQ(Number().style(), NumberObjectStyle());
  EXPECT_EQ(Number(1).value(), 7);
  EXPECT_EQ(Number(1).style().digits, 3);
}

TEST_F(SiglusObjectNumberTest, SizeQueriesReturnUnscaledRequestedCut) {
  // Number size getters describe an atlas cut, even after scaling a long value.
  Run(R"(
    a.create_number("font"); a.set_number(12345); a.set_scale(2000, 3000);
    width = a.get_size_x(); height = a.get_size_y();
    cut_width = a.get_size_x(1); cut_height = a.get_size_y(1);
    missing = a.get_size_x(-1);
  )");
  EXPECT_EQ(Int("width"), 10);
  EXPECT_EQ(Int("height"), 14);
  EXPECT_EQ(Int("cut_width"), 11);
  EXPECT_EQ(Int("cut_height"), 15);
  EXPECT_EQ(Int("missing"), 0);
}

TEST_F(SiglusObjectNumberTest, FilePathQueryReturnsAtlasFilename) {
  // Number objects expose the filename used to create their atlas.
  Run("a.create_number(\"font\"); path = a.get_file_path();");
  EXPECT_EQ(runtime.vm->globals_->at("path").Get_if<serilang::String>()->str_,
            "font");
}

TEST_F(SiglusObjectNumberTest, PixelQueriesSampleRequestedDigitCut) {
  // Number pixel queries address the original atlas rather than laid-out
  // digits.
  Run(R"(
    a.create_number("font"); path = a.get_file_path();
    red = a.get_pixel_color_r(0, 0, 1);
  )");
  EXPECT_EQ(Int("red"), 2);
}

TEST_F(SiglusObjectNumberTest, NonNumberSettersLeaveOtherDrawersIntact) {
  // Number commands on a string leave its data intact and read back zero.
  Run(R"(
    a.create_string("text"); a.set_number(12);
    a.set_number_param(3, 1, 1, 1, 0, 0);
    value = a.get_number(); text = a.get_string();
  )");
  EXPECT_EQ(Int("value"), 0);
  EXPECT_EQ(runtime.vm->globals_->at("text").Get_if<serilang::String>()->str_,
            "text");
}

TEST_F(SiglusObjectNumberTest, FreeClearsNumericDataAndPath) {
  // Freeing a number releases its drawer and restores empty-object readback.
  Run(R"(
    a.create_number("font"); a.set_number(12); a.free();
    value = a.get_number(); exists = a.exist_type();
  )");
  EXPECT_EQ(Int("value"), 0);
  EXPECT_EQ(Int("exists"), 0);
  EXPECT_TRUE(Object().FilePath().empty());
}

}  // namespace libsiglus::binding
