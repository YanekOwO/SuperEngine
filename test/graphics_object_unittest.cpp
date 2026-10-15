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

#include "core/frame_action.hpp"
#include "core/object.hpp"
#include "core/object_internal/object_mutator.hpp"
#include "mock_clock.hpp"
#include "mock_graphics_object_data.hpp"
#include "vm/function.hpp"

#include <memory>

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

TEST_F(GraphicsObjectTest, ButtonCloneCopiesParameters) {
  serilang::Function callback(nullptr);
  obj.Param().button.config = MwndConfig::Button{};
  obj.Param().button.config->slot_id = 2;
  obj.Param().button.call = &callback;
  obj.Param().button.hit_state = ButtonState::Push;
  obj.Param().button.real_state = ButtonState::Disable;
  obj.Param().button.visible = false;

  GraphicsObject clone = obj.Clone();

  ASSERT_TRUE(clone.Param().button.config);
  EXPECT_EQ(clone.Param().button.config->slot_id, 2);
  EXPECT_EQ(clone.Param().button.call, &callback);
  EXPECT_EQ(clone.Param().button, obj.Param().button);
  EXPECT_EQ(clone.Param().button.hit_state, ButtonState::Push);
  EXPECT_EQ(clone.Param().button.real_state, ButtonState::Disable);
  EXPECT_FALSE(clone.Param().button.visible);
  clone.Param().button.config->slot_id = 3;
  EXPECT_EQ(obj.Param().button.config->slot_id, 2);
  EXPECT_EQ(obj.Param().button.hit_state, ButtonState::Push);
}

TEST_F(GraphicsObjectTest, ButtonMovesTransferParametersAndResetSource) {
  serilang::Function callback(nullptr);
  obj.Param().button.config = MwndConfig::Button{};
  obj.Param().button.config->slot_id = 2;
  obj.Param().button.call = &callback;
  obj.Param().button.hit_state = ButtonState::Push;
  obj.Param().button.real_state = ButtonState::Disable;
  obj.Param().button.visible = false;

  GraphicsObject moved = std::move(obj);

  ASSERT_TRUE(moved.Param().button.config);
  EXPECT_EQ(moved.Param().button.config->slot_id, 2);
  EXPECT_EQ(moved.Param().button.call, &callback);
  EXPECT_EQ(moved.Param().button.hit_state, ButtonState::Push);
  EXPECT_EQ(moved.Param().button.real_state, ButtonState::Disable);
  EXPECT_FALSE(moved.Param().button.visible);
  EXPECT_FALSE(obj.Param().button.config);
  EXPECT_EQ(obj.Param().button.call, nullptr);
  EXPECT_EQ(obj.Param().button.hit_state, ButtonState::Normal);
  EXPECT_EQ(obj.Param().button.real_state, ButtonState::Normal);
  EXPECT_TRUE(obj.Param().button.visible);
  EXPECT_EQ(obj.Param().button, ButtonProperties{});

  GraphicsObject assigned;
  assigned = std::move(moved);

  ASSERT_TRUE(assigned.Param().button.config);
  EXPECT_EQ(assigned.Param().button.config->slot_id, 2);
  EXPECT_EQ(assigned.Param().button.call, &callback);
  EXPECT_EQ(assigned.Param().button.hit_state, ButtonState::Push);
  EXPECT_EQ(assigned.Param().button.real_state, ButtonState::Disable);
  EXPECT_FALSE(assigned.Param().button.visible);
  EXPECT_FALSE(moved.Param().button.config);
  EXPECT_EQ(moved.Param().button.call, nullptr);
  EXPECT_EQ(moved.Param().button, ButtonProperties{});

  assigned = GraphicsObject();
  EXPECT_EQ(assigned.Param().button, ButtonProperties{});
}

TEST_F(GraphicsObjectTest, ButtonResetRestoresDefaultParameters) {
  serilang::Function callback(nullptr);
  obj.Param().button.config = MwndConfig::Button{};
  obj.Param().button.call = &callback;
  obj.Param().button.hit_state = ButtonState::Push;
  obj.Param().button.real_state = ButtonState::Disable;
  obj.Param().button.visible = false;

  obj.InitializeParams();

  EXPECT_FALSE(obj.Param().button.config);
  EXPECT_EQ(obj.Param().button.call, nullptr);
  EXPECT_EQ(obj.Param().button.hit_state, ButtonState::Normal);
  EXPECT_EQ(obj.Param().button.real_state, ButtonState::Normal);
  EXPECT_TRUE(obj.Param().button.visible);
  EXPECT_EQ(obj.Param().button, ButtonProperties{});

  obj.Param().button.config = MwndConfig::Button{};
  obj.Param().button.call = &callback;
  obj.Reset();
  EXPECT_FALSE(obj.Param().button.config);
  EXPECT_EQ(obj.Param().button.call, nullptr);
  EXPECT_EQ(obj.Param().button, ButtonProperties{});
}

