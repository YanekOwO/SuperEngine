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

TEST_F(SiglusObjectButtonTest, TopmostGroupOwnsOverlappingButton) {
  // Two groups overlap; only the visually topmost button captures the click.
  runtime.stage->groups[kLayerFg].resize(2);
  auto other = runtime.stage->groups.Get(kLayerFg, 1);
  other->status = Group::Status::Active;
  auto button = std::make_shared<GraphicsObject>(Object().Clone());
  button->Param().button.group = 1;
  button->Param().button.button_number = 9;
  runtime.stage->objects[kLayerFg][1] = std::move(button);
  Press();
  Release();
  EXPECT_FALSE(group->decided_button_no);
  EXPECT_EQ(other->decided_button_no, 9);
}

TEST_F(SiglusObjectButtonTest, CancelUsesHighestPriorityOnce) {
  // One cancel reaches the highest-priority group and plays its sound once.
  runtime.stage->groups[kLayerFg].resize(2);
  auto other = runtime.stage->groups.Get(kLayerFg, 1);
  other->status = Group::Status::Waiting;
  other->cancel_enabled = group->cancel_enabled = true;
  other->cancel_priority = 2;
  other->cancel_se = 8;
  std::vector<int> sounds;
  interaction->SetGroupHandlers([&](int se) { sounds.push_back(se); }, {});
  input.cancel = {.on_up = true, .down_up = true};
  interaction->Update();
  interaction->Update();
  EXPECT_EQ(other->result, Group::Result::Canceled);
  EXPECT_EQ(group->status, Group::Status::Active);
  EXPECT_EQ(sounds, std::vector<int>({8}));
}

TEST_F(SiglusObjectButtonTest, BackgroundGroupDoesNotTakeInput) {
  // Foreground interaction must leave a background group available after a
  // click.
  runtime.stage->groups[kLayerBg].resize(1);
  auto background = runtime.stage->groups.Get(kLayerBg, 0);
  background->status = Group::Status::Active;
  runtime.stage->objects[kLayerBg][0] =
      std::make_shared<GraphicsObject>(Object().Clone());
  Press();
  Release();
  EXPECT_EQ(group->decided_button_no, 7);
  EXPECT_FALSE(background->decided_button_no);
}

TEST_F(SiglusObjectButtonTest, DecisionConsumesInput) {
  // A completed selection clears the shared input and invokes script-input
  // consumption before another control can see the release.
  int consumed = 0;
  interaction->SetGroupHandlers({}, [&] { ++consumed; });
  Press();
  Release();
  EXPECT_EQ(consumed, 1);
  EXPECT_FALSE(input.decide.on_up);
  EXPECT_FALSE(input.decide.down_up);
}

TEST_F(SiglusObjectButtonTest, ActiveGroupConsumesDialogueClickEvent) {
  // A group owns click events before lower-priority dialogue waits receive
  // them, while its live input remains available to the selection update.
  EXPECT_TRUE(interaction->HandleMwndButtonEvent(MouseDown{MouseButton::LEFT}));
  Press();
  Release();
  EXPECT_EQ(group->decided_button_no, 7);
}

TEST_F(SiglusObjectButtonTest, RectangleHitExcludesStoredCutPadding) {
  // A cropped button uses its stored rectangle, including transparent pixels
  // inside that rectangle, without capturing the surrounding logical canvas.
  auto* raw = SDL_CreateSurface(20, 20, SDL_PIXELFORMAT_ARGB8888);
  auto cropped = std::make_shared<SDLSurface>(
      raw, std::vector<GrpRect>{{.rect = Rect::REC(0, 0, 20, 20),
                                 .pixel_region = PixelRegion{
                                     Rect::REC(8, 10, 4, 3), Point(-8, -10)}}});
  cropped->Fill(RGBAColour::Clear());
  Object().SetDrawer(std::make_unique<GraphicsObjectOfFile>("cut", cropped));
  Run("a.set_button_alpha_test(0); a.set_scale(2000,2000); a.set_pos(30,40);");
  input.mouse_pos = Point(48, 62);
  interaction->Update();
  ASSERT_EQ(group->hit_button_no, 7);
  input.mouse_pos = Point(32, 42);
  interaction->Update();
  EXPECT_FALSE(group->hit_button_no);
}

TEST_F(SiglusObjectButtonTest, StoredCutHitTracksPatternAndOrigin) {
  // The second atlas cut has a nonzero origin; its cropped hit rectangle
  // follows the same placement and scaling as the rendered pixels.
  auto* raw = SDL_CreateSurface(40, 40, SDL_PIXELFORMAT_ARGB8888);
  auto cropped = std::make_shared<SDLSurface>(
      raw, std::vector<GrpRect>{{.rect = Rect::REC(0, 0, 20, 20)},
                                {.rect = Rect::REC(20, 20, 20, 20),
                                 .originX = 2,
                                 .originY = 3,
                                 .pixel_region = PixelRegion{
                                     Rect::REC(28, 30, 4, 3), Point(-6, -7)}}});
  cropped->Fill(RGBAColour::Clear());
  Object().SetDrawer(std::make_unique<GraphicsObjectOfFile>("cut", cropped));
  Run("a.patno = 1; a.set_scale(2000,2000); a.set_pos(30,40);");
  input.mouse_pos = Point(44, 56);
  interaction->Update();
  ASSERT_EQ(group->hit_button_no, 7);
  input.mouse_pos = Point(32, 42);
  interaction->Update();
  EXPECT_FALSE(group->hit_button_no);
}

TEST_F(SiglusObjectButtonTest, EmptyStoredCutCannotCapturePointer) {
  // An empty crop inside a nonempty logical canvas has no button hit area.
  auto* raw = SDL_CreateSurface(20, 20, SDL_PIXELFORMAT_ARGB8888);
  auto cropped = std::make_shared<SDLSurface>(
      raw,
      std::vector<GrpRect>{{.rect = Rect::REC(0, 0, 20, 20),
                            .pixel_region = PixelRegion{Rect(), Point()}}});
  Object().SetDrawer(std::make_unique<GraphicsObjectOfFile>("cut", cropped));
  Press();
  EXPECT_FALSE(group->hit_button_no);
}

}  // namespace
