// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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

#include "core/stage.hpp"
#include "libsiglus/siglus_scene_renderer.hpp"
#include "mock_graphics_object_data.hpp"

#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

class StageTest : public ::testing::Test {
 protected:
  void SetDummyData(GraphicsObject& object) {
    object.SetDrawer(std::make_unique<MockGraphicsObjectData>());
  }

  template <class T>
  std::string GetExistFlags(const LazyArray<T>& la) {
    std::string ret;
    for (std::size_t i = 0, n = la.Size(); i < n; ++i)
      ret += la.Exists(i) ? '1' : '0';
    return ret;
  }
};

TEST_F(StageTest, ObjectLayerHelpersAccessExpectedBuffers) {
  Stage stage(3);

  EXPECT_EQ(&stage.GetObject(kLayerFg, 0), &stage.objects[kLayerFg][0]);
  EXPECT_EQ(&stage.GetObject(kLayerBg, 1), &stage.objects[kLayerBg][1]);
  EXPECT_EQ(&stage.GetObject(kLayerNext, 2), &stage.objects[kLayerNext][2]);

  EXPECT_THROW(stage.GetObject(-1, 0), std::runtime_error);
  EXPECT_THROW(stage.GetObject(3, 0), std::runtime_error);
  EXPECT_THROW(stage.GetFreeObjectId(3), std::runtime_error);
}

TEST_F(StageTest, SetRemoveAndFreeIdUseRequestedLayer) {
  Stage stage(3);
  stage.GetObject(kLayerFg, 0).Param().SetX(7);
  EXPECT_EQ(stage.GetFreeObjectId(kLayerFg), 1);

  GraphicsObject object;
  SetDummyData(object);
  object.Param().SetX(42);
  stage.SetObject(kLayerNext, 2, std::move(object));

  ASSERT_TRUE(stage.objects[kLayerNext].Exists(2));
  EXPECT_TRUE(stage.objects[kLayerNext][2].HasDrawer());
  EXPECT_EQ(stage.objects[kLayerNext][2].Param().position_x, 42);

  stage.RemoveObject(kLayerNext, 2);
  EXPECT_FALSE(stage.objects[kLayerNext].Exists(2));
}

TEST_F(StageTest, FreeAndInitializeSingleObjectAffectFrontAndBackOnly) {
  Stage stage(1);
  SetDummyData(stage.objects[kLayerFg][0]);
  SetDummyData(stage.objects[kLayerBg][0]);
  SetDummyData(stage.objects[kLayerNext][0]);
  stage.objects[kLayerFg][0].Param().SetX(10);
  stage.objects[kLayerBg][0].Param().SetX(20);
  stage.objects[kLayerNext][0].Param().SetX(30);

  stage.FreeObjectData(0);

  EXPECT_FALSE(stage.objects[kLayerFg][0].HasDrawer());
  EXPECT_FALSE(stage.objects[kLayerBg][0].HasDrawer());
  EXPECT_TRUE(stage.objects[kLayerNext][0].HasDrawer());

  stage.InitializeObjectParams(0);

  EXPECT_EQ(stage.objects[kLayerFg][0].Param().position_x, 0);
  EXPECT_EQ(stage.objects[kLayerBg][0].Param().position_x, 0);
  EXPECT_EQ(stage.objects[kLayerNext][0].Param().position_x, 30);
}

TEST_F(StageTest, ConstructorAndResetIncludeNextObjects) {
  Stage stage(3, 2);
  stage.objects[kLayerNext][1].Param().SetVisible(1);
  stage.groups[kLayerFg].Resize(1);
  stage.groups[kLayerBg].Resize(1);
  stage.groups[kLayerNext].Resize(1);
  stage.GetEffect(kLayerNext, 1).mono = 100;
  stage.SetTransitionRenderAlpha(0.25, 0.75);

  EXPECT_EQ(GetExistFlags(stage.objects[kLayerNext]), "010");
  stage.Reset();
  EXPECT_EQ(GetExistFlags(stage.objects[kLayerNext]), "000")
      << "Stage.Reset should clear next object buffer";
  EXPECT_EQ(stage.groups[kLayerFg].Size(), 1)
      << "stage Reset() should not shrink the storage container";
  EXPECT_EQ(stage.groups[kLayerBg].Size(), 1);
  EXPECT_EQ(stage.groups[kLayerNext].Size(), 1);
  EXPECT_EQ(stage.effects[kLayerNext].Size(), 2);
  EXPECT_EQ(GetExistFlags(stage.effects[kLayerNext]), "00");
  EXPECT_DOUBLE_EQ(stage.foreground_render_alpha(), 1.0);
  EXPECT_DOUBLE_EQ(stage.next_render_alpha(), 0.0);
}

