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

#include "core/frame_counter.hpp"
#include "mock_clock.hpp"
#include "utilities/clock.hpp"

#include <chrono>
#include <memory>

using std::chrono_literals::operator""ms;

class FrameCounterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    clock_ = std::make_shared<MockClock>();
    clock_->SetElapsed(0ms);
  }

  FrameCounter MakeCounter(int frame_min,
                           int frame_max,
                           int milliseconds,
                           InterpolationType type = InterpolationType::OneShot,
                           InterpolationMode mode = InterpolationMode::Linear) {
    return FrameCounter(clock_,
                        Interpolation(Range(frame_min, frame_max), type, mode),
                        milliseconds);
  }

  std::shared_ptr<MockClock> clock_;
};

TEST_F(FrameCounterTest, LinearProgression) {
  auto counter = MakeCounter(/*frame_min=*/0, /*frame_max=*/10,
                             /*milliseconds=*/1000);

  auto delay = 100ms;
  counter.BeginTimer(delay);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0)
      << "At time=-100ms, the frame should be 0 (initial value)";

  clock_->AdvanceTime(100ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0)
      << "At time=0, the frame should be 0 (initial value)";

  clock_->AdvanceTime(100ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 1);

  clock_->AdvanceTime(400ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 5);

  clock_->AdvanceTime(500ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 10);
  EXPECT_FALSE(counter.IsActive());
}

TEST_F(FrameCounterTest, ZeroAndNegativeDurationFinishImmediately) {
  auto zero = MakeCounter(5, 10, 0);
  auto negative = MakeCounter(5, 10, -100);

  EXPECT_FLOAT_EQ(zero.ReadFrame(), 10);
  EXPECT_FALSE(zero.IsActive());
  EXPECT_FLOAT_EQ(negative.ReadFrame(), 10);
  EXPECT_FALSE(negative.IsActive());
}

TEST_F(FrameCounterTest, NegativeDelayStartsOneShotCountersPartwayThrough) {
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

TEST_F(FrameCounterTest, ConstantRangeFinishesImmediately) {
  auto counter = MakeCounter(5, 5, 1000);

  EXPECT_FLOAT_EQ(counter.ReadFrame(), 5);
  EXPECT_FALSE(counter.IsActive());
  clock_->AdvanceTime(500ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 5);
}

TEST_F(FrameCounterTest, OneShotEarlyEndYieldsFinalValue) {
  auto counter = MakeCounter(0, 5, 1000);

  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);
  EXPECT_TRUE(counter.IsActive());

  clock_->AdvanceTime(10ms);
  counter.EndTimer();
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 5);
  EXPECT_FALSE(counter.IsActive());
}

TEST_F(FrameCounterTest, BasicLoop) {
  auto counter = MakeCounter(0, 3, 300, InterpolationType::Loop);

  auto delay = 100ms;
  counter.BeginTimer(delay);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);

  clock_->AdvanceTime(delay);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);
  clock_->AdvanceTime(100ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 1);
  clock_->AdvanceTime(200ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);

  EXPECT_TRUE(counter.IsActive())
      << "Because it's a loop, it remains active at cycle boundaries.";

  clock_->AdvanceTime(10ms);
  int frame_val = counter.ReadFrame();
  EXPECT_GE(frame_val, 0);
  EXPECT_LE(frame_val, 1);
}

TEST_F(FrameCounterTest, LoopEarlyEndFreezesCurrentValue) {
  auto counter = MakeCounter(0, 5, 1000, InterpolationType::Loop);

  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);
  EXPECT_TRUE(counter.IsActive());

  clock_->AdvanceTime(100ms);
  counter.EndTimer();
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0.5f);
  EXPECT_FALSE(counter.IsActive());
}

TEST_F(FrameCounterTest, LoopSupportsEasing) {
  auto accelerating = MakeCounter(0, 100, 1000, InterpolationType::Loop,
                                  InterpolationMode::Accelerate);
  auto decelerating = MakeCounter(0, 100, 1000, InterpolationType::Loop,
                                  InterpolationMode::Decelerate);
  accelerating.BeginTimer(100ms);
  decelerating.BeginTimer(100ms);

  EXPECT_FLOAT_EQ(accelerating.ReadFrame(), 0);
  EXPECT_FLOAT_EQ(decelerating.ReadFrame(), 0);

  clock_->AdvanceTime(600ms);
  EXPECT_FLOAT_EQ(accelerating.ReadFrame(), 25);
  EXPECT_FLOAT_EQ(decelerating.ReadFrame(), 75);
}

TEST_F(FrameCounterTest, BasicTurn) {
  auto counter = MakeCounter(2, 5, 300, InterpolationType::Turn);

  auto delay = 100ms;
  counter.BeginTimer(delay);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 2);

  clock_->AdvanceTime(delay);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 2);

  clock_->AdvanceTime(100ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 3);

  clock_->AdvanceTime(100ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 4);

  clock_->AdvanceTime(100ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 5);

  clock_->AdvanceTime(100ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 4);
}

