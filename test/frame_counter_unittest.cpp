// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
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

#include "core/counter.hpp"
#include "core/frame_counter.hpp"
#include "mock_clock.hpp"
#include "utilities/clock.hpp"

#include <chrono>
#include <memory>

using std::chrono_literals::operator""ms;

enum class TimeSourceKind { Clock, Counter };

class FrameCounterTest : public ::testing::TestWithParam<TimeSourceKind> {
 protected:
  void SetUp() override {
    clock_ = std::make_shared<MockClock>();
    clock_->SetElapsed(0ms);
    source_counter_ = std::make_shared<Counter>();
    source_counter_->StartGame();
  }

  FrameCounter MakeCounter(int frame_min,
                           int frame_max,
                           int milliseconds,
                           InterpolationType type = InterpolationType::OneShot,
                           InterpolationMode mode = InterpolationMode::Linear) {
    if (GetParam() == TimeSourceKind::Counter)
      return FrameCounter(
          source_counter_,
          Interpolation(Range(frame_min, frame_max), type, mode), milliseconds);
    return FrameCounter(clock_,
                        Interpolation(Range(frame_min, frame_max), type, mode),
                        milliseconds);
  }

  void AdvanceTime(std::chrono::milliseconds delta) {
    clock_->AdvanceTime(delta);
    source_counter_->Advance(delta, 0ms);
  }

  std::shared_ptr<MockClock> clock_;
  std::shared_ptr<Counter> source_counter_;
};

INSTANTIATE_TEST_SUITE_P(
    TimeSources,
    FrameCounterTest,
    ::testing::Values(TimeSourceKind::Clock, TimeSourceKind::Counter),
    [](const ::testing::TestParamInfo<TimeSourceKind>& info) {
      return info.param == TimeSourceKind::Clock ? "Clock" : "Counter";
    });

TEST_P(FrameCounterTest, LinearProgression) {
  auto counter = MakeCounter(/*frame_min=*/0, /*frame_max=*/10,
                             /*milliseconds=*/1000);

  auto delay = 100ms;
  counter.BeginTimer(delay);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0)
      << "At time=-100ms, the frame should be 0 (initial value)";

  AdvanceTime(100ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0)
      << "At time=0, the frame should be 0 (initial value)";

  AdvanceTime(100ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 1);

  AdvanceTime(400ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 5);

  AdvanceTime(500ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 10);
  EXPECT_FALSE(counter.IsActive());
}

TEST_P(FrameCounterTest, ZeroAndNegativeDurationFinishImmediately) {
  auto zero = MakeCounter(5, 10, 0);
  auto negative = MakeCounter(5, 10, -100);

  EXPECT_FLOAT_EQ(zero.ReadFrame(), 10);
  EXPECT_FALSE(zero.IsActive());
  EXPECT_FLOAT_EQ(negative.ReadFrame(), 10);
  EXPECT_FALSE(negative.IsActive());
}

TEST_P(FrameCounterTest, NegativeDelayStartsOneShotCountersPartwayThrough) {
  const auto already_elapsed = std::chrono::milliseconds(-500);

  auto linear = MakeCounter(0, 1, 1000);
  linear.BeginTimer(already_elapsed);
  EXPECT_FLOAT_EQ(linear.ReadFrame(), 0.5f);

  auto accelerating = MakeCounter(0, 1, 1000, InterpolationType::OneShot,
                                  InterpolationMode::Accelerate);
  accelerating.BeginTimer(already_elapsed);
  EXPECT_FLOAT_EQ(accelerating.ReadFrame(), 0.25f);

  auto decelerating = MakeCounter(0, 1, 1000, InterpolationType::OneShot,
                                  InterpolationMode::Decelerate);
  decelerating.BeginTimer(already_elapsed);
  EXPECT_FLOAT_EQ(decelerating.ReadFrame(), 0.75f);
}

TEST_P(FrameCounterTest, ConstantRangeFinishesImmediately) {
  auto counter = MakeCounter(5, 5, 1000);

  EXPECT_FLOAT_EQ(counter.ReadFrame(), 5);
  EXPECT_FALSE(counter.IsActive());
  AdvanceTime(500ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 5);
}

