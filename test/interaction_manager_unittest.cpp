// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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

#include "core/gameexe.hpp"
#include "core/input.hpp"
#include "core/interaction_manager.hpp"
#include "core/stage.hpp"
#include "mock_graphics_object_data.hpp"
#include "systems/sdl/sdl_surface.hpp"

#include <memory>

class InteractionManagerTest : public ::testing::Test {
 protected:
  static ButtonActionTable MakeButtonActions() {
    Gameexe gameexe;
    gameexe.parseLine("BUTTON.ACTION.CNT = 1");
    gameexe.parseLine("BUTTON.ACTION.0.NORMAL = 10,0,0,255,0,0");
    gameexe.parseLine("BUTTON.ACTION.0.HIT = 11,0,0,224,16,1");
    gameexe.parseLine("BUTTON.ACTION.0.PUSH = 12,0,0,192,32,2");
    gameexe.parseLine("BUTTON.ACTION.0.SELECT = 13,0,0,160,48,3");
    gameexe.parseLine("BUTTON.ACTION.0.DISABLE = 14,0,0,128,64,4");
    return ButtonActionTable::ParseSiglus(gameexe);
  }

  InteractionManagerTest()
      : surface_(std::make_shared<SDLSurface>(Size(20, 20))),
        manager_(stage_, input_, MakeButtonActions()) {
    surface_->Fill(RGBAColour(255, 255, 255, 255));
    input_.mouse_pos = Point(15, 25);
  }

  Group& AddGroup(int layer = kLayerFg,
                  Group::Status status = Group::Status::Active) {
    auto& groups = stage_.groups[layer];
    const size_t index = groups.size();
    groups.resize(index + 1);
    Group& group = *stage_.groups.Get(layer, index);
    group.status = status;
    return group;
  }

  GraphicsObject& AddButton(int object_id,
                            int button_no,
                            int group_no = 0,
                            int layer = kLayerFg) {
    GraphicsObject& object = stage_.GetObject(layer, object_id);
    object.SetDrawer(std::make_unique<MockGraphicsObjectData>(surface_));
    ObjectParameter& param = object.Param();
    param.SetVisible(1);
    param.SetX(10);
    param.SetY(20);
    param.SetButtonOpts(0, 0, group_no, button_no);
    return object;
  }

  Stage stage_{8};
  InputListener input_;
  std::shared_ptr<SDLSurface> surface_;
  InteractionManager manager_;
};

TEST_F(InteractionManagerTest, HoverSelectsEligibleButtonInEveryLayer) {
  for (int layer = kLayerFg; layer <= kLayerNext; ++layer) {
    AddGroup(layer);
    AddButton(0, 10 + layer, 0, layer);
  }

  manager_.Update();

  EXPECT_EQ(stage_.groups[kLayerFg][0]->hit_button_no, 10);
  EXPECT_EQ(stage_.groups[kLayerBg][0]->hit_button_no, 11);
  EXPECT_EQ(stage_.groups[kLayerNext][0]->hit_button_no, 12);
  EXPECT_EQ(stage_.GetObject(kLayerFg, 0).Param().GetPattNo(), 11);
}

TEST_F(InteractionManagerTest, AppliesPushAndDecidedButtonActions) {
  Group& group = AddGroup();
  GraphicsObject& button = AddButton(0, 42);
  input_.decide = {.down = true, .on_down = true};

  manager_.Update();

  EXPECT_EQ(button.Param().GetPattNo(), 12);
  EXPECT_FLOAT_EQ(button.Param().GetNormalizedAlpha(), 192.0f / 255.0f);
  EXPECT_FLOAT_EQ(button.Param().GetNormalizedBright(), 32.0f / 255.0f);
  EXPECT_FLOAT_EQ(button.Param().GetNormalizedDark(), 2.0f / 255.0f);

  input_.decide = {.on_up = true, .down_up = true};
  manager_.Update();

  EXPECT_EQ(group.decided_button_no, 42);
  EXPECT_EQ(button.Param().GetPattNo(), 12);
}

TEST_F(InteractionManagerTest, AppliesExplicitAndInheritedButtonStates) {
  AddGroup();
  GraphicsObject& selected = AddButton(0, 10);
  selected.Param().SetButtonState(3);
  selected.ResetChildren(1);
  GraphicsObject& selected_child = selected.TouchChild(0);
  selected_child.Param().SetButtonOpts(0, 0, 0, 11);

  GraphicsObject& disabled = AddButton(1, 20);
  disabled.Param().SetButtonState(4);

  manager_.Update();

  EXPECT_EQ(selected.Param().GetPattNo(), 13);
  EXPECT_EQ(selected_child.Param().GetPattNo(), 13);
  EXPECT_EQ(disabled.Param().GetPattNo(), 14);
}

TEST_F(InteractionManagerTest, ClearsStaleOverridesWhenButtonIsCleared) {
  AddGroup();
  GraphicsObject& button = AddButton(0, 10);
  button.Param().SetButtonOverrides(99, 0, 0, 1, 2, 3);
  ButtonProperties properties = button.Param().ButtonProperty();
  properties.is_button = 0;
  button.Param().SetButtonProperty(properties);

  manager_.Update();

  EXPECT_FALSE(button.Param().GetButtonUsingOverides());
}

