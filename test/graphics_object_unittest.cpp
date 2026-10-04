// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
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

#include "core/object.hpp"
#include "core/object_internal/object_mutator.hpp"
#include "mock_clock.hpp"
#include "mock_graphics_object_data.hpp"

#include <chrono>
#include <memory>
#include <optional>

using std::chrono_literals::operator""ms;

namespace {

ObjectParameterMutator MakeMutator(
    std::function<void(ObjectParameter&, int)> setter,
    std::shared_ptr<Clock> clock,
    int start,
    int end,
    int duration_ms) {
  return ObjectParameterMutator(
      std::move(setter),
      FrameCounter(std::move(clock),
                   Interpolation(Range(start, end), InterpolationType::OneShot,
                                 InterpolationMode::Linear),
                   duration_ms));
}

}  // namespace

class GraphicsObjectTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto& param = obj.Param();

    param.is_visible = true;
    param.position_x = 10;
    param.position_y = 20;
  }

  void SetDummyData(GraphicsObject& object) {
    object.SetDrawer(std::make_unique<MockGraphicsObjectData>());
  }

  GraphicsObject obj;
};

TEST_F(GraphicsObjectTest, MoveSemantics) {
  GraphicsObject other = std::move(obj);

  EXPECT_TRUE(other.Param().is_visible);
  EXPECT_EQ(other.Param().position_x, 10);
  EXPECT_EQ(other.Param().position_y, 20);

  EXPECT_FALSE(obj.Param().is_visible);
}

TEST_F(GraphicsObjectTest, Clone) {
  GraphicsObject other = obj.Clone();

  other.Param().position_x = 100;
  EXPECT_TRUE(other.Param().is_visible);
  EXPECT_EQ(other.Param().position_x, 100);
  EXPECT_EQ(other.Param().position_y, 20);
  EXPECT_EQ(obj.Param().position_x, 10);
}

TEST_F(GraphicsObjectTest, CloneCopiesFilePath) {
  obj.SetFilePath("bg47");

  GraphicsObject other = obj.Clone();

  EXPECT_EQ(other.FilePath(), "bg47");
  other.SetFilePath("cg01");
  EXPECT_EQ(obj.FilePath(), "bg47");
}

TEST_F(GraphicsObjectTest, MoveTransfersFilePath) {
  obj.SetFilePath("bg47");

  GraphicsObject other = std::move(obj);

  EXPECT_EQ(other.FilePath(), "bg47");
  EXPECT_TRUE(obj.FilePath().empty());

  GraphicsObject assigned;
  assigned = std::move(other);
  EXPECT_EQ(assigned.FilePath(), "bg47");
  EXPECT_TRUE(other.FilePath().empty());
}

TEST_F(GraphicsObjectTest, CloneCopiesChildrenDeeply) {
  obj.ResetChildren(2);
  GraphicsObject& child = obj.TouchChild(1);
  child.Param().SetX(42);
  SetDummyData(child);

  GraphicsObject other = obj.Clone();

  ASSERT_TRUE(other.HasChildren());
  ASSERT_NE(other.GetChild(1), nullptr);
  ASSERT_NE(obj.GetChild(1), other.GetChild(1));
  EXPECT_EQ(other.GetChild(1)->Param().position_x, 42);
  EXPECT_TRUE(other.GetChild(1)->HasDrawer());

  other.GetChild(1)->Param().SetX(100);
  EXPECT_EQ(obj.GetChild(1)->Param().position_x, 42);
}

TEST_F(GraphicsObjectTest, SetObjectDataClearsChildren) {
  obj.ResetChildren(1);
  obj.TouchChild(0).Param().SetX(42);

  SetDummyData(obj);

  EXPECT_TRUE(obj.HasDrawer());
  EXPECT_FALSE(obj.HasChildren());
}

TEST_F(GraphicsObjectTest, ResetChildrenClearsFilePath) {
  obj.SetFilePath("bg47");

  obj.ResetChildren(1);

  EXPECT_TRUE(obj.FilePath().empty());
}

TEST_F(GraphicsObjectTest, InitializeParamsPreservesChildren) {
  obj.ResetChildren(1);
  obj.TouchChild(0).Param().SetX(42);
  obj.SetFilePath("bg47");

  obj.InitializeParams();

  ASSERT_TRUE(obj.HasChildren());
  ASSERT_NE(obj.GetChild(0), nullptr);
  EXPECT_EQ(obj.GetChild(0)->Param().position_x, 42);
  EXPECT_EQ(obj.FilePath(), "bg47");
}