TEST_P(FrameCounterTest, OneShotEarlyEndYieldsFinalValue) {
  auto counter = MakeCounter(0, 5, 1000);

  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);
  EXPECT_TRUE(counter.IsActive());

  AdvanceTime(10ms);
  counter.EndTimer();
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 5);
  EXPECT_FALSE(counter.IsActive());
}

TEST_P(FrameCounterTest, BasicLoop) {
  auto counter = MakeCounter(0, 3, 300, InterpolationType::Loop);

  auto delay = 100ms;
  counter.BeginTimer(delay);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);

  AdvanceTime(delay);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);
  AdvanceTime(100ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 1);
  AdvanceTime(200ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);

  EXPECT_TRUE(counter.IsActive())
      << "Because it's a loop, it remains active at cycle boundaries.";

  AdvanceTime(10ms);
  int frame_val = counter.ReadFrame();
  EXPECT_GE(frame_val, 0);
  EXPECT_LE(frame_val, 1);
}

TEST_P(FrameCounterTest, LoopEarlyEndFreezesCurrentValue) {
  auto counter = MakeCounter(0, 5, 1000, InterpolationType::Loop);

  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);
  EXPECT_TRUE(counter.IsActive());

  AdvanceTime(100ms);
  counter.EndTimer();
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0.5f);
  EXPECT_FALSE(counter.IsActive());
}

TEST_P(FrameCounterTest, LoopSupportsEasing) {
  auto accelerating = MakeCounter(0, 100, 1000, InterpolationType::Loop,
                                  InterpolationMode::Accelerate);
  auto decelerating = MakeCounter(0, 100, 1000, InterpolationType::Loop,
                                  InterpolationMode::Decelerate);
  accelerating.BeginTimer(100ms);
  decelerating.BeginTimer(100ms);

  EXPECT_FLOAT_EQ(accelerating.ReadFrame(), 0);
  EXPECT_FLOAT_EQ(decelerating.ReadFrame(), 0);

  AdvanceTime(600ms);
  EXPECT_FLOAT_EQ(accelerating.ReadFrame(), 25);
  EXPECT_FLOAT_EQ(decelerating.ReadFrame(), 75);
}

TEST_P(FrameCounterTest, BasicTurn) {
  auto counter = MakeCounter(2, 5, 300, InterpolationType::Turn);

  auto delay = 100ms;
  counter.BeginTimer(delay);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 2);

  AdvanceTime(delay);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 2);

  AdvanceTime(100ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 3);

  AdvanceTime(100ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 4);

  AdvanceTime(100ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 5);

  AdvanceTime(100ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 4);
}

TEST_P(FrameCounterTest, TurnEarlyEndFreezesCurrentValue) {
  auto counter = MakeCounter(0, 5, 1000, InterpolationType::Turn);

  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);
  EXPECT_TRUE(counter.IsActive());

  AdvanceTime(1100ms);
  counter.EndTimer();
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 4.5f);
  EXPECT_FALSE(counter.IsActive());
}

TEST_P(FrameCounterTest, TurnSupportsEasingInBothDirections) {
  auto accelerating = MakeCounter(0, 100, 1000, InterpolationType::Turn,
                                  InterpolationMode::Accelerate);
  auto decelerating = MakeCounter(0, 100, 1000, InterpolationType::Turn,
                                  InterpolationMode::Decelerate);

  AdvanceTime(500ms);
  EXPECT_FLOAT_EQ(accelerating.ReadFrame(), 25);
  EXPECT_FLOAT_EQ(decelerating.ReadFrame(), 75);

  AdvanceTime(1000ms);
  EXPECT_FLOAT_EQ(accelerating.ReadFrame(), 25);
  EXPECT_FLOAT_EQ(decelerating.ReadFrame(), 75);
}

TEST_P(FrameCounterTest, AcceleratingOneShot) {
  auto counter = MakeCounter(0, 10, 1000, InterpolationType::OneShot,
                             InterpolationMode::Accelerate);

  auto delay = 100ms;
  counter.BeginTimer(delay);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);

  AdvanceTime(delay);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);

  AdvanceTime(500ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 2.5f);

  AdvanceTime(500ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 10);
  EXPECT_FALSE(counter.IsActive());
}

