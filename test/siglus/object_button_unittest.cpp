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
#include "core/gameexe.hpp"
#include "core/group.hpp"
#include "core/input.hpp"
#include "core/interaction_manager.hpp"
#include "core/object_internal/drawer/file.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "serialization_test_helpers.hpp"
#include "systems/sdl/sdl_surface.hpp"

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

namespace {
using namespace libsiglus;
using namespace libsiglus::binding;

class SiglusObjectButtonTest : public ::testing::Test {
 protected:
  void SetUp() override {
    runtime.GameplayMachine().vm = std::make_unique<serilang::VM>(
        std::make_shared<serilang::GarbageCollector>());
    runtime.stage = std::make_unique<Stage>(4);
    runtime.counter_list = std::make_shared<CounterList>(0);
    runtime.frame_action_manager = std::make_shared<FrameActionManager>(
        *runtime.stage, runtime.local_config, 0);
    for (auto key : {"0_memory", "1_counter", "1_frame_action", "1_object"})
      (*SiglusBindingRegistry::Find(key))(runtime.GameplayMachine());
    Run("a = Object(); a.set_button(7); a.disp = 1;");
    auto* raw = SDL_CreateSurface(20, 20, SDL_PIXELFORMAT_ARGB8888);
    SDL_FillSurfaceRect(raw, nullptr, 0xffffffff);
    surface = std::make_shared<SDLSurface>(raw);
    Object().SetDrawer(
        std::make_unique<GraphicsObjectOfFile>("button", surface));
    runtime.stage->groups[kLayerFg].resize(1);
    group = runtime.stage->groups.Get(kLayerFg, 0);
    group->status = Group::Status::Active;
    interaction = std::make_unique<InteractionManager>(
        *runtime.stage, input, ButtonActionTable::ParseSiglus(gameexe));
  }
  void Run(const std::string& text) {
    Execute(*runtime.GameplayMachine().vm, text);
  }
  GraphicsObject& Object() { return *runtime.stage->objects.Get(kLayerFg, 0); }
  void Press() {
    input.mouse_pos = Point(5, 5);
    input.decide = {.down = true, .on_down = true};
    interaction->Update();
    input.decide.ResetEdges();
  }
  void DragOutside() {
    input.mouse_pos = Point(100, 100);
    interaction->Update();
  }
  void Release() {
    input.decide = {.on_up = true, .down_up = true};
    interaction->Update();
  }
  SiglusRuntime runtime;
  Gameexe gameexe;
  InputListener input;
  std::shared_ptr<SDLSurface> surface;
  std::shared_ptr<Group> group;
  std::unique_ptr<InteractionManager> interaction;
};

TEST_F(SiglusObjectButtonTest, PushKeepRetainsPressedStateOutside) {
  // A captured push-keep button stays pressed after dragging outside its
  // bounds.
  Run("a.set_button_pushkeep(1);");
  Press();
  DragOutside();
  EXPECT_EQ(group->pushed_button_no, 7);
  EXPECT_EQ(Object().Param().button.real_state, ButtonState::Push);
}

TEST_F(SiglusObjectButtonTest, PushKeepDecidesOnReleaseOutside) {
  // Releasing outside a retained button completes its original group selection.
  Run("a.set_button_pushkeep(1);");
  Press();
  DragOutside();
  Release();
  EXPECT_EQ(group->decided_button_no, 7);
}

TEST_F(SiglusObjectButtonTest, OrdinaryButtonDoesNotDecideOutside) {
  // Without push-keep, dragging outside clears the push and release cannot
  // decide.
  Run("a.set_button_pushkeep(0);");
  Press();
  DragOutside();
  EXPECT_FALSE(group->pushed_button_no.has_value());
  Release();
  EXPECT_FALSE(group->decided_button_no.has_value());
}

TEST_F(SiglusObjectButtonTest, HiddenButtonLosesPushKeepCapture) {
  // Hiding a captured button makes it unavailable even when push-keep is
  // enabled.
  Run("a.set_button_pushkeep(1);");
  Press();
  Run("a.disp = 0;");
  DragOutside();
  Release();
  EXPECT_FALSE(group->decided_button_no.has_value());
}

TEST_F(SiglusObjectButtonTest, ClearButtonResetsPushKeep) {
  // Clearing button options restores the default push-keep value of zero.
  Run("a.set_button_pushkeep(1); a.clear_button(); value = "
      "a.get_button_pushkeep();");
  EXPECT_EQ(*runtime.GameplayMachine().vm->globals_->at("value").Get_if<int>(),
            0);
}

TEST_F(SiglusObjectButtonTest, ButtonAlphaTestRejectsTransparentPixels) {
  // Button alpha testing ignores transparent pixels without changing object
  // alpha testing.
  surface->Fill(RGBAColour::Clear());
  Run("a.alpha_test = 0; a.set_button_alpha_test(1);");
  Press();
  EXPECT_FALSE(group->hit_button_no);
  EXPECT_EQ(Object().Param().alpha_test, 0);
}

TEST_F(SiglusObjectButtonTest, DisabledButtonAlphaTestUsesRectangle) {
  // An opaque-policy button uses its rectangle even if ordinary object alpha
  // testing is enabled.
  surface->Fill(RGBAColour::Clear());
  Run("a.alpha_test = 1; a.set_button_alpha_test(0);");
  Press();
  EXPECT_EQ(group->hit_button_no, 7);
  EXPECT_FALSE(Object().GetDrawer().HitTest(Object(), Point(5, 5)));
}

TEST_F(SiglusObjectButtonTest, ButtonAlphaTestTracksTransformedPixels) {
  // A scaled and translated button samples the corresponding source pixel.
  surface->Fill(RGBAColour::Clear());
  surface->Fill(RGBAColour::White(), Rect::REC(0, 0, 5, 5));
  Run("a.set_button_alpha_test(1); a.set_scale(2000,2000); a.set_pos(30,40);");
  input.mouse_pos = Point(32, 42);
  interaction->Update();
  EXPECT_EQ(group->hit_button_no, 7);
  input.mouse_pos = Point(60, 70);
  interaction->Update();
  EXPECT_FALSE(group->hit_button_no);
}

TEST_F(SiglusObjectButtonTest, ButtonAlphaTestSurvivesArchive) {
  // The independent integer alpha policy survives binding queries and
  // serialization.
  Run("a.set_button_alpha_test(3); value = a.get_button_alpha_test();");
  EXPECT_EQ(*runtime.GameplayMachine().vm->globals_->at("value").Get_if<int>(),
            3);
  serialization_test::ForEachFormat([&](auto format) {
    ButtonProperties restored;
    serialization_test::RoundTrip(Object().Param().button, restored, format);
    EXPECT_EQ(restored, Object().Param().button);
  });
}

TEST_F(SiglusObjectButtonTest, ClearButtonResetsAlphaTest) {
  // Clearing the button returns its alpha-test policy to the legacy rectangle
  // default.
  Run("a.set_button_alpha_test(1); a.clear_button(); value = "
      "a.get_button_alpha_test();");
  EXPECT_EQ(*runtime.GameplayMachine().vm->globals_->at("value").Get_if<int>(),
            0);
}

}  // namespace
