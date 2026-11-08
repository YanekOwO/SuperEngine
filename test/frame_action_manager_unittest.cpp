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

#include "core/game_config.hpp"
#include "core/stage.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std::chrono_literals;

struct ActionCall {
  ActionReference reference;
  ScriptCall request;
  bool active;
  bool end_action;
  int time;
};

struct FakeExec {
  std::vector<ActionCall> calls;
  FrameActionManager::Exec on_exec;

  void operator()(const ActionReference& reference, ScriptCall request) {
    const auto& action = *reference.action;
    calls.push_back({reference, request, action.IsActive(), action.end_action,
                     action.counter->Get()});
    if (on_exec)
      on_exec(reference, request);
  }
};

static void ExpectReset(const FrameAction& action) {
  EXPECT_FALSE(action.IsActive());
  EXPECT_EQ(action.call, ScriptCall{});
  EXPECT_EQ(action.end_time, 0);
  EXPECT_FALSE(action.end_action);
  ASSERT_NE(action.counter, nullptr);
  EXPECT_EQ(action.counter->Get(), 0);
  EXPECT_FALSE(action.counter->IsActive());
}

class FrameActionManagerTest : public ::testing::Test {
 protected:
  Stage stage{2};
  LocalConfig config;
  FakeExec exec;
  FrameActionManager manager{stage, config, 2};
  ActionReference action{manager.global_action(), {}};
  const CmdTarget target{2, 5};

  void SetUp() override {
    manager.SetExec([this](const ActionReference& reference,
                           ScriptCall selected) { exec(reference, selected); });
  }

  ActionReference Channel(int index = 0) {
    return {TouchFrameAction(manager.global_channels().at(index)), {}};
  }

  ActionReference Object(int layer = kLayerFg) {
    auto owner = stage.objects.Get(layer, 0);
    return {owner->GetFrameAction(), owner};
  }
};

TEST_F(FrameActionManagerTest, StartInitializesWithoutExecuting) {
  // Start an inactive slot with stale state; install the command and reset
  // time.
  auto counter = action.action->GetCounter();
  counter->Set(37);
  action.action->end_action = true;
  const ScriptCall request{
      .target = target, .integers = {7}, .strings = {"value"}};

  manager.Start(action, 100, false, request);

  EXPECT_EQ(std::get<CmdTarget>(action.action->call.target), target);
  EXPECT_EQ(action.action->call, request);
  EXPECT_EQ(action.action->end_time, 100);
  EXPECT_FALSE(action.action->end_action);
  EXPECT_EQ(action.action->counter, counter);
  EXPECT_TRUE(counter->IsActive());
  EXPECT_EQ(counter->Get(), 0);
  EXPECT_TRUE(exec.calls.empty());
}

TEST_F(FrameActionManagerTest, StartSelectsGameTime) {
  // Give the clocks different deltas; a game-time action uses the game delta.
  manager.Start(action, -1, false, ScriptCall{.target = target});
  manager.AdvanceAndDispatch(3ms, 20ms);
  ASSERT_EQ(exec.calls.size(), 1);
  EXPECT_EQ(exec.calls[0].time, 20);
}

TEST_F(FrameActionManagerTest, DispatchRetainsCallWhenActionIsReset) {
  // Reset the slot inside its callback; the dispatched snapshot keeps its data.
  const ScriptCall request{
      .target = target, .integers = {7}, .strings = {"saved"}};
  manager.Start(action, -1, false, request);
  exec.on_exec = [&](const ActionReference& reference, ScriptCall selected) {
    reference.action->Reset();
    EXPECT_EQ(selected, request);
  };

  manager.AdvanceAndDispatch(1ms, 1ms);

  ASSERT_EQ(exec.calls.size(), 1);
  EXPECT_EQ(exec.calls[0].request, request);
  ExpectReset(*action.action);
}

TEST_F(FrameActionManagerTest, StartSelectsRealTime) {
  // Give the clocks different deltas; a real-time action uses the real delta.
  manager.Start(action, -1, true, ScriptCall{.target = target});
  manager.AdvanceAndDispatch(3ms, 20ms);
  ASSERT_EQ(exec.calls.size(), 1);
  EXPECT_EQ(exec.calls[0].time, 3);
}

