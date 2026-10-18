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
//
// -----------------------------------------------------------------------

#include "long_operations/siglus_wait.hpp"

#include "core/event_listener.hpp"
#include "systems/event_system.hpp"
#include "utilities/overload.hpp"
#include "vm/exception.hpp"
#include "vm/future.hpp"
#include "vm/gc.hpp"
#include "vm/promise.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>

namespace lop {
namespace sr = serilang;

namespace {

bool IsAdvanceInput(const Event& event) {
  return std::visit(overload(
                        [](const KeyDown& event) {
                          return event.code == KeyCode::RETURN ||
                                 event.code == KeyCode::SPACE ||
                                 event.code == KeyCode::LCTRL ||
                                 event.code == KeyCode::RCTRL;
                        },
                        [](const MouseDown& event) {
                          return event.button == MouseButton::LEFT;
                        },
                        [](const auto&) { return false; }),
                    event);
}

class VmWaitBackend final : public IWaitBackend {
 public:
  VmWaitBackend(sr::Scheduler& scheduler, EventSystem* events)
      : scheduler_(scheduler), events_(events) {}

  std::unique_ptr<IWaitRegistration> Register(std::chrono::milliseconds delay,
                                              bool interrupt_on_input,
                                              Completion completion) override {
    auto request = std::make_shared<Request>(std::move(completion));
    auto registration = std::make_unique<Registration>(request);
    if (events_ && interrupt_on_input)
      events_->AddListener(request);

    std::weak_ptr<Request> weak = request;
    scheduler_.PushCallbackAfter(
        [weak] {
          if (auto request = weak.lock())
            request->Notify(WaitOutcome::Timeout);
        },
        delay);
    return registration;
  }

 private:
  struct Request final : EventListener {
    explicit Request(Completion completion)
        : completion(std::move(completion)) {}

    void Notify(WaitOutcome outcome) {
      // Resumption may destroy this registration while its callback runs.
      if (auto notify = std::exchange(completion, {}))
        notify(outcome);
    }
    void OnEvent(std::shared_ptr<Event> event) override {
      if (event && IsAdvanceInput(*event))
        Notify(WaitOutcome::InterruptedByInput);
    }

    Completion completion;
  };

  struct Registration : public IWaitRegistration {
    Registration(std::shared_ptr<Request> request)
        : request_(std::move(request)) {}
    ~Registration() override { request_->completion = {}; }
    std::shared_ptr<Request> request_;
  };

  sr::Scheduler& scheduler_;
  EventSystem* events_;
};

// Eager polling has no external task owner. Only the scheduled callback owns
// the producer; the event system observes it through a weak listener.
class PollingWait final : public EventListener,
                          public std::enable_shared_from_this<PollingWait> {
 public:
  PollingWait(sr::Scheduler& scheduler,
              EventSystem* events,
              std::function<bool()> done,
              std::chrono::milliseconds interval,
              std::function<void()> on_key,
              std::shared_ptr<sr::Promise> promise)
      : scheduler_(scheduler),
        events_(events),
        done_(std::move(done)),
        interval_(std::max(interval, std::chrono::milliseconds::zero())),
        on_key_(std::move(on_key)),
        promise_(std::move(promise)) {}

  ~PollingWait() override {
    if (events_)
      events_->RemoveListener(weak_from_this());
    try {
      promise_->Reject("native coroutine task cancelled");
    } catch (...) {
      // Scheduler teardown must not throw.
    }
  }

  void Start() {
    if (events_)
      events_->AddListener(shared_from_this());
    Schedule();
  }

  void OnEvent(std::shared_ptr<Event> event) override {
    if (finished_ || !event || !IsAdvanceInput(*event))
      return;
    finished_ = true;
    try {
      if (on_key_)
        on_key_();
    } catch (const sr::RuntimeError& e) {
      promise_->Reject(e.message());
      return;
    }
    // Other skip exceptions escape to the input caller and stop polling.
    promise_->Resolve(sr::Value(1));
  }

 private:
  void Schedule() {
    scheduler_.PushCallbackAfter([self = shared_from_this()] { self->Poll(); },
                                 interval_);
  }

  void Poll() {
    if (finished_)
      return;

    bool done = false;
    try {
      done = done_();
    } catch (const std::exception& e) {
      finished_ = true;
      promise_->Reject(e.what());
      return;
    } catch (...) {
      finished_ = true;
      promise_->Reject("wait predicate threw an unknown exception");
      return;
    }
    if (done) {
      finished_ = true;
      promise_->Resolve(sr::Value(0));
    } else {
      Schedule();
    }
  }

  sr::Scheduler& scheduler_;
  EventSystem* events_;
  std::function<bool()> done_;
  std::chrono::milliseconds interval_;
  std::function<void()> on_key_;
  std::shared_ptr<sr::Promise> promise_;
  bool finished_ = false;
};

}  // namespace

struct FutureBackedCoroutineTask::State {
  State(sr::VM& vm,
        std::unique_ptr<LongOperation> operation,
        EventSystem* events)
      : operation(std::move(operation)),
        backend(vm.scheduler_, events),
        waits(backend, [this](std::coroutine_handle<> h) { Drive(h); }) {
    if (!this->operation)
      throw std::invalid_argument("native coroutine operation is null");
    promise->initial_await = [this](sr::VM&, sr::Value&, sr::Value&) {
      Start();
    };
  }

