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

#include <gtest/gtest.h>

#include "core/event_listener.hpp"
#include "long_operations/siglus_wait.hpp"
#include "mock_clock.hpp"
#include "poller_helpers.hpp"
#include "systems/event_backend.hpp"
#include "systems/event_system.hpp"
#include "vm/future.hpp"
#include "vm/promise.hpp"
#include "vm/vm.hpp"

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <utility>

namespace lop {
namespace {
using namespace std::chrono_literals;
namespace sr = serilang;

class QueuedBackend final : public IEventBackend {
 public:
  std::deque<Event> events;
  std::shared_ptr<Event> PollEvent() override {
    if (events.empty())
      return std::make_shared<Event>(std::monostate{});
    auto event = std::make_shared<Event>(std::move(events.front()));
    events.pop_front();
    return event;
  }
};

class DelayedOperation final : public LongOperation {
 public:
  explicit DelayedOperation(int& completed) : completed_(completed) {}
  Task<int> Run(WaitContext& waits) override {
    co_await waits.WaitFor(10ms, true);
    ++completed_;
    co_return 42;
  }

 private:
  int& completed_;
};

class CallbackListener final : public EventListener {
 public:
  explicit CallbackListener(std::function<void()> callback)
      : callback_(std::move(callback)) {}
  void OnEvent(std::shared_ptr<Event> event) override {
    if (event && std::holds_alternative<KeyDown>(*event))
      callback_();
  }

 private:
  std::function<void()> callback_;
};

class NativeWaitScopeTest : public ::testing::Test {
 protected:
  std::shared_ptr<sr::GarbageCollector> gc =
      std::make_shared<sr::GarbageCollector>();
  sr::VM vm{gc};
  MockClock* clock = nullptr;
  bool active = true;
  bool alive = true;