TEST_F(FrameActionManagerTest,
       RestartFinalizesPreviousActionBeforeReplacement) {
  // Replace a running command; its final call must still see its old arguments.
  manager.Start(action, 10, false,
                ScriptCall{.target = target, .integers = {7}});
  const CmdTarget replacement{3, 9};
  manager.Start(
      action, 20, true,
      ScriptCall{.target = replacement, .strings = {std::string("new")}});

  ASSERT_EQ(exec.calls.size(), 1);
  EXPECT_EQ(std::get<CmdTarget>(exec.calls[0].request.target), target);
  EXPECT_EQ(exec.calls[0].request.integers, (std::vector<int>{7}));
  EXPECT_TRUE(exec.calls[0].end_action);
  EXPECT_EQ(exec.calls[0].time, 10);
  EXPECT_EQ(std::get<CmdTarget>(action.action->call.target), replacement);
  EXPECT_EQ(action.action->call.strings, (std::vector<std::string>{"new"}));
  EXPECT_EQ(action.action->end_time, 20);
  EXPECT_EQ(action.action->counter->Get(), 0);
  EXPECT_FALSE(action.action->end_action);
}

TEST_F(FrameActionManagerTest, FailedRestartDoesNotInstallReplacement) {
  // Fail the old final call during restart; leave the slot reset, not replaced.
  manager.Start(action, 10, false, ScriptCall{.target = target});
  exec.on_exec = [](const ActionReference&, ScriptCall) {
    throw std::runtime_error("execution failed");
  };

  EXPECT_THROW(
      manager.Start(action, 20, false, ScriptCall{.target = (CmdTarget{3, 9})}),
      std::runtime_error);
  ASSERT_EQ(exec.calls.size(), 1);
  EXPECT_EQ(std::get<CmdTarget>(exec.calls[0].request.target), target);
  ExpectReset(*action.action);
}

TEST_F(FrameActionManagerTest, EndFinalizesOnceAndResetsRetainedState) {
  // End a running action twice; expose final state once and retain its counter.
  manager.Start(action, 10, false,
                ScriptCall{.target = target, .integers = {7}});
  auto counter = action.action->counter;
  counter->Set(20);
  manager.End(action);
  manager.End(action);

  ASSERT_EQ(exec.calls.size(), 1);
  const auto& call = exec.calls[0];
  EXPECT_EQ(call.reference.action, action.action);
  EXPECT_EQ(std::get<CmdTarget>(call.request.target), target);
  EXPECT_EQ(call.request.integers, (std::vector<int>{7}));
  EXPECT_FALSE(call.active);
  EXPECT_TRUE(call.end_action);
  EXPECT_EQ(call.time, 10);
  EXPECT_EQ(action.action->counter, counter);
  ExpectReset(*action.action);
}

TEST_F(FrameActionManagerTest, EndClampsIndefiniteDurationToZero) {
  // Explicitly end an indefinite action; the final counter value must be zero.
  manager.Start(action, -1, false, ScriptCall{.target = target});
  action.action->counter->Set(30);
  manager.End(action);
  ASSERT_EQ(exec.calls.size(), 1);
  EXPECT_EQ(exec.calls[0].time, 0);
}

TEST_F(FrameActionManagerTest, EndInactiveActionResetsWithoutExecuting) {
  // End a slot with only stale state; clear it without invoking exec.
  action.action->call.integers = {7};
  action.action->end_time = 12;
  action.action->end_action = true;
  action.action->GetCounter()->StartGame();
  action.action->counter->Set(25);
  manager.End(action);
  EXPECT_TRUE(exec.calls.empty());
  ExpectReset(*action.action);
}

TEST_F(FrameActionManagerTest, FinalExecutionFailureStillResetsAction) {
  // Throw from final execution; propagate the error after clearing all state.
  manager.Start(action, 10, false,
                ScriptCall{.target = target, .integers = {7}});
  exec.on_exec = [](const ActionReference&, ScriptCall) {
    throw std::runtime_error("execution failed");
  };
  EXPECT_THROW(manager.End(action), std::runtime_error);
  ExpectReset(*action.action);
}