  ~State() { Cancel(); }

  void Start() {
    if (!std::exchange(started, true))
      Drive({});
  }

  void Drive(std::coroutine_handle<> h) {
    if (h) {
      h.resume();
    } else {
      coroutine = operation->Run(waits);
      coroutine.Start();
    }
    if (!coroutine.Done())
      return;

    // Settlement happens after native final suspension. Wakers may now prune
    // or destroy the owner; keep only the promise alive and do not touch the
    // state after notification.
    auto completion = promise;
    int value = 0;
    std::optional<std::string> error;
    try {
      value = coroutine.TakeResult();
    } catch (const sr::RuntimeError& e) {
      error = e.message();
    }
    // Other task exceptions escape to the original await/timer/input caller.
    // Keep promise notification outside the task-exception translation catch.
    if (error)
      completion->Reject(std::move(*error));
    else
      completion->Resolve(sr::Value(value));
  }

  void Cancel() noexcept {
    started = true;
    coroutine = {};
    operation.reset();
    promise->initial_await = {};
    try {
      promise->Reject("native coroutine task cancelled");
    } catch (...) {
      // Cancellation/destruction must not throw.
    }
  }

  bool Done() const noexcept { return started && coroutine.Done(); }

  // Reverse destruction keeps operation and wait context alive for the frame.
  std::unique_ptr<LongOperation> operation;
  VmWaitBackend backend;
  WaitContext waits;
  std::shared_ptr<sr::Promise> promise = std::make_shared<sr::Promise>();
  Task<int> coroutine;
  bool started = false;
};

FutureBackedCoroutineTask::FutureBackedCoroutineTask(
    sr::VM& vm,
    std::unique_ptr<LongOperation> operation,
    EventSystem* event_system)
    : state_(std::make_unique<State>(vm, std::move(operation), event_system)) {}

FutureBackedCoroutineTask::~FutureBackedCoroutineTask() = default;

FutureBackedCoroutineTask::FutureBackedCoroutineTask(
    FutureBackedCoroutineTask&&) noexcept = default;

FutureBackedCoroutineTask& FutureBackedCoroutineTask::operator=(
    FutureBackedCoroutineTask&&) noexcept = default;

void FutureBackedCoroutineTask::Start() {
  if (state_)
    state_->Start();
}

void FutureBackedCoroutineTask::Cancel() noexcept {
  if (state_)
    state_->Cancel();
}

sr::Future* FutureBackedCoroutineTask::MakeFuture(sr::GarbageCollector& gc) {
  if (!state_)
    throw std::logic_error("native coroutine task owner is empty");
  auto* future = gc.Allocate<sr::Future>();
  future->promise = state_->promise;
  return future;
}

bool FutureBackedCoroutineTask::Done() const noexcept {
  return !state_ || state_->Done();
}

sr::Value MakeResolvedFuture(sr::GarbageCollector& gc, int result) {
  auto* future = gc.Allocate<sr::Future>();
  future->promise->Resolve(sr::Value(result));
  return sr::Value(future);
}

sr::Value MakePollingWaitFuture(sr::VM& vm,
                                std::function<bool()> done,
                                bool key_skip,
                                EventSystem* event_system,
                                std::chrono::milliseconds poll_interval,
                                std::function<void()> on_key) {
  if (!done || done())
    return MakeResolvedFuture(*vm.gc_);

  auto* future = vm.gc_->Allocate<sr::Future>();
  auto polling = std::make_shared<PollingWait>(
      vm.scheduler_, key_skip ? event_system : nullptr, std::move(done),
      poll_interval, std::move(on_key), future->promise);
  // Polling starts at creation. The external timer keeps its state alive;
  // making a future alone does not add new VM pending-work tracking.
  polling->Start();
  return sr::Value(future);
}

sr::Future* PendingCoroutineTasks::MakeFuture(
    sr::VM& vm,
    std::unique_ptr<LongOperation> operation,
    EventSystem* event_system) {
  return MakeFuture(*vm.gc_, FutureBackedCoroutineTask(vm, std::move(operation),
                                                       event_system));
}

sr::Future* PendingCoroutineTasks::MakeFuture(
    sr::GarbageCollector& gc,
    FutureBackedCoroutineTask pending) {
  PruneDone();
  auto* future = pending.MakeFuture(gc);
  tasks_.emplace_back(std::move(pending));
  return future;
}

void PendingCoroutineTasks::PruneDone() {
  std::erase_if(tasks_, [](const auto& task) { return task.Done(); });
}

std::size_t PendingCoroutineTasks::size() const noexcept {
  return tasks_.size();
}

}  // namespace lop
