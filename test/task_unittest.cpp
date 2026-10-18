// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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
//
// -----------------------------------------------------------------------

#include "utilities/task.hpp"

#include <gtest/gtest.h>

#include <coroutine>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<Task<int>>);
static_assert(!std::is_copy_assignable_v<Task<int>>);
static_assert(std::is_nothrow_move_constructible_v<Task<int>>);
static_assert(std::is_nothrow_move_assignable_v<Task<int>>);

// An external wait with explicit teardown. No scheduler or elapsed real time
// participates in native task tests.
class ManualGate {
 public:
  struct Awaiter {
    ManualGate& gate;
    std::coroutine_handle<> handle;

    ~Awaiter() {
      if (gate.handle_ == handle)
        gate.handle_ = {};
    }

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> continuation) noexcept {
      handle = continuation;
      gate.handle_ = continuation;
    }
    void await_resume() const noexcept {}
  };

  Awaiter operator co_await() noexcept { return Awaiter{*this, {}}; }

  bool Waiting() const noexcept { return static_cast<bool>(handle_); }

  void Resume() {
    if (auto handle = std::exchange(handle_, {}))
      handle.resume();
  }

 private:
  std::coroutine_handle<> handle_;
};

struct FrameTracker {
  explicit FrameTracker(int& destructions) : destructions_(destructions) {}
  ~FrameTracker() { ++destructions_; }

  FrameTracker(const FrameTracker&) = delete;
  FrameTracker& operator=(const FrameTracker&) = delete;

 private:
  int& destructions_;
};

Task<int> Immediate(int& starts, int value = 7) {
  ++starts;
  co_return value;
}

Task<int> WaitForValue(ManualGate& gate,
                       int& starts,
                       int& destructions,
                       int value = 7) {
  FrameTracker tracker(destructions);
  ++starts;
  co_await gate;
  co_return value;
}

Task<int> ComposeWait(ManualGate& gate, int& starts, int& destructions) {
  FrameTracker tracker(destructions);
  ++starts;
  co_return 1 + co_await WaitForValue(gate, starts, destructions);
}

Task<int> AwaitOwned(Task<int> child) { co_return co_await std::move(child); }

struct NonDefault {
  explicit NonDefault(int value) : value(value) {}
  NonDefault(const NonDefault&) = delete;
  NonDefault& operator=(const NonDefault&) = delete;
  NonDefault(NonDefault&&) = default;
  NonDefault& operator=(NonDefault&&) = default;

  int value;
};

Task<NonDefault> NonDefaultValue() { co_return NonDefault(23); }

Task<std::unique_ptr<int>> MoveOnlyValue() {
  co_return std::make_unique<int>(41);
}

Task<std::unique_ptr<int>> ComposeMoveOnly() {
  co_return co_await MoveOnlyValue();
}

Task<std::string> StringValue() { co_return "native task"; }

Task<void> Touch(int& counter) {
  ++counter;
  co_return;
}

Task<void> ComposeVoid(int& counter) {
  co_await Touch(counter);
  ++counter;
}

struct NativeFailure : std::runtime_error {
  using std::runtime_error::runtime_error;
};

Task<int> Fail() {
  throw NativeFailure("native task failed");
  co_return 0;
}

Task<void> FailVoid() {
  throw NativeFailure("void task failed");
  co_return;
}

Task<int> FailAfterWait(ManualGate& gate) {
  co_await gate;
  throw NativeFailure("suspended task failed");
  co_return 0;
}

Task<int> CatchChildFailure() {
  try {
    co_return co_await Fail();
  } catch (const NativeFailure& error) {
    EXPECT_STREQ(error.what(), "native task failed");
    co_return 19;
  }
}

Task<int> FailNonStandard() {
  throw 31;
  co_return 0;
}

Task<int> DeepChain(int depth) {
  if (depth == 0)
    co_return 0;
  co_return 1 + co_await DeepChain(depth - 1);
}

Task<void> OwnParameter(std::shared_ptr<int> owner) {
  (void)owner;
  co_return;
}