TEST_F(FrameActionManagerTest, FinalCallbackCannotKeepReplacementInSameSlot) {
  // Start a replacement from the final callback; final cleanup clears that
  // slot.
  manager.Start(action, 10, false, ScriptCall{.target = target});
  exec.on_exec = [this](const ActionReference& reference, ScriptCall) {
    manager.Start(reference, -1, false,
                  ScriptCall{.target = CmdTarget{3, 9}, .integers = {7}});
  };
  manager.End(action);
  ASSERT_EQ(exec.calls.size(), 1);
  ExpectReset(*action.action);
}

TEST_F(FrameActionManagerTest, DispatchFailureLeavesActionActive) {
  // Throw from a regular callback; propagate the error without finalizing.
  manager.Start(action, -1, false,
                ScriptCall{.target = target, .integers = {7}});
  exec.on_exec = [](const ActionReference&, ScriptCall) {
    throw std::runtime_error("execution failed");
  };
  EXPECT_THROW(manager.AdvanceAndDispatch(3ms, 20ms), std::runtime_error);
  EXPECT_EQ(std::get<CmdTarget>(action.action->call.target), target);
  EXPECT_EQ(action.action->call.integers, (std::vector<int>{7}));
  EXPECT_EQ(action.action->counter->Get(), 20);
  EXPECT_FALSE(action.action->end_action);
}

TEST_F(FrameActionManagerTest, DispatchVisitsGlobalThenChannelsWithoutOwners) {
  // Start the global and two channels; dispatch them in order with no owner.
  const std::vector<ActionReference> references{action, Channel(0), Channel(1)};
  for (const auto& reference : references)
    manager.Start(reference, -1, false,
                  ScriptCall{.target = target, .integers = {7}});
  manager.AdvanceAndDispatch(1ms, 2ms);

  ASSERT_EQ(exec.calls.size(), references.size());
  for (std::size_t i = 0; i < references.size(); ++i) {
    EXPECT_EQ(exec.calls[i].reference.action, references[i].action);
    EXPECT_EQ(exec.calls[i].reference.owner, nullptr);
    EXPECT_EQ(std::get<CmdTarget>(exec.calls[i].request.target), target);
    EXPECT_EQ(exec.calls[i].request.integers, (std::vector<int>{7}));
    EXPECT_TRUE(exec.calls[i].active);
    EXPECT_FALSE(exec.calls[i].end_action);
  }
}

TEST_F(FrameActionManagerTest, DispatchVisitsObjectChannelsAndChildrenByLayer) {
  // Populate each layer with a parent, channel, and child; preserve their
  // owners and visit background, foreground, then next objects in traversal
  // order.
  std::vector<ActionReference> references;
  for (int layer : {kLayerBg, kLayerFg, kLayerNext}) {
    auto parent = Object(layer);
    parent.owner->frame_action_list_.resize(1);
    parent.owner->GetChildren().resize(1);
    parent.owner->TouchChild(0);
    auto child = parent.owner->GetChildren()[0];
    references.push_back(parent);
    references.push_back({parent.owner->GetFrameAction(0), parent.owner});
    references.push_back({child->GetFrameAction(), child});
  }
  for (const auto& reference : references)
    manager.Start(reference, -1, false, ScriptCall{.target = target});
  manager.AdvanceAndDispatch(1ms, 2ms);

  ASSERT_EQ(exec.calls.size(), references.size());
  for (std::size_t i = 0; i < references.size(); ++i) {
    EXPECT_EQ(exec.calls[i].reference.action, references[i].action);
    EXPECT_EQ(exec.calls[i].reference.owner, references[i].owner);
  }
}

TEST_F(FrameActionManagerTest, AdvancesAllCountersBeforeFirstCallback) {
  // Inspect later actions from the global callback; all clocks must advance
  // first.
  auto channel = Channel();
  auto object = Object();
  for (const auto& reference : {action, channel, object})
    manager.Start(reference, -1, false, ScriptCall{.target = target});
  exec.on_exec = [&](const ActionReference&, ScriptCall) {
    EXPECT_EQ(channel.action->counter->Get(), 20);
    EXPECT_EQ(object.action->counter->Get(), 20);
  };
  manager.AdvanceAndDispatch(3ms, 20ms);
  EXPECT_EQ(exec.calls.size(), 3);
}