TEST_F(StageTest, TransitionRenderAlphaIsClampedAndClearable) {
  Stage stage(1);

  EXPECT_DOUBLE_EQ(stage.foreground_render_alpha(), 1.0);
  EXPECT_DOUBLE_EQ(stage.next_render_alpha(), 0.0);

  stage.SetTransitionRenderAlpha(-0.5, 1.5);

  EXPECT_DOUBLE_EQ(stage.foreground_render_alpha(), 0.0);
  EXPECT_DOUBLE_EQ(stage.next_render_alpha(), 1.0);

  stage.ClearTransitionRenderState();

  EXPECT_DOUBLE_EQ(stage.foreground_render_alpha(), 1.0);
  EXPECT_DOUBLE_EQ(stage.next_render_alpha(), 0.0);
}

TEST_F(StageTest, WipePromotesBackToFrontAndClearsBack) {
  Stage stage(3);
  SetDummyData(stage.objects[kLayerBg][0]);
  stage.objects[kLayerBg][0].Param().SetVisible(1);
  stage.objects[kLayerBg][0].Param().SetX(42);
  EXPECT_EQ(GetExistFlags(stage.objects[kLayerBg]), "100");

  stage.Wipe();

  EXPECT_EQ(GetExistFlags(stage.objects[kLayerBg]), "000");
  ASSERT_EQ(GetExistFlags(stage.objects[kLayerFg]), "100");
  EXPECT_TRUE(stage.objects[kLayerFg][0].HasDrawer());
  EXPECT_TRUE(stage.objects[kLayerFg][0].Param().visible());
  EXPECT_EQ(stage.objects[kLayerFg][0].Param().position_x, 42);
}

TEST_F(StageTest, WipePromotesBackObjectWithOnlyChildren) {
  Stage stage(3);
  stage.objects[kLayerBg][0].ResetChildren(1);
  stage.objects[kLayerBg][0].Param().SetVisible(1);
  stage.objects[kLayerBg][0].Param().SetX(42);
  GraphicsObject& child = stage.objects[kLayerBg][0].TouchChild(0);
  SetDummyData(child);
  child.Param().SetX(7);

  stage.Wipe();

  EXPECT_EQ(GetExistFlags(stage.objects[kLayerBg]), "000");
  ASSERT_EQ(GetExistFlags(stage.objects[kLayerFg]), "100");
  EXPECT_FALSE(stage.objects[kLayerFg][0].HasDrawer());
  ASSERT_TRUE(stage.objects[kLayerFg][0].HasChildren());
  ASSERT_NE(stage.objects[kLayerFg][0].GetChild(0), nullptr);
  EXPECT_TRUE(stage.objects[kLayerFg][0].GetChild(0)->HasDrawer());
  EXPECT_EQ(stage.objects[kLayerFg][0].Param().position_x, 42);
  EXPECT_EQ(stage.objects[kLayerFg][0].GetChild(0)->Param().position_x, 7);
}

TEST_F(StageTest, WipeDoesNotAllocateEmptySlots) {
  Stage stage(3);

  stage.Wipe();

  EXPECT_EQ(GetExistFlags(stage.objects[kLayerBg]), "000");
  EXPECT_EQ(GetExistFlags(stage.objects[kLayerFg]), "000");
  EXPECT_EQ(GetExistFlags(stage.objects[kLayerNext]), "000");
}