  void SetUp() override {
    auto time = std::make_unique<MockClock>(Clock::timepoint_t{});
    clock = time.get();
    vm.scheduler_ =
        sr::Scheduler(std::make_unique<sr::RecordingPoller>(), std::move(time));
  }
  NativeWaitScope Scope() {
    return {[this] { return active; }, [this] { return alive; }};
  }
  void Advance(std::chrono::milliseconds delta) {
    clock->AdvanceTime(delta);
    vm.RunReadyUntilBlocked();
  }
};

TEST_F(NativeWaitScopeTest, InactivePollingDefersItsPredicate) {
  // An inactive producer must leave the predicate untouched until reactivated.
  active = false;
  int polls = 0;
  auto future = MakePollingWaitFuture(
      vm,
      [&] {
        ++polls;
        return true;
      },
      false, nullptr, 5ms, {}, Scope());
  auto promise = future.Get<sr::Future*>()->promise;
  Advance(5ms);
  EXPECT_EQ(polls, 0);
  EXPECT_FALSE(promise->HasResult());
  active = true;
  Advance(5ms);
  EXPECT_EQ(polls, 1);
  EXPECT_EQ(promise->result->value(), sr::Value(0));
}

TEST_F(NativeWaitScopeTest, ExpiredPollingCancelsBeforeCallingItsPredicate) {
  // Released resources invalidate polling before the producer touches them.
  int polls = 0;
  auto future = MakePollingWaitFuture(
      vm,
      [&] {
        ++polls;
        return false;
      },
      false, nullptr, 5ms, {}, Scope());
  auto promise = future.Get<sr::Future*>()->promise;
  alive = false;
  Advance(5ms);
  EXPECT_EQ(polls, 1);
  ASSERT_TRUE(promise->HasResult());
  EXPECT_FALSE(promise->result->has_value());
}

TEST_F(NativeWaitScopeTest, InactivePollingIgnoresAdvanceInput) {
  // Menu input must not finish the inactive gameplay producer's key wait.
  auto backend = std::make_unique<QueuedBackend>();
  auto* queue = backend.get();
  EventSystem events(std::move(backend));
  int skipped = 0;
  auto future = MakePollingWaitFuture(
      vm, [] { return false; }, true, &events, 5ms, [&] { ++skipped; },
      Scope());
  auto promise = future.Get<sr::Future*>()->promise;
  active = false;
  queue->events.emplace_back(KeyDown{KeyCode::RETURN});
  events.ExecuteEventSystem();
  EXPECT_EQ(skipped, 0);
  EXPECT_FALSE(promise->HasResult());
  active = true;
  queue->events.emplace_back(KeyDown{KeyCode::RETURN});
  events.ExecuteEventSystem();
  EXPECT_EQ(skipped, 1);
  EXPECT_EQ(promise->result->value(), sr::Value(1));
  alive = false;
  Advance(5ms);
}

TEST_F(NativeWaitScopeTest, InactiveCoroutineDefersTimeoutContinuation) {
  // A suspended native coroutine retains a due timeout while its scope pauses.
  int completed = 0;
  FutureBackedCoroutineTask task(
      vm, std::make_unique<DelayedOperation>(completed), nullptr, {}, Scope());
  auto promise = task.MakeFuture(*gc)->promise;
  task.Start();
  active = false;
  Advance(10ms);
  EXPECT_EQ(completed, 0);
  EXPECT_FALSE(promise->HasResult());
  active = true;
  Advance(5ms);
  EXPECT_EQ(completed, 1);
  EXPECT_EQ(promise->result->value(), sr::Value(42));
}

TEST_F(NativeWaitScopeTest, CoroutineRequiresBothEligibilityPredicates) {
  // Caller-specific eligibility cannot bypass the enclosing execution scope.
  int completed = 0;
  FutureBackedCoroutineTask task(
      vm, std::make_unique<DelayedOperation>(completed), nullptr,
      [] { return true; }, Scope());
  auto promise = task.MakeFuture(*gc)->promise;
  task.Start();
  active = false;
  Advance(10ms);
  EXPECT_EQ(completed, 0);
  EXPECT_FALSE(promise->HasResult());
}

TEST_F(NativeWaitScopeTest, ExpiredCoroutineCancelsWithoutResumingItsBody) {
  // Resource teardown cancels a paused coroutine before its continuation runs.
  int completed = 0;
  FutureBackedCoroutineTask task(
      vm, std::make_unique<DelayedOperation>(completed), nullptr, {}, Scope());
  auto promise = task.MakeFuture(*gc)->promise;
  task.Start();
  active = false;
  alive = false;
  Advance(10ms);
  EXPECT_EQ(completed, 0);
  ASSERT_TRUE(promise->HasResult());
  EXPECT_FALSE(promise->result->has_value());
  EXPECT_TRUE(task.Done());
}

TEST_F(NativeWaitScopeTest, CancelledCoroutineIgnoresLateInput) {
  // An earlier input listener destroys the task after event dispatch retained
  // its wait listener; that listener must not inspect the released scope.
  auto backend = std::make_unique<QueuedBackend>();
  auto* queue = backend.get();
  EventSystem events(std::move(backend));
  int completed = 0;
  int scope_checks = 0;
  auto scope = Scope();
  scope.active = [&] {
    ++scope_checks;
    return true;
  };
  auto task = std::make_unique<FutureBackedCoroutineTask>(
      vm, std::make_unique<DelayedOperation>(completed), &events,
      std::function<bool()>{}, std::move(scope));
  auto promise = task->MakeFuture(*gc)->promise;
  task->Start();
  auto cancel = std::make_shared<CallbackListener>([&] { task.reset(); });
  events.AddListener(1, cancel);
  queue->events.emplace_back(KeyDown{KeyCode::RETURN});
  events.ExecuteEventSystem();
  EXPECT_EQ(scope_checks, 0);
  EXPECT_EQ(completed, 0);
  ASSERT_TRUE(promise->HasResult());
  EXPECT_FALSE(promise->result->has_value());
}

}  // namespace
}  // namespace lop