TEST_P(FrameCounterTest, DeceleratingOneShot) {
  auto counter = MakeCounter(0, 10, 1000, InterpolationType::OneShot,
                             InterpolationMode::Decelerate);

  auto delay = 100ms;
  counter.BeginTimer(delay);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);

  AdvanceTime(delay);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);

  AdvanceTime(100ms);
  EXPECT_GE(counter.ReadFrame(), 1);

  AdvanceTime(900ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 10);
  EXPECT_FALSE(counter.IsActive());
}

TEST_P(FrameCounterTest, SetFrameStopsAndFreezesEveryPlaybackType) {
  auto one_shot = MakeCounter(0, 100, 1000);
  auto loop = MakeCounter(0, 100, 1000, InterpolationType::Loop);
  auto turn = MakeCounter(0, 100, 1000, InterpolationType::Turn);

  one_shot.SetFrame(11);
  loop.SetFrame(22);
  turn.SetFrame(33);
  AdvanceTime(1000ms);

  EXPECT_FLOAT_EQ(one_shot.ReadFrame(), 11);
  EXPECT_FLOAT_EQ(loop.ReadFrame(), 22);
  EXPECT_FLOAT_EQ(turn.ReadFrame(), 33);
  EXPECT_TRUE(one_shot.IsFinished());
  EXPECT_TRUE(loop.IsFinished());
  EXPECT_TRUE(turn.IsFinished());
}

TEST_P(FrameCounterTest, BeginTimerCannotReactivateTerminalCounters) {
  auto zero_duration = MakeCounter(0, 10, 0, InterpolationType::Loop);
  auto constant_range = MakeCounter(5, 5, 1000, InterpolationType::Turn);

  zero_duration.BeginTimer();
  constant_range.BeginTimer();

  EXPECT_FLOAT_EQ(zero_duration.ReadFrame(), 10);
  EXPECT_FLOAT_EQ(constant_range.ReadFrame(), 5);
  EXPECT_FALSE(zero_duration.IsActive());
  EXPECT_FALSE(constant_range.IsActive());
}

TEST_P(FrameCounterTest, DefaultCopyConstructorCopiesIndependentState) {
  auto original = MakeCounter(0, 100, 1000, InterpolationType::Turn,
                              InterpolationMode::Accelerate);
  AdvanceTime(500ms);
  EXPECT_FLOAT_EQ(original.ReadFrame(), 25);

  FrameCounter copy = original;
  original.SetFrame(7);
  AdvanceTime(100ms);

  EXPECT_FLOAT_EQ(original.ReadFrame(), 7);
  EXPECT_FLOAT_EQ(copy.ReadFrame(), 36);
  EXPECT_FALSE(original.IsActive());
  EXPECT_TRUE(copy.IsActive());
}

TEST(FrameCounterCounterSourceTest,
     SamplesCurrentValueAtConstructionAndRestart) {
  auto source = std::make_shared<Counter>();
  source->StartGame();
  source->Advance(250ms, 0ms);
  FrameCounter frame(source, Interpolation(Range(0, 100)), 1000);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 0);
  EXPECT_EQ(source->Get(), 250);

  source->Advance(500ms, 0ms);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 50);

  frame.BeginTimer(100ms);
  EXPECT_EQ(source->Get(), 750);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 0);
  source->Advance(100ms, 0ms);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 0);
  source->Advance(500ms, 0ms);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 50);
  source->Advance(500ms, 0ms);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 100);
  EXPECT_TRUE(frame.IsFinished());
  EXPECT_TRUE(source->IsActive());
}

TEST(FrameCounterCounterSourceTest, CallerControlsPauseResumeAndAdvancement) {
  auto source = std::make_shared<Counter>();
  source->StartReal();
  FrameCounter frame(source, Interpolation(Range(0, 100)), 1000);
  source->Advance(500ms, 250ms);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 25);
  EXPECT_EQ(source->Get(), 250);

  source->Stop();
  frame.BeginTimer(-250ms);
  EXPECT_FALSE(source->IsActive());
  source->Advance(0ms, 250ms);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 25);
  EXPECT_TRUE(frame.IsActive());

  source->Resume();
  source->Advance(0ms, 250ms);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 50);
  frame.EndTimer();
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 100);
  EXPECT_EQ(source->Get(), 500);
  EXPECT_TRUE(source->IsActive());
  source->Advance(0ms, 250ms);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 100);
}