TEST(TaskTest, EmptyTaskIsDoneButCannotStartOrProduceAResult) {
  Task<int> task;
  EXPECT_FALSE(task);
  EXPECT_TRUE(task.Done());
  EXPECT_THROW(task.Start(), std::logic_error);
  EXPECT_THROW(task.TakeResult(), std::logic_error);
  EXPECT_THROW(std::move(task).operator co_await(), std::logic_error);
}

TEST(TaskTest, StartsLazilyAndOnlyOnce) {
  int starts = 0;
  auto task = Immediate(starts);
  EXPECT_TRUE(task);
  EXPECT_FALSE(task.Done());
  EXPECT_EQ(starts, 0);
  EXPECT_THROW(task.TakeResult(), std::logic_error);

  task.Start();
  task.Start();

  EXPECT_EQ(starts, 1);
  EXPECT_TRUE(task.Done());
  EXPECT_EQ(task.TakeResult(), 7);
  EXPECT_THROW(task.TakeResult(), std::logic_error);
  EXPECT_NO_THROW(task.Start());
}

TEST(TaskTest, RepeatedStartDoesNotResumeThroughAnOutstandingWait) {
  ManualGate gate;
  int starts = 0;
  int destructions = 0;
  auto task = WaitForValue(gate, starts, destructions);

  task.Start();
  task.Start();

  EXPECT_EQ(starts, 1);
  EXPECT_TRUE(gate.Waiting());
  EXPECT_FALSE(task.Done());
  EXPECT_THROW(task.TakeResult(), std::logic_error);

  gate.Resume();
  EXPECT_FALSE(gate.Waiting());
  EXPECT_TRUE(task.Done());
  EXPECT_EQ(task.TakeResult(), 7);
}

TEST(TaskTest, SupportsNonDefaultConstructibleAndMoveOnlyResults) {
  auto non_default = NonDefaultValue();
  non_default.Start();
  EXPECT_EQ(non_default.TakeResult().value, 23);

  auto move_only = ComposeMoveOnly();
  move_only.Start();
  auto result = move_only.TakeResult();
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(*result, 41);

  auto string = StringValue();
  string.Start();
  EXPECT_EQ(string.TakeResult(), "native task");
}

TEST(TaskTest, ComposesVoidTasksAndConsumesVoidCompletionOnce) {
  int counter = 0;
  auto task = ComposeVoid(counter);
  EXPECT_EQ(counter, 0);

  task.Start();

  EXPECT_EQ(counter, 2);
  EXPECT_TRUE(task.Done());
  EXPECT_NO_THROW(task.TakeResult());
  EXPECT_THROW(task.TakeResult(), std::logic_error);
}

TEST(TaskTest, ComposesSuspendedChildrenAndReturnsTheirResult) {
  ManualGate gate;
  int starts = 0;
  int destructions = 0;
  {
    auto task = ComposeWait(gate, starts, destructions);
    task.Start();
    EXPECT_EQ(starts, 2);
    EXPECT_FALSE(task.Done());
    EXPECT_EQ(destructions, 0);

    gate.Resume();

    EXPECT_TRUE(task.Done());
    EXPECT_EQ(task.TakeResult(), 8);
    EXPECT_EQ(destructions, 2);
  }
  EXPECT_EQ(destructions, 2);
}

TEST(TaskTest, StoresOriginalExceptionsUntilConsumption) {
  auto task = Fail();
  EXPECT_NO_THROW(task.Start());
  EXPECT_TRUE(task.Done());
  EXPECT_THROW(task.TakeResult(), NativeFailure);
  EXPECT_THROW(task.TakeResult(), std::logic_error);

  auto void_task = FailVoid();
  EXPECT_NO_THROW(void_task.Start());
  EXPECT_THROW(void_task.TakeResult(), NativeFailure);
  EXPECT_THROW(void_task.TakeResult(), std::logic_error);

  auto non_standard = FailNonStandard();
  non_standard.Start();
  try {
    non_standard.TakeResult();
    FAIL() << "Expected the original non-standard exception";
  } catch (int error) {
    EXPECT_EQ(error, 31);
  }
}

TEST(TaskTest, ParentCanCatchOriginalChildException) {
  auto task = CatchChildFailure();
  EXPECT_NO_THROW(task.Start());
  EXPECT_EQ(task.TakeResult(), 19);
}