TEST_F(GraphicsObjectTest, FreeObjectDataClearsChildren) {
  obj.ResetChildren(1);
  obj.TouchChild(0);
  obj.SetFilePath("bg47");

  obj.FreeObjectData();

  EXPECT_FALSE(obj.HasDrawer());
  EXPECT_FALSE(obj.HasChildren());
  EXPECT_TRUE(obj.FilePath().empty());
}

TEST_F(GraphicsObjectTest, FreeDataAndInitializeParamsClearsChildren) {
  obj.ResetChildren(1);
  obj.TouchChild(0);
  obj.Param().SetX(42);
  obj.SetFilePath("bg47");

  obj.FreeDataAndInitializeParams();

  EXPECT_FALSE(obj.HasChildren());
  EXPECT_EQ(obj.Param().position_x, 0);
  EXPECT_TRUE(obj.FilePath().empty());
}

TEST_F(GraphicsObjectTest, InvisibleParentDoesNotRenderChildren) {
  obj.ResetChildren(1);
  obj.Param().SetVisible(false);
  GraphicsObject& child = obj.TouchChild(0);
  child.Param().SetVisible(true);
  int render_count = 0;
  auto drawer = std::make_unique<MockGraphicsObjectData>();
  drawer->SetRenderCallback(
      [&render_count](const GraphicsObject&, std::optional<ParentObjState>) {
        ++render_count;
      });
  child.SetDrawer(std::move(drawer));

  obj.Render();

  EXPECT_EQ(render_count, 0);
}

TEST_F(GraphicsObjectTest, ParentWithoutObjectDataPassesStateToChildren) {
  obj.ResetChildren(1);
  obj.Param().SetVisible(true);
  obj.Param().SetX(10);
  obj.Param().SetY(20);
  obj.Param().SetAlpha(128);
  GraphicsObject& child = obj.TouchChild(0);
  child.Param().SetVisible(true);

  int render_count = 0;
  std::optional<ParentObjState> last_parent;
  auto drawer = std::make_unique<MockGraphicsObjectData>();
  drawer->SetRenderCallback(
      [&render_count, &last_parent](const GraphicsObject&,
                                    std::optional<ParentObjState> parent) {
        ++render_count;
        last_parent = std::move(parent);
      });
  child.SetDrawer(std::move(drawer));

  obj.Render();

  EXPECT_EQ(render_count, 1);
  ASSERT_TRUE(last_parent.has_value());
  EXPECT_FLOAT_EQ(last_parent->render_state.pos_x, 10.0f);
  EXPECT_FLOAT_EQ(last_parent->render_state.pos_y, 20.0f);
  EXPECT_FLOAT_EQ(last_parent->alpha, obj.Param().GetNormalizedAlpha());
}

TEST_F(GraphicsObjectTest, ParentButtonEffectsPassToChildren) {
  obj.ResetChildren(1);
  obj.Param().SetVisible(true);
  obj.Param().SetAlpha(200);
  obj.Param().SetBright(10);
  obj.Param().SetDark(20);
  obj.Param().SetButtonOverrides(0, 0, 0, 128, 30, 40);
  GraphicsObject& child = obj.TouchChild(0);
  child.Param().SetVisible(true);

  std::optional<ParentObjState> last_parent;
  auto drawer = std::make_unique<MockGraphicsObjectData>();
  drawer->SetRenderCallback(
      [&last_parent](const GraphicsObject&,
                     std::optional<ParentObjState> parent) {
        last_parent = std::move(parent);
      });
  child.SetDrawer(std::move(drawer));

  obj.Render();

  ASSERT_TRUE(last_parent.has_value());
  EXPECT_FLOAT_EQ(last_parent->alpha, (200 * 128 / 255) / 255.0f);
  EXPECT_FLOAT_EQ(last_parent->bright, 40.0f / 255.0f);
  EXPECT_FLOAT_EQ(last_parent->dark, 60.0f / 255.0f);
}