TEST_F(FrameActionManagerTest, AdvancesCounterWithoutActiveAction) {
  // Run a counter in an inactive slot; advance time without executing a
  // command.
  auto counter = action.action->GetCounter();
  counter->StartGame();
  manager.AdvanceAndDispatch(3ms, 20ms);
  EXPECT_EQ(counter->Get(), 20);
  EXPECT_TRUE(exec.calls.empty());
}

TEST_F(FrameActionManagerTest, NegativeDeltasDoNotAdvanceActions) {
  // Advance game and real actions with negative deltas; both remain at zero.
  auto channel = Channel();
  manager.Start(action, -1, false, ScriptCall{.target = target});
  manager.Start(channel, -1, true, ScriptCall{.target = target});
  manager.AdvanceAndDispatch(-3ms, -20ms);
  ASSERT_EQ(exec.calls.size(), 2);
  EXPECT_EQ(exec.calls[0].time, 0);
  EXPECT_EQ(exec.calls[1].time, 0);
}

TEST_F(FrameActionManagerTest, TimeStopFreezesAllClocksButStillDispatches) {
  // Stop global time with both kinds of action active; execute without
  // advancing.
  auto object = Object();
  manager.Start(action, -1, false, ScriptCall{.target = target});
  manager.Start(object, -1, true, ScriptCall{.target = target});
  config.time_stop = true;
  manager.AdvanceAndDispatch(3ms, 20ms);
  ASSERT_EQ(exec.calls.size(), 2);
  EXPECT_EQ(exec.calls[0].time, 0);
  EXPECT_EQ(exec.calls[1].time, 0);
}

TEST_F(FrameActionManagerTest, FrameActionStopAffectsOnlyGlobalClocks) {
  // Toggle the live frame-action flag; object clocks continue and globals
  // resume.
  auto channel = Channel();
  auto object = Object();
  for (const auto& reference : {action, channel, object})
    manager.Start(reference, -1, false, ScriptCall{.target = target});
  config.frame_action_time_stop = true;
  manager.AdvanceAndDispatch(3ms, 20ms);
  EXPECT_EQ(action.action->counter->Get(), 0);
  EXPECT_EQ(channel.action->counter->Get(), 0);
  EXPECT_EQ(object.action->counter->Get(), 20);
  EXPECT_EQ(exec.calls.size(), 3);

  config.frame_action_time_stop = false;
  manager.AdvanceAndDispatch(3ms, 20ms);
  EXPECT_EQ(action.action->counter->Get(), 20);
  EXPECT_EQ(channel.action->counter->Get(), 20);
  EXPECT_EQ(object.action->counter->Get(), 40);
}

TEST_F(FrameActionManagerTest, StageStopAffectsOnlyObjectClocks) {
  // Replace the live configuration after stage stop; objects must resume too.
  auto object = Object();
  manager.Start(action, -1, false, ScriptCall{.target = target});
  manager.Start(object, -1, false, ScriptCall{.target = target});
  config.stage_time_stop = true;
  manager.AdvanceAndDispatch(3ms, 20ms);
  EXPECT_EQ(action.action->counter->Get(), 20);
  EXPECT_EQ(object.action->counter->Get(), 0);
  EXPECT_EQ(exec.calls.size(), 2);

  config = LocalConfig{};
  manager.AdvanceAndDispatch(3ms, 20ms);
  EXPECT_EQ(action.action->counter->Get(), 40);
  EXPECT_EQ(object.action->counter->Get(), 20);
}

TEST_F(FrameActionManagerTest,
       ExpirationDispatchesNormallyBeforeFinalizingOnce) {
  // Overshoot a duration; observe regular elapsed time, then a clamped final
  // call.
  manager.Start(action, 10, false, ScriptCall{.target = target});
  manager.AdvanceAndDispatch(3ms, 20ms);
  manager.AdvanceAndDispatch(3ms, 20ms);
  ASSERT_EQ(exec.calls.size(), 2);
  EXPECT_FALSE(exec.calls[0].end_action);
  EXPECT_EQ(exec.calls[0].time, 20);
  EXPECT_TRUE(exec.calls[1].end_action);
  EXPECT_EQ(exec.calls[1].time, 10);
  ExpectReset(*action.action);
}