TEST_F(InteractionManagerTest, HoverIgnoresIneligibleButtons) {
  Group& group = AddGroup();
  GraphicsObject& invisible = AddButton(0, 10);
  invisible.Param().SetVisible(0);
  GraphicsObject& click_disabled = AddButton(1, 11);
  click_disabled.Param().SetClickDisable(1);
  GraphicsObject& selected = AddButton(2, 12);
  selected.Param().SetButtonState(3);
  GraphicsObject& disabled = AddButton(3, 13);
  disabled.Param().SetButtonState(4);
  AddButton(4, 14).Param().SetButtonOpts(-1, 0, 0, 14);

  manager_.Update();

  EXPECT_EQ(group.hit_button_no, std::nullopt);
}

TEST_F(InteractionManagerTest, HoverChoosesHighestOrderedOverlappingButton) {
  Group& group = AddGroup();
  GraphicsObject& lower = AddButton(1, 10);
  lower.Param().SetZOrder(5);
  lower.Param().SetZLayer(9);
  GraphicsObject& higher = AddButton(0, 11);
  higher.Param().SetZOrder(6);
  higher.Param().SetZLayer(-100);

  manager_.Update();

  EXPECT_EQ(group.hit_button_no, 11);
}

TEST_F(InteractionManagerTest, PressAndReleaseOnSameButtonDecidesGroup) {
  Group& group = AddGroup();
  AddButton(0, 42);
  input_.decide = {.down = true, .on_down = true};

  manager_.Update();

  EXPECT_EQ(group.hit_button_no, 42);
  EXPECT_EQ(group.pressed_button_no, 42);
  EXPECT_EQ(group.pushed_button_no, 42);

  input_.decide = {.on_up = true, .down_up = true};
  manager_.Update();

  EXPECT_EQ(group.status, Group::Status::Disabled);
  EXPECT_EQ(group.result, Group::Result::Decided);
  EXPECT_EQ(group.decided_button_no, 42);
  EXPECT_EQ(group.result_button_no, 42);
  EXPECT_FALSE(group.cancel_enabled);
  EXPECT_EQ(group.hit_button_no, std::nullopt);
  EXPECT_EQ(group.pressed_button_no, std::nullopt);
  EXPECT_EQ(group.pushed_button_no, std::nullopt);
}

TEST_F(InteractionManagerTest, ReleaseAwayFromPressedButtonDoesNotDecide) {
  Group& group = AddGroup();
  AddButton(0, 42);
  input_.decide = {.down = true, .on_down = true};
  manager_.Update();

  input_.mouse_pos = Point(100, 100);
  input_.decide = {.on_up = true, .down_up = true};
  manager_.Update();

  EXPECT_EQ(group.status, Group::Status::Active);
  EXPECT_EQ(group.result, Group::Result::None);
  EXPECT_EQ(group.decided_button_no, std::nullopt);
  EXPECT_EQ(group.hit_button_no, std::nullopt);
  EXPECT_EQ(group.pressed_button_no, std::nullopt);
  EXPECT_EQ(group.pushed_button_no, std::nullopt);
}

TEST_F(InteractionManagerTest, SameFrameClickDecidesHoveredButton) {
  Group& group = AddGroup();
  AddButton(0, 7);
  input_.decide = {.on_down = true, .on_up = true, .down_up = true};

  manager_.Update();

  EXPECT_EQ(group.status, Group::Status::Disabled);
  EXPECT_EQ(group.result, Group::Result::Decided);
  EXPECT_EQ(group.result_button_no, 7);
}

TEST_F(InteractionManagerTest, CancelDisablesGroupAndReportsHoveredButton) {
  Group& group = AddGroup();
  group.cancel_enabled = true;
  AddButton(0, 21);
  input_.cancel = {.on_up = true, .down_up = true};

  manager_.Update();

  EXPECT_EQ(group.status, Group::Status::Disabled);
  EXPECT_EQ(group.result, Group::Result::Canceled);
  EXPECT_EQ(group.decided_button_no, -1);
  EXPECT_EQ(group.result_button_no, 21);
  EXPECT_FALSE(group.cancel_enabled);
  EXPECT_EQ(group.hit_button_no, std::nullopt);
}

TEST_F(InteractionManagerTest, DisabledGroupIsNotModified) {
  Group& group = AddGroup(kLayerFg, Group::Status::Disabled);
  group.hit_button_no = 91;
  group.pressed_button_no = 92;
  group.pushed_button_no = 93;
  AddButton(0, 42);
  input_.decide = {.down = true, .on_down = true};
  input_.cancel = {.on_up = true, .down_up = true};

  manager_.Update();

  EXPECT_EQ(group.status, Group::Status::Disabled);
  EXPECT_EQ(group.result, Group::Result::None);
  EXPECT_EQ(group.hit_button_no, 91);
  EXPECT_EQ(group.pressed_button_no, 92);
  EXPECT_EQ(group.pushed_button_no, 93);
}