TEST_F(StageTest, WipeOnlyAllocatesTouchedSlots) {
  Stage stage(3);
  SetDummyData(stage.objects[kLayerBg][1]);
  stage.objects[kLayerBg][1].Param().SetX(42);

  stage.Wipe();

  EXPECT_EQ(GetExistFlags(stage.objects[kLayerFg]), "010");
  EXPECT_EQ(GetExistFlags(stage.objects[kLayerBg]), "000");
  EXPECT_EQ(GetExistFlags(stage.objects[kLayerNext]), "000");
  EXPECT_EQ(stage.objects[kLayerFg][1].Param().position_x, 42);
}

TEST_F(StageTest, WipePreservesOldFrontInNext) {
  Stage stage(3);
  SetDummyData(stage.objects[kLayerFg][0]);
  stage.objects[kLayerFg][0].Param().SetX(10);
  SetDummyData(stage.objects[kLayerBg][0]);
  stage.objects[kLayerBg][0].Param().SetX(20);

  stage.Wipe();

  ASSERT_EQ(GetExistFlags(stage.objects[kLayerNext]), "100");
  EXPECT_TRUE(stage.objects[kLayerNext][0].HasDrawer());
  EXPECT_EQ(stage.objects[kLayerNext][0].Param().position_x, 10);
  EXPECT_EQ(stage.objects[kLayerFg][0].Param().position_x, 20);
}

TEST_F(StageTest, WipeCopyPreservesFrontWhenBackIsEmpty) {
  Stage stage(3);
  SetDummyData(stage.objects[kLayerFg][0]);
  stage.objects[kLayerFg][0].Param().SetX(10);
  stage.objects[kLayerFg][0].Param().SetWipeCopy(1);

  stage.Wipe();

  ASSERT_EQ(GetExistFlags(stage.objects[kLayerFg]), "100");
  ASSERT_EQ(GetExistFlags(stage.objects[kLayerNext]), "100");
  EXPECT_TRUE(stage.objects[kLayerFg][0].HasDrawer());
  EXPECT_EQ(stage.objects[kLayerFg][0].Param().position_x, 10);
  EXPECT_EQ(stage.objects[kLayerNext][0].Param().position_x, 10);
}

TEST_F(StageTest, WipeRangeUsesLexicographicSorterComparison) {
  Stage stage(5);

  const int orders[] = {1, 1, 2, 2, 2};
  const int layers[] = {8, 9, 0, 1, 2};
  for (size_t i = 0; i < 5; ++i) {
    SetDummyData(stage.objects[kLayerFg][i]);
    stage.objects[kLayerFg][i].Param().z_order = orders[i];
    stage.objects[kLayerFg][i].Param().z_layer = layers[i];
    stage.objects[kLayerFg][i].Param().SetX(static_cast<int>(i + 10));
  }

  stage.Wipe(1, 2, 9, 1);

  ASSERT_EQ(GetExistFlags(stage.objects[kLayerNext]), "01110");
  EXPECT_TRUE(stage.objects[kLayerFg][0].HasDrawer());
  EXPECT_FALSE(stage.objects[kLayerFg][1].HasDrawer());
  EXPECT_FALSE(stage.objects[kLayerFg][2].HasDrawer());
  EXPECT_FALSE(stage.objects[kLayerFg][3].HasDrawer());
  EXPECT_TRUE(stage.objects[kLayerFg][4].HasDrawer());

  EXPECT_EQ(stage.objects[kLayerNext][1].Param().position_x, 11);
  EXPECT_EQ(stage.objects[kLayerNext][2].Param().position_x, 12);
  EXPECT_EQ(stage.objects[kLayerNext][3].Param().position_x, 13);
}

TEST_F(StageTest, WipeRangeIgnoresEmptyAllocatedBackSlots) {
  Stage stage(1);
  SetDummyData(stage.objects[kLayerFg][0]);
  stage.objects[kLayerFg][0].Param().z_order = 5;
  stage.objects[kLayerFg][0].Param().SetX(10);
  stage.objects[kLayerBg][0].Param().SetX(99);

  stage.Wipe(0, 0, std::numeric_limits<int>::min(),
             std::numeric_limits<int>::max());

  EXPECT_FALSE(stage.objects[kLayerNext].Exists(0));
  ASSERT_TRUE(stage.objects[kLayerFg].Exists(0));
  EXPECT_TRUE(stage.objects[kLayerFg][0].HasDrawer());
  EXPECT_EQ(stage.objects[kLayerFg][0].Param().position_x, 10);
  ASSERT_TRUE(stage.objects[kLayerBg].Exists(0));
  EXPECT_FALSE(stage.objects[kLayerBg][0].HasDrawer());
  EXPECT_EQ(stage.objects[kLayerBg][0].Param().position_x, 99);
}