TEST(TaskTest, SuspendedExceptionsStayInsideTheNativeTask) {
  ManualGate gate;
  auto task = FailAfterWait(gate);
  task.Start();

  EXPECT_NO_THROW(gate.Resume());

  EXPECT_TRUE(task.Done());
  EXPECT_THROW(task.TakeResult(), NativeFailure);
}

TEST(TaskTest, CompletedStartedTaskCanBeConsumedByAParent) {
  int starts = 0;
  auto child = Immediate(starts, 13);
  child.Start();
  auto parent = AwaitOwned(std::move(child));
  EXPECT_FALSE(child);

  parent.Start();

  EXPECT_EQ(starts, 1);
  EXPECT_EQ(parent.TakeResult(), 13);
}

TEST(TaskTest, StartedSuspendedTaskCannotChangeItsDriver) {
  ManualGate gate;
  int starts = 0;
  int destructions = 0;
  auto child = WaitForValue(gate, starts, destructions);
  child.Start();

  EXPECT_THROW(std::move(child).operator co_await(), std::logic_error);
  EXPECT_TRUE(child);
  EXPECT_TRUE(gate.Waiting());

  gate.Resume();
  EXPECT_EQ(child.TakeResult(), 7);
  EXPECT_THROW(std::move(child).operator co_await(), std::logic_error);
}

TEST(TaskTest, MoveTransfersSuspendedFrameOwnership) {
  ManualGate gate;
  int starts = 0;
  int destructions = 0;
  {
    auto original = WaitForValue(gate, starts, destructions);
    original.Start();
    auto moved = std::move(original);
    EXPECT_FALSE(original);
    EXPECT_TRUE(original.Done());
    EXPECT_THROW(original.Start(), std::logic_error);
    EXPECT_EQ(destructions, 0);

    auto* same_task = &moved;
    moved = std::move(*same_task);
    EXPECT_TRUE(moved);
    EXPECT_TRUE(gate.Waiting());
  }

  EXPECT_EQ(destructions, 1);
  EXPECT_FALSE(gate.Waiting());
  EXPECT_NO_THROW(gate.Resume());
}

TEST(TaskTest, MoveAssignmentDestroysThePreviousSuspendedFrame) {
  ManualGate old_gate;
  ManualGate new_gate;
  int starts = 0;
  int destructions = 0;
  {
    auto destination = WaitForValue(old_gate, starts, destructions);
    auto source = WaitForValue(new_gate, starts, destructions, 29);
    destination.Start();
    source.Start();

    destination = std::move(source);

    EXPECT_EQ(destructions, 1);
    EXPECT_FALSE(old_gate.Waiting());
    EXPECT_TRUE(new_gate.Waiting());
    EXPECT_FALSE(source);
    new_gate.Resume();
    EXPECT_EQ(destination.TakeResult(), 29);
  }
  EXPECT_EQ(destructions, 2);
}

TEST(TaskTest, DestroyingSuspendedParentDestroysItsChildAndCancelsItsWait) {
  ManualGate gate;
  int starts = 0;
  int destructions = 0;
  {
    auto parent = ComposeWait(gate, starts, destructions);
    parent.Start();
    EXPECT_TRUE(gate.Waiting());
    EXPECT_EQ(destructions, 0);
  }

  EXPECT_EQ(destructions, 2);
  EXPECT_FALSE(gate.Waiting());
  EXPECT_NO_THROW(gate.Resume());
}

TEST(TaskTest, DestroyingColdTaskReleasesOwnedParametersWithoutStarting) {
  auto owner = std::make_shared<int>(5);
  std::weak_ptr<int> weak_owner = owner;
  {
    auto task = OwnParameter(std::move(owner));
    EXPECT_FALSE(task.Done());
    EXPECT_FALSE(weak_owner.expired());
  }
  EXPECT_TRUE(weak_owner.expired());
}

TEST(TaskTest, DeepCompositionUsesContinuationTransfer) {
  constexpr int depth = 20000;
  auto task = DeepChain(depth);
  task.Start();
  EXPECT_TRUE(task.Done());
  EXPECT_EQ(task.TakeResult(), depth);
}