TEST(FrameCounterCounterSourceTest, ReadsValueChangesWhileSourceIsInactive) {
  auto source = std::make_shared<Counter>();
  source->Set(-1000);
  FrameCounter frame(source, Interpolation(Range(0, 100)), 1000);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 0);
  EXPECT_EQ(source->Get(), -1000);
  EXPECT_FALSE(source->IsActive());

  frame.BeginTimer(100ms);
  EXPECT_EQ(source->Get(), -1000);
  EXPECT_FALSE(source->IsActive());
  source->Set(-400);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 50);

  frame.SetFrame(7);
  EXPECT_EQ(source->Get(), -400);
  EXPECT_FALSE(source->IsActive());
  frame.BeginTimer();
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 0);
  source->Set(100);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 50);
  frame.EndTimer();
  EXPECT_EQ(source->Get(), 100);
  EXPECT_FALSE(source->IsActive());
}

TEST(FrameCounterCounterSourceTest,
     UsesInterpolatedSourceValueAcrossLoopBoundary) {
  auto source = std::make_shared<Counter>();
  source->StartFrameGame(0, 1000, 2000ms, true);
  FrameCounter frame(source, Interpolation(Range(0, 100)), 1000);
  EXPECT_TRUE(source->IsFrameMode());
  EXPECT_EQ(source->Get(), 0);

  source->Advance(500ms, 0ms);
  EXPECT_EQ(source->Get(), 250);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 25);
  source->Advance(1500ms, 0ms);
  EXPECT_EQ(source->Get(), 0);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 0);
  EXPECT_TRUE(frame.IsActive());
  source->Advance(1000ms, 0ms);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 50);
}

TEST(FrameCounterCounterSourceTest, FollowsDescendingAndNegativeSourceValues) {
  auto source = std::make_shared<Counter>();
  source->StartFrameReal(500, -500, 1000ms, false);
  FrameCounter frame(
      source, Interpolation(Range(0, 100), InterpolationType::Loop), 1000);
  source->Advance(0ms, 250ms);
  EXPECT_EQ(source->Get(), 250);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 0);

  frame.BeginTimer(-1000ms);
  EXPECT_EQ(source->Get(), 250);
  EXPECT_TRUE(source->IsFrameMode());
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 0);
  source->Advance(0ms, 250ms);
  EXPECT_EQ(source->Get(), 0);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 75);
  source->Advance(0ms, 500ms);
  EXPECT_EQ(source->Get(), -500);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 25);
  EXPECT_FALSE(source->IsActive());
  EXPECT_TRUE(frame.IsActive());
  frame.EndTimer();
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 25);
  EXPECT_EQ(source->Get(), -500);
}

TEST(FrameCounterCounterSourceTest, SourceResetDoesNotRebaseOrReactivateFrame) {
  auto source = std::make_shared<Counter>();
  source->StartGame();
  FrameCounter frame(source, Interpolation(Range(0, 100)), 1000);
  source->Advance(500ms, 0ms);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 50);

  source->Reset();
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 0);
  EXPECT_TRUE(frame.IsActive());
  source->Set(1000);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 100);
  EXPECT_TRUE(frame.IsFinished());
  source->Set(500);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 100);
  frame.BeginTimer(-250ms);
  EXPECT_FLOAT_EQ(frame.ReadFrame(), 25);
  EXPECT_FALSE(source->IsActive());
}

TEST(FrameCounterCounterSourceTest, CopiesShareSourceAndRetainItsLifetime) {
  std::weak_ptr<Counter> observed_source;
  {
    auto source = std::make_shared<Counter>();
    source->Set(250);
    observed_source = source;
    FrameCounter original(source, Interpolation(Range(0, 100)), 1000);
    FrameCounter copy = original;
    source->Set(750);
    EXPECT_FLOAT_EQ(original.ReadFrame(), 50);
    EXPECT_FLOAT_EQ(copy.ReadFrame(), 50);

    source.reset();
    ASSERT_FALSE(observed_source.expired());
    original.SetFrame(7);
    observed_source.lock()->Set(1000);
    EXPECT_FLOAT_EQ(original.ReadFrame(), 7);
    EXPECT_FLOAT_EQ(copy.ReadFrame(), 75);
    EXPECT_TRUE(copy.IsActive());
  }
  EXPECT_TRUE(observed_source.expired());
}