TEST_F(StageTest, BackWipeEraseClearsFrontAndConsumesBack) {
  Stage stage(3);
  SetDummyData(stage.objects[kLayerFg][0]);
  stage.objects[kLayerFg][0].Param().SetX(10);
  stage.objects[kLayerBg][0].Param().SetWipeErase(1);

  stage.Wipe();

  ASSERT_EQ(GetExistFlags(stage.objects[kLayerFg]), "100");
  ASSERT_EQ(GetExistFlags(stage.objects[kLayerNext]), "100");
  ASSERT_EQ(GetExistFlags(stage.objects[kLayerBg]), "000");

  EXPECT_FALSE(stage.objects[kLayerFg][0].HasDrawer());
  EXPECT_EQ(stage.objects[kLayerFg][0].Param().wipe_erase, 1);
  EXPECT_EQ(stage.objects[kLayerNext][0].Param().position_x, 10);
}

TEST_F(StageTest, WipePromotesGroupsInRangeAndClearsTransientState) {
  Stage stage(1);
  stage.groups[kLayerFg].Resize(3);
  stage.groups[kLayerBg].Resize(3);
  stage.groups[kLayerNext].Resize(4);

  Group& before = stage.groups[kLayerFg][1];
  before.order = 1;
  before.layer = 9;
  before.cancel_priority = 11;
  before.status = Group::Status::Active;
  before.result = Group::Result::Decided;
  before.result_button_no = 101;
  before.hit_button_no = 1;
  before.pushed_button_no = 1;
  before.pressed_button_no = 1;

  Group& replacement = stage.groups[kLayerBg][1];
  replacement.order = 7;
  replacement.layer = 8;
  replacement.cancel_priority = 22;
  replacement.status = Group::Status::Waiting;
  replacement.result = Group::Result::Canceled;
  replacement.result_button_no = -1;
  replacement.hit_button_no = 2;
  replacement.pressed_button_no = 2;

  stage.groups[kLayerFg][0].order = 1;
  stage.groups[kLayerFg][0].layer = 8;
  stage.groups[kLayerFg][2].order = 2;
  stage.groups[kLayerFg][2].layer = 2;
  stage.groups[kLayerBg][0].cancel_priority = 30;
  stage.groups[kLayerBg][2].cancel_priority = 40;

  stage.Wipe(1, 2, 9, 1);

  ASSERT_EQ(stage.groups[kLayerNext].Size(), 3);
  const Group& saved = stage.groups[kLayerNext][1];
  EXPECT_EQ(saved.order, 1);
  EXPECT_EQ(saved.layer, 9);
  EXPECT_EQ(saved.cancel_priority, 11);
  EXPECT_EQ(saved.status, Group::Status::Active);
  EXPECT_EQ(saved.result, Group::Result::Decided);
  EXPECT_EQ(saved.result_button_no, 101);
  EXPECT_FALSE(saved.hit_button_no);
  EXPECT_FALSE(saved.pushed_button_no);
  EXPECT_FALSE(saved.pressed_button_no);

  const Group& promoted = stage.groups[kLayerFg][1];
  EXPECT_EQ(promoted.order, 7);
  EXPECT_EQ(promoted.layer, 8);
  EXPECT_EQ(promoted.cancel_priority, 22);
  EXPECT_EQ(promoted.status, Group::Status::Waiting);
  EXPECT_EQ(promoted.result, Group::Result::Canceled);
  EXPECT_EQ(promoted.result_button_no, -1);
  EXPECT_FALSE(promoted.hit_button_no);
  EXPECT_FALSE(promoted.pressed_button_no);

  EXPECT_FALSE(stage.groups[kLayerBg].Exists(1));

  EXPECT_EQ(stage.groups[kLayerFg][0].order, 1);
  EXPECT_EQ(stage.groups[kLayerBg][0].cancel_priority, 30);
  EXPECT_EQ(stage.groups[kLayerFg][2].layer, 2);
  EXPECT_EQ(stage.groups[kLayerBg][2].cancel_priority, 40);
}