TEST_F(GraphicsObjectTest, FrameActionsMoveAndCloneWithIndependentCounters) {
  EXPECT_EQ(obj.frame_action_, nullptr);
  EXPECT_EQ(obj.frame_action_list_.size(), 0u);
  serilang::Function callback(nullptr);
  auto action = obj.GetFrameAction();
  action->function = &callback;
  action->arguments = {12, std::string("original")};
  action->end_time = 100;
  action->end_action = true;
  action->GetCounter()->StartGame();
  action->counter->Advance(37ms, 37ms);
  obj.frame_action_list_.resize(2);
  auto channel = obj.GetFrameAction(0);
  channel->function = &callback;
  channel->end_action = true;
  channel->GetCounter()->Set(7);
  std::weak_ptr<Counter> counter = action->counter;
  std::weak_ptr<Counter> channel_counter = channel->counter;

  GraphicsObject clone = obj.Clone();
  auto copied = clone.GetFrameAction();
  EXPECT_NE(copied, action);
  EXPECT_NE(copied->counter, action->counter);
  EXPECT_EQ(copied->function, &callback);
  EXPECT_EQ(copied->arguments, action->arguments);
  EXPECT_EQ(copied->end_time, 100);
  EXPECT_FALSE(copied->end_action);
  ASSERT_EQ(clone.frame_action_list_.size(), 2u);
  EXPECT_NE(clone.frame_action_list_[0]->counter, channel->counter);
  EXPECT_EQ(clone.frame_action_list_[0]->function, &callback);
  EXPECT_EQ(clone.frame_action_list_[0]->counter->Get(), 7);
  EXPECT_FALSE(clone.frame_action_list_[0]->end_action);
  EXPECT_EQ(clone.frame_action_list_[1], nullptr);

  copied->counter->Advance(3ms, 3ms);
  copied->arguments[1] = std::string("copy");
  EXPECT_EQ(copied->counter->Get(), 40);
  EXPECT_EQ(action->counter->Get(), 37);
  EXPECT_EQ(std::get<std::string>(action->arguments[1]), "original");

  GraphicsObject moved = std::move(obj);
  EXPECT_EQ(moved.frame_action_, action);
  EXPECT_EQ(moved.frame_action_list_[0], channel);
  EXPECT_EQ(moved.GetFrameAction()->function, &callback);
  EXPECT_EQ(moved.GetFrameAction()->counter, counter.lock());
  EXPECT_EQ(moved.GetFrameAction()->counter->Get(), 37);
  EXPECT_TRUE(moved.GetFrameAction()->end_action);
  ASSERT_EQ(moved.frame_action_list_.size(), 2u);
  EXPECT_EQ(moved.frame_action_list_[0]->counter, channel_counter.lock());
  EXPECT_FALSE(obj.GetFrameAction()->IsActive());
  EXPECT_EQ(obj.GetFrameAction()->counter, nullptr);
  EXPECT_EQ(obj.frame_action_list_.size(), 0u);
  EXPECT_EQ(obj.GetFrameAction()->GetCounter()->Get(), 0);
  obj.GetFrameAction()->counter->Set(9);
  GraphicsObject assigned;
  assigned = std::move(moved);
  EXPECT_EQ(assigned.frame_action_, action);
  EXPECT_EQ(assigned.frame_action_list_[0], channel);
  EXPECT_EQ(assigned.GetFrameAction()->function, &callback);
  EXPECT_EQ(assigned.GetFrameAction()->counter, counter.lock());
  EXPECT_EQ(assigned.GetFrameAction()->end_time, 100);
  ASSERT_EQ(assigned.frame_action_list_.size(), 2u);
  EXPECT_EQ(assigned.frame_action_list_[0]->counter, channel_counter.lock());
  EXPECT_FALSE(moved.GetFrameAction()->IsActive());
  EXPECT_EQ(moved.GetFrameAction()->counter, nullptr);
  EXPECT_EQ(moved.frame_action_list_.size(), 0u);
  EXPECT_EQ(moved.GetFrameAction()->GetCounter()->Get(), 0);
  EXPECT_EQ(assigned.GetFrameAction()->counter->Get(), 37);
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

TEST_F(GraphicsObjectTest,
       ChildSharedHandlesRetainTargetsAcrossMovesAndReplacement) {
  obj.ResetChildren(1);
  GraphicsObject& child = obj.TouchChild(0);
  child.Param().SetX(42);
  std::shared_ptr<GraphicsObject> handle = obj.GetChildren()[0];

  GraphicsObject moved = std::move(obj);
  ASSERT_TRUE(handle);
  EXPECT_EQ(handle->Param().position_x, 42);
  EXPECT_EQ(handle.get(), moved.GetChild(0));

  moved.SetChild(0, GraphicsObject());
  EXPECT_EQ(handle->Param().position_x, 42);
  EXPECT_NE(handle.get(), moved.GetChild(0));
}

TEST_F(GraphicsObjectTest, ClonedChildrenHaveIndependentSharedHandles) {
  obj.ResetChildren(1);
  obj.TouchChild(0).Param().SetX(42);
  std::shared_ptr<GraphicsObject> original = obj.GetChildren()[0];

  GraphicsObject clone = obj.Clone();
  std::shared_ptr<GraphicsObject> copied = clone.GetChildren()[0];

  ASSERT_TRUE(original);
  ASSERT_TRUE(copied);
  EXPECT_NE(original.get(), copied.get());

  clone.ResetChildren(0);
  EXPECT_TRUE(original);
  EXPECT_EQ(copied->Param().position_x, 42);
  EXPECT_TRUE(clone.GetChildren().empty());
}

TEST_F(GraphicsObjectTest, SetObjectDataShouldNotClearChildren) {
  obj.ResetChildren(1);
  obj.TouchChild(0).Param().SetX(42);

  SetDummyData(obj);

  EXPECT_TRUE(obj.HasDrawer());
  EXPECT_TRUE(obj.HasChildren())
      << "Setting object drawer should not reset children objects";
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

  obj.Reset();

  EXPECT_FALSE(obj.HasChildren());
  EXPECT_EQ(obj.Param().position_x, 0);
  EXPECT_TRUE(obj.FilePath().empty());
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