TEST_F(FrameActionManagerTest, ZeroDurationFinalizesOnFirstDispatch) {
  // Dispatch a zero-duration action without elapsed time; finalize immediately.
  manager.Start(action, 0, false, ScriptCall{.target = target});
  manager.AdvanceAndDispatch(0ms, 0ms);
  ASSERT_EQ(exec.calls.size(), 2);
  EXPECT_FALSE(exec.calls[0].end_action);
  EXPECT_TRUE(exec.calls[1].end_action);
  EXPECT_EQ(exec.calls[1].time, 0);
  ExpectReset(*action.action);
}

TEST_F(FrameActionManagerTest, IndefiniteActionDoesNotExpire) {
  // Advance an indefinite action repeatedly; it keeps dispatching regular
  // calls.
  manager.Start(action, -1, false, ScriptCall{.target = target});
  manager.AdvanceAndDispatch(3ms, 20ms);
  manager.AdvanceAndDispatch(3ms, 20ms);
  ASSERT_EQ(exec.calls.size(), 2);
  EXPECT_FALSE(exec.calls[0].end_action);
  EXPECT_FALSE(exec.calls[1].end_action);
  EXPECT_TRUE(action.action->IsActive());
}

TEST_F(FrameActionManagerTest, BackgroundActionDoesNotExpire) {
  // Exceed a background action's duration; it remains active after regular
  // calls.
  auto object = Object(kLayerBg);
  manager.Start(object, 1, false, ScriptCall{.target = target});
  manager.AdvanceAndDispatch(3ms, 20ms);
  ASSERT_EQ(exec.calls.size(), 1);
  EXPECT_FALSE(exec.calls[0].end_action);
  EXPECT_EQ(exec.calls[0].time, 20);
  EXPECT_TRUE(object.action->IsActive());
}

TEST_F(FrameActionManagerTest, NextLayerActionExpires) {
  // Exceed a next-layer action's duration; it receives its final call and
  // resets.
  auto object = Object(kLayerNext);
  manager.Start(object, 1, false, ScriptCall{.target = target});
  manager.AdvanceAndDispatch(3ms, 20ms);
  ASSERT_EQ(exec.calls.size(), 2);
  EXPECT_TRUE(exec.calls[1].end_action);
  ExpectReset(*object.action);
}

TEST_F(FrameActionManagerTest, CancelGlobalsPreservesChannelsAndObjectActions) {
  // Cancel active globals silently; retain handles/list size and object state.
  auto channel = Channel();
  auto object = Object();
  for (const auto& reference : {action, channel, object})
    manager.Start(reference, -1, false,
                  ScriptCall{.target = target, .integers = {7}});
  auto counter = channel.action->counter;
  manager.CancelGlobals();
  EXPECT_TRUE(exec.calls.empty());
  ExpectReset(*action.action);
  ExpectReset(*channel.action);
  ASSERT_EQ(manager.global_channels().size(), 2);
  EXPECT_EQ(manager.global_action(), action.action);
  EXPECT_EQ(manager.global_channels()[0], channel.action);
  EXPECT_EQ(channel.action->counter, counter);
  EXPECT_EQ(manager.global_channels()[1], nullptr);
  EXPECT_EQ(std::get<CmdTarget>(object.action->call.target), target);
  EXPECT_EQ(object.action->call.integers, (std::vector<int>{7}));
  EXPECT_TRUE(object.action->counter->IsActive());
}

TEST_F(FrameActionManagerTest,
       CallbackCreatedActionDoesNotReceivePreviousDelta) {
  // Start a channel from the global callback; its first dispatch sees zero
  // time.
  auto channel = Channel();
  manager.Start(action, -1, false, ScriptCall{.target = target});
  exec.on_exec = [&](const ActionReference& reference, ScriptCall) {
    if (reference.action == action.action)
      manager.Start(channel, -1, false, ScriptCall{.target = target});
  };
  manager.AdvanceAndDispatch(3ms, 20ms);
  ASSERT_EQ(exec.calls.size(), 2);
  EXPECT_EQ(exec.calls[0].time, 20);
  EXPECT_EQ(exec.calls[1].reference.action, channel.action);
  EXPECT_EQ(exec.calls[1].time, 0);
}

