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
//
// -----------------------------------------------------------------------

#include <gtest/gtest.h>

#include "mock_clock.hpp"

#include "vm/object.hpp"
#include "vm/scheduler.hpp"

#include <chrono>
#include <memory>
#include <stdexcept>

namespace serilang_test {

using namespace serilang;
using namespace std::chrono_literals;

class SchedulerTest : public ::testing::Test {
 protected:
  struct FakePoller : IPoller {
    void Wait(std::chrono::milliseconds timeout) override {
      ++wait_calls;
      last_timeout = timeout;
    }

    int wait_calls = 0;
    std::chrono::milliseconds last_timeout{};
  };

  void SetUp() override {
    auto p = std::make_unique<FakePoller>();
    auto c = std::make_unique<MockClock>();
    poller = p.get();
    clock = c.get();
    scheduler = Scheduler(std::move(p), std::move(c));
  }

  FakePoller* poller = nullptr;
  MockClock* clock = nullptr;
  Scheduler scheduler;
};

TEST_F(SchedulerTest, IsIdle) {
  EXPECT_TRUE(scheduler.IsIdle());

  Fiber fiber;
  fiber.state = FiberState::Suspended;
  scheduler.PushTask(&fiber);
  EXPECT_FALSE(scheduler.IsIdle());

  EXPECT_EQ(scheduler.NextTask(), &fiber);
  EXPECT_TRUE(scheduler.IsIdle());
}

TEST_F(SchedulerTest, IgnoresDeadFibers) {
  Fiber fiber;
  fiber.state = FiberState::Dead;
  scheduler.PushTask(&fiber);
  scheduler.PushMicroTask(&fiber);

  EXPECT_TRUE(scheduler.IsIdle());
  EXPECT_EQ(scheduler.NextTask(), nullptr);
  EXPECT_EQ(fiber.state, FiberState::Dead);
}

TEST_F(SchedulerTest, MicroTasksPreemptRunQueue) {
  Fiber run1, run2, micro1, micro2;

  run1.state = run2.state = micro1.state = micro2.state = FiberState::Suspended;

  scheduler.PushTask(&run1);
  scheduler.PushTask(&run2);
  scheduler.PushMicroTask(&micro1);
  scheduler.PushMicroTask(&micro2);

  EXPECT_EQ(scheduler.NextTask(), &micro2);
  EXPECT_EQ(scheduler.NextTask(), &micro1);
  EXPECT_EQ(scheduler.NextTask(), &run1);
  EXPECT_EQ(scheduler.NextTask(), &run2);
  EXPECT_EQ(scheduler.NextTask(), nullptr);
  EXPECT_TRUE(scheduler.IsIdle());
}

TEST_F(SchedulerTest, DrainExpiredTimers) {
  Fiber fiber;
  fiber.state = FiberState::Suspended;

  const auto start = clock->GetTime();
  scheduler.PushAt(&fiber, start + 5ms);

  scheduler.DrainExpiredTimers();
  EXPECT_EQ(fiber.state, FiberState::Suspended);
  EXPECT_EQ(scheduler.NextTask(), nullptr);

  clock->AdvanceTime(4ms);
  scheduler.DrainExpiredTimers();
  EXPECT_EQ(scheduler.NextTask(), nullptr);

  clock->AdvanceTime(1ms);
  scheduler.DrainExpiredTimers();
  EXPECT_EQ(fiber.state, FiberState::Running);
  EXPECT_EQ(scheduler.NextTask(), &fiber);
  EXPECT_EQ(scheduler.NextTask(), nullptr);
}

TEST_F(SchedulerTest, DrainExpiredTimersRunsCallbacks) {
  Fiber fiber;
  fiber.state = FiberState::Suspended;

  bool callback_invoked = false;
  const auto base = clock->GetTime();
  scheduler.PushAt(&fiber, base + 5ms);
  scheduler.PushCallbackAt([&] { callback_invoked = true; }, base + 7ms);

  fiber.state = FiberState::Dead;

  clock->AdvanceTime(5ms);
  scheduler.DrainExpiredTimers();
  EXPECT_FALSE(callback_invoked);
  EXPECT_EQ(scheduler.NextTask(), nullptr);

  clock->AdvanceTime(2ms);
  scheduler.DrainExpiredTimers();
  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(scheduler.NextTask(), nullptr);
}

TEST_F(SchedulerTest, TimerFailurePropagatesWithoutReplayingTheCallback) {
  int failed_calls = 0;
  int later_calls = 0;
  const auto base = clock->GetTime();
  scheduler.PushCallbackAt(
      [&] {
        ++failed_calls;
        throw std::runtime_error("frame callback failed");
      },
      base + 5ms);
  scheduler.PushCallbackAt([&] { ++later_calls; }, base + 6ms);

  clock->AdvanceTime(10ms);
  EXPECT_THROW(scheduler.DrainExpiredTimers(), std::runtime_error);
  EXPECT_EQ(failed_calls, 1);
  EXPECT_EQ(later_calls, 0);

  EXPECT_NO_THROW(scheduler.DrainExpiredTimers());
  EXPECT_EQ(failed_calls, 1);
  EXPECT_EQ(later_calls, 1);
}

TEST_F(SchedulerTest, WaitPoller) {
  Fiber fiber;
  fiber.state = FiberState::Suspended;

  const auto base = clock->GetTime();
  scheduler.PushAt(&fiber, base + 20ms);
  scheduler.WaitForNext();

  EXPECT_EQ(poller->wait_calls, 1);
  EXPECT_EQ(poller->last_timeout, 20ms);

  clock->AdvanceTime(5ms);
  scheduler.WaitForNext();
  EXPECT_EQ(poller->wait_calls, 2);
  EXPECT_EQ(poller->last_timeout, 15ms);

  clock->AdvanceTime(20ms);
  scheduler.WaitForNext();
  EXPECT_EQ(poller->wait_calls, 3);
  EXPECT_EQ(poller->last_timeout, 0ms);
}

TEST_F(SchedulerTest, WaitForNextDoesNothingWithoutTimers) {
  scheduler.WaitForNext();
  EXPECT_EQ(poller->wait_calls, 0);
}

TEST_F(SchedulerTest, ScheduleAtOffsets) {
  Fiber fiber;
  fiber.state = FiberState::Suspended;
  bool callback_invoked = false;

  scheduler.PushAfter(&fiber, 10ms);
  scheduler.PushCallbackAfter([&] { callback_invoked = true; }, 15ms);

  scheduler.WaitForNext();
  EXPECT_EQ(poller->last_timeout, 10ms);

  scheduler.DrainExpiredTimers();
  EXPECT_EQ(scheduler.NextTask(), nullptr);
  EXPECT_FALSE(callback_invoked);

  clock->AdvanceTime(10ms);
  scheduler.DrainExpiredTimers();
  EXPECT_EQ(fiber.state, FiberState::Running);
  EXPECT_EQ(scheduler.NextTask(), &fiber);

  scheduler.WaitForNext();
  EXPECT_EQ(poller->last_timeout, 5ms);

  clock->AdvanceTime(5ms);
  scheduler.DrainExpiredTimers();
  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(scheduler.NextTask(), nullptr);
}

}  // namespace serilang_test