TEST_F(StageTest, FullWipePromotesSparseBackgroundGroups) {
  Stage stage(1);
  stage.groups[kLayerBg].Resize(4);
  Group& background = stage.groups[kLayerBg][3];
  background.order = 5;
  background.layer = 6;
  background.cancel_priority = 42;
  background.status = Group::Status::Active;
  background.result = Group::Result::Decided;
  background.result_button_no = 8;

  stage.Wipe();

  ASSERT_EQ(stage.groups[kLayerFg].Size(), 4);
  ASSERT_EQ(stage.groups[kLayerBg].Size(), 4);
  ASSERT_EQ(stage.groups[kLayerNext].Size(), 4);
  EXPECT_EQ(stage.groups[kLayerFg][3].order, 5);
  EXPECT_EQ(stage.groups[kLayerFg][3].layer, 6);
  EXPECT_EQ(stage.groups[kLayerFg][3].cancel_priority, 42);
  EXPECT_EQ(stage.groups[kLayerFg][3].status, Group::Status::Active);
  EXPECT_EQ(stage.groups[kLayerFg][3].result_button_no, 8);
  EXPECT_FALSE(stage.groups[kLayerBg].Exists(3));
  EXPECT_FALSE(stage.groups[kLayerNext].Exists(3));
}

TEST_F(StageTest, WipeDoesNotAllocateDefaultGroupsOrEffects) {
  Stage stage(1, 3);
  stage.groups[kLayerFg].Resize(2);
  stage.groups[kLayerBg].Resize(2);
  stage.groups[kLayerNext].Resize(4);

  stage.Wipe();

  EXPECT_EQ(stage.groups[kLayerFg].Size(), 2);
  EXPECT_EQ(stage.groups[kLayerBg].Size(), 2);
  EXPECT_EQ(stage.groups[kLayerNext].Size(), 2);
  EXPECT_EQ(GetExistFlags(stage.groups[kLayerFg]), "00");
  EXPECT_EQ(GetExistFlags(stage.groups[kLayerBg]), "00");
  EXPECT_EQ(GetExistFlags(stage.groups[kLayerNext]), "00");
  EXPECT_EQ(GetExistFlags(stage.effects[kLayerFg]), "000");
  EXPECT_EQ(GetExistFlags(stage.effects[kLayerBg]), "000");
  EXPECT_EQ(GetExistFlags(stage.effects[kLayerNext]), "000");
}

TEST_F(StageTest, WipePromotesAndResetsEffects) {
  Stage stage(1, 1);
  stage.GetEffect(kLayerFg, 0).mono = 10;
  stage.GetEffect(kLayerBg, 0).mono = 200;

  stage.Wipe();

  EXPECT_EQ(stage.GetEffect(kLayerNext, 0).mono, 10);
  EXPECT_EQ(stage.GetEffect(kLayerFg, 0).mono, 200);
  EXPECT_EQ(stage.GetEffect(kLayerBg, 0).mono, 0);
}

TEST_F(StageTest, WipeCopyPreservesFrontUnlessBackErases) {
  Stage stage(1, 1);
  StageEffect& front = stage.GetEffect(kLayerFg, 0);
  StageEffect& back = stage.GetEffect(kLayerBg, 0);
  front.mono = 10;
  front.wipe_copy = 1;
  back.mono = 200;

  stage.Wipe();
  EXPECT_EQ(stage.GetEffect(kLayerFg, 0).mono, 10);
  EXPECT_EQ(stage.GetEffect(kLayerBg, 0).mono, 200);

  stage.GetEffect(kLayerBg, 0).wipe_erase = 1;
  stage.Wipe();
  EXPECT_EQ(stage.GetEffect(kLayerFg, 0).mono, 200);
  EXPECT_EQ(stage.GetEffect(kLayerBg, 0).mono, 0);
}