TEST_F(FrameCounterTest, TurnEarlyEndFreezesCurrentValue) {
  auto counter = MakeCounter(0, 5, 1000, InterpolationType::Turn);

  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);
  EXPECT_TRUE(counter.IsActive());

  clock_->AdvanceTime(1100ms);
  counter.EndTimer();
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 4.5f);
  EXPECT_FALSE(counter.IsActive());
}

TEST_F(FrameCounterTest, TurnSupportsEasingInBothDirections) {
  auto accelerating = MakeCounter(0, 100, 1000, InterpolationType::Turn,
                                  InterpolationMode::Accelerate);
  auto decelerating = MakeCounter(0, 100, 1000, InterpolationType::Turn,
                                  InterpolationMode::Decelerate);

  clock_->AdvanceTime(500ms);
  EXPECT_FLOAT_EQ(accelerating.ReadFrame(), 25);
  EXPECT_FLOAT_EQ(decelerating.ReadFrame(), 75);

  clock_->AdvanceTime(1000ms);
  EXPECT_FLOAT_EQ(accelerating.ReadFrame(), 25);
  EXPECT_FLOAT_EQ(decelerating.ReadFrame(), 75);
}

TEST_F(FrameCounterTest, AcceleratingOneShot) {
  auto counter = MakeCounter(0, 10, 1000, InterpolationType::OneShot,
                             InterpolationMode::Accelerate);

  auto delay = 100ms;
  counter.BeginTimer(delay);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);

  clock_->AdvanceTime(delay);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);

  clock_->AdvanceTime(500ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 2.5f);

  clock_->AdvanceTime(500ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 10);
  EXPECT_FALSE(counter.IsActive());
}

TEST_F(FrameCounterTest, DeceleratingOneShot) {
  auto counter = MakeCounter(0, 10, 1000, InterpolationType::OneShot,
                             InterpolationMode::Decelerate);

  auto delay = 100ms;
  counter.BeginTimer(delay);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);

  clock_->AdvanceTime(delay);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 0);

  clock_->AdvanceTime(100ms);
  EXPECT_GE(counter.ReadFrame(), 1);

  clock_->AdvanceTime(900ms);
  EXPECT_FLOAT_EQ(counter.ReadFrame(), 10);
  EXPECT_FALSE(counter.IsActive());
}

TEST_F(FrameCounterTest, SetFrameStopsAndFreezesEveryPlaybackType) {
  auto one_shot = MakeCounter(0, 100, 1000);
  auto loop = MakeCounter(0, 100, 1000, InterpolationType::Loop);
  auto turn = MakeCounter(0, 100, 1000, InterpolationType::Turn);

  one_shot.SetFrame(11);
  loop.SetFrame(22);
  turn.SetFrame(33);
  clock_->AdvanceTime(1000ms);

  EXPECT_FLOAT_EQ(one_shot.ReadFrame(), 11);
  EXPECT_FLOAT_EQ(loop.ReadFrame(), 22);
  EXPECT_FLOAT_EQ(turn.ReadFrame(), 33);
  EXPECT_TRUE(one_shot.IsFinished());
  EXPECT_TRUE(loop.IsFinished());
  EXPECT_TRUE(turn.IsFinished());
}

TEST_F(FrameCounterTest, BeginTimerCannotReactivateTerminalCounters) {
  auto zero_duration = MakeCounter(0, 10, 0, InterpolationType::Loop);
  auto constant_range = MakeCounter(5, 5, 1000, InterpolationType::Turn);

  zero_duration.BeginTimer();
  constant_range.BeginTimer();

  EXPECT_FLOAT_EQ(zero_duration.ReadFrame(), 10);
  EXPECT_FLOAT_EQ(constant_range.ReadFrame(), 5);
  EXPECT_FALSE(zero_duration.IsActive());
  EXPECT_FALSE(constant_range.IsActive());
}

TEST_F(FrameCounterTest, DefaultCopyConstructorCopiesIndependentState) {
  auto original = MakeCounter(0, 100, 1000, InterpolationType::Turn,
                              InterpolationMode::Accelerate);
  clock_->AdvanceTime(500ms);
  EXPECT_FLOAT_EQ(original.ReadFrame(), 25);

  FrameCounter copy = original;
  original.SetFrame(7);
  clock_->AdvanceTime(100ms);

  EXPECT_FLOAT_EQ(original.ReadFrame(), 7);
  EXPECT_FLOAT_EQ(copy.ReadFrame(), 36);
  EXPECT_FALSE(original.IsActive());
  EXPECT_TRUE(copy.IsActive());
}