TEST_F(GraphicsObjectTest, ParentMaskPassesToUnmaskedChildren) {
  obj.ResetChildren(1);
  obj.Param().SetMaskNo(3);
  GraphicsObject& child = obj.TouchChild(0);
  child.Param().SetVisible(true);

  std::optional<ObjectMask> rendered_mask;
  auto drawer = std::make_unique<MockGraphicsObjectData>();
  drawer->SetMaskRenderCallback(
      [&rendered_mask](const GraphicsObject&, std::optional<ParentObjState>,
                       std::optional<ObjectMask> mask) {
        rendered_mask = std::move(mask);
      });
  child.SetDrawer(std::move(drawer));

  auto surface = std::make_shared<SDLSurface>();
  ObjectMaskResolver resolver =
      [surface](int index) -> std::optional<ObjectMask> {
    if (index != 3)
      return std::nullopt;
    return ObjectMask{.surface = surface, .origin = Point(12, 34)};
  };
  obj.Render(std::nullopt, &resolver);

  ASSERT_TRUE(rendered_mask.has_value());
  EXPECT_EQ(rendered_mask->surface, surface);
  EXPECT_EQ(rendered_mask->origin, Point(12, 34));
}

TEST_F(GraphicsObjectTest, ExplicitInvalidChildMaskOverridesParentMask) {
  obj.ResetChildren(1);
  obj.Param().SetMaskNo(3);
  GraphicsObject& child = obj.TouchChild(0);
  child.Param().SetVisible(true);
  child.Param().SetMaskNo(4);

  std::optional<ObjectMask> rendered_mask;
  auto drawer = std::make_unique<MockGraphicsObjectData>();
  drawer->SetMaskRenderCallback(
      [&rendered_mask](const GraphicsObject&, std::optional<ParentObjState>,
                       std::optional<ObjectMask> mask) {
        rendered_mask = std::move(mask);
      });
  child.SetDrawer(std::move(drawer));

  auto surface = std::make_shared<SDLSurface>();
  ObjectMaskResolver resolver =
      [surface](int index) -> std::optional<ObjectMask> {
    if (index != 3)
      return std::nullopt;
    return ObjectMask{.surface = surface, .origin = Point(12, 34)};
  };
  obj.Render(std::nullopt, &resolver);

  EXPECT_FALSE(rendered_mask.has_value());
  EXPECT_EQ(child.Param().mask_no, 4);
}

TEST_F(GraphicsObjectTest, EndObjectMutatorMatching) {
  auto clock = std::make_shared<MockClock>();
  obj.AddObjectMutator(ObjectMutator(
      MakeMutator([](ObjectParameter& pm, int value) { pm.SetX(value); }, clock,
                  10, 100, 1000),
      -1, "fade"));
  obj.AddObjectMutator(ObjectMutator(
      MakeMutator([](ObjectParameter& pm, int value) { pm.SetY(value); }, clock,
                  20, 200, 1000),
      -1, "fade"));

  EXPECT_EQ(obj.CountMutators(), 2);
  EXPECT_TRUE(obj.IsMutatorRunningMatching(-1, "fade"));

  obj.EndObjectMutatorMatching(-1, "fade", 0);

  EXPECT_FALSE(obj.IsMutatorRunningMatching(-1, "fade"));
  EXPECT_EQ(obj.CountMutators(), 0);
  EXPECT_EQ(obj.Param().x(), 100);
  EXPECT_EQ(obj.Param().y(), 200);
}

TEST_F(GraphicsObjectTest, MatchingMutatorsRunIndependently) {
  auto clock = std::make_shared<MockClock>();
  obj.AddObjectMutator(ObjectMutator(
      MakeMutator([](ObjectParameter& pm, int value) { pm.SetX(value); }, clock,
                  10, 100, 100),
      -1, "move"));

  int completion_count = 0;
  ObjectMutator y_mutator(
      MakeMutator([](ObjectParameter& pm, int value) { pm.SetY(value); }, clock,
                  20, 220, 200),
      -1, "move");
  y_mutator.OnComplete(
      [&completion_count](ObjectParameter&) { ++completion_count; });
  obj.AddObjectMutator(std::move(y_mutator));

  EXPECT_EQ(obj.CountMutators(), 2);

  clock->AdvanceTime(100ms);
  obj.ExecuteMutators();
  EXPECT_EQ(obj.Param().x(), 100);
  EXPECT_EQ(obj.Param().y(), 120);
  EXPECT_EQ(obj.CountMutators(), 1);
  EXPECT_TRUE(obj.IsMutatorRunningMatching(-1, "move"));
  EXPECT_EQ(completion_count, 0);

  clock->AdvanceTime(100ms);
  obj.ExecuteMutators();
  EXPECT_EQ(obj.Param().y(), 220);
  EXPECT_EQ(obj.CountMutators(), 0);
  EXPECT_FALSE(obj.IsMutatorRunningMatching(-1, "move"));
  EXPECT_EQ(completion_count, 1);

  obj.ExecuteMutators();
  EXPECT_EQ(completion_count, 1);
}