TEST_F(FrameActionManagerTest, DispatchRetainsGlobalChannelsDuringCallback) {
  // Remove channels from the global callback; the dispatch snapshot retains
  // them.
  auto channel = Channel();
  manager.Start(action, -1, false, ScriptCall{.target = target});
  manager.Start(channel, -1, false, ScriptCall{.target = target});
  exec.on_exec = [&](const ActionReference& reference, ScriptCall) {
    if (reference.action == action.action)
      manager.global_channels().clear();
  };
  manager.AdvanceAndDispatch(3ms, 20ms);
  ASSERT_EQ(exec.calls.size(), 2);
  EXPECT_EQ(exec.calls[1].reference.action, channel.action);
}

TEST_F(FrameActionManagerTest, DispatchRetainsStageRootsDuringCallback) {
  // Delete stage objects from a global callback; retained roots still dispatch.
  auto object = Object();
  manager.Start(action, -1, false, ScriptCall{.target = target});
  manager.Start(object, -1, false, ScriptCall{.target = target});
  exec.on_exec = [&](const ActionReference& reference, ScriptCall) {
    if (reference.action == action.action)
      stage.objects.Clear();
  };
  manager.AdvanceAndDispatch(3ms, 20ms);
  ASSERT_EQ(exec.calls.size(), 2);
  EXPECT_EQ(exec.calls[1].reference.owner, object.owner);
}

TEST_F(FrameActionManagerTest, DispatchRetainsObjectChannelsDuringCallback) {
  // Clear an object's channels from its first channel; dispatch the second too.
  auto object = Object();
  object.owner->frame_action_list_.resize(2);
  auto first = object.owner->GetFrameAction(0);
  auto second = object.owner->GetFrameAction(1);
  manager.Start({first, object.owner}, -1, false, ScriptCall{.target = target});
  manager.Start({second, object.owner}, -1, false,
                ScriptCall{.target = target});
  exec.on_exec = [&](const ActionReference&, ScriptCall) {
    object.owner->frame_action_list_.clear();
  };
  manager.AdvanceAndDispatch(3ms, 20ms);
  ASSERT_EQ(exec.calls.size(), 2);
  EXPECT_EQ(exec.calls[0].reference.action, first);
  EXPECT_EQ(exec.calls[1].reference.action, second);
}

TEST_F(FrameActionManagerTest, DispatchRetainsChildrenDuringCallback) {
  // Clear siblings from the first child's callback; retained children still
  // run.
  auto object = Object();
  object.owner->GetChildren().resize(2);
  object.owner->TouchChild(0);
  object.owner->TouchChild(1);
  auto first = object.owner->GetChildren()[0];
  auto second = object.owner->GetChildren()[1];
  manager.Start({first->GetFrameAction(), first}, -1, false,
                ScriptCall{.target = target});
  manager.Start({second->GetFrameAction(), second}, -1, false,
                ScriptCall{.target = target});
  exec.on_exec = [&](const ActionReference&, ScriptCall) {
    object.owner->GetChildren().clear();
  };
  manager.AdvanceAndDispatch(3ms, 20ms);
  ASSERT_EQ(exec.calls.size(), 2);
  EXPECT_EQ(exec.calls[0].reference.owner, first);
  EXPECT_EQ(exec.calls[1].reference.owner, second);
}

TEST_F(FrameActionManagerTest, StartRejectsNullAction) {
  // Start an empty reference; reject it before invoking execution.
  EXPECT_THROW(manager.Start({}, 10, false, ScriptCall{.target = target}),
               std::invalid_argument);
  EXPECT_TRUE(exec.calls.empty());
}

TEST_F(FrameActionManagerTest, EndRejectsNullAction) {
  // End an empty reference; reject it before invoking execution.
  EXPECT_THROW(manager.End({}), std::invalid_argument);
  EXPECT_TRUE(exec.calls.empty());
}
