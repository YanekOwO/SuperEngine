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

#include "long_operations/reallive_task.hpp"

#include "machine/call_stack.hpp"

#include <stdexcept>
#include <utility>
#include <variant>

namespace lop {

struct RealliveTask::Registration final : IWaitRegistration {
  Registration(RealliveTask& owner,
               bool interrupt_on_input,
               Completion completion)
      : owner(owner),
        interrupt_on_input(interrupt_on_input),
        completion(std::move(completion)) {}

  ~Registration() override {
    if (owner.pending_ == this)
      owner.pending_ = nullptr;
  }

  RealliveTask& owner;
  bool interrupt_on_input;
  Completion completion;
  WaitOutcome outcome = WaitOutcome::Timeout;
};

RealliveTask::RealliveTask(std::shared_ptr<LongOperation> operation,
                           std::shared_ptr<Clock> clock)
    : operation_(std::move(operation)),
      clock_(std::move(clock)),
      waits_(*this) {
  if (!operation_ || !clock_)
    throw std::invalid_argument(
        "RealLive task requires an operation and clock");
}

RealliveTask::~RealliveTask() = default;

std::unique_ptr<IWaitRegistration> RealliveTask::Register(
    std::chrono::milliseconds delay,
    bool interrupt_on_input,
    Completion completion) {
  if (pending_)
    throw std::logic_error("RealLive task already has a pending wait");

  auto registration = std::make_unique<Registration>(*this, interrupt_on_input,
                                                     std::move(completion));
  ready_until_ = clock_->GetTime() + delay;
  pending_ = registration.get();
  return registration;
}

void RealliveTask::Advance(bool fast_forward) {
  if (finished_)
    return;

  try {
    if (!std::exchange(started_, true)) {
      task_ = operation_->Run(waits_);
      task_.Start();
    } else if (pending_ &&
               (fast_forward || clock_->GetTime() >= ready_until_)) {
      auto* registration = std::exchange(pending_, nullptr);
      auto completion = std::move(registration->completion);
      const auto outcome = registration->outcome;
      // Resumption destroys this registration and can install the next wait.
      completion(outcome);
    }

    if (task_.Done()) {
      finished_ = true;
      ready_until_ = Clock::timepoint_t::min();
      task_.TakeResult();
    }
  } catch (...) {
    // A failed factory or coroutine is terminal, even when the interpreter
    // catches the exception and retains this stack frame until its next step.
    finished_ = true;
    ready_until_ = Clock::timepoint_t::min();
    task_ = {};
    throw;
  }
}

bool RealliveTask::Done() const noexcept { return finished_; }

Clock::timepoint_t RealliveTask::ReadyUntil() const noexcept {
  return ready_until_;
}

void RealliveTask::OnEvent(std::shared_ptr<Event> event) {
  if (finished_ || !event)
    return;

  // The operation can consume the event by replacing it with monostate.
  const bool input = std::holds_alternative<KeyDown>(*event) ||
                     std::holds_alternative<KeyUp>(*event) ||
                     std::holds_alternative<MouseDown>(*event) ||
                     std::holds_alternative<MouseUp>(*event) ||
                     std::holds_alternative<MouseMotion>(*event);
  operation_->OnEvent(event);
  if (input && pending_ && pending_->interrupt_on_input) {
    pending_->outcome = WaitOutcome::InterruptedByInput;
    ready_until_ = clock_->GetTime();
  }
}

void AdvanceLongOperation(CallStack& stack, bool fast_forward) {
  auto* top = stack.Top();
  if (!top || top->frame_type != StackFrame::TYPE_LONGOP || !top->long_op)
    return;

  auto task = top->long_op;
  {
    auto lock = stack.GetLock();
    task->Advance(fast_forward);
  }

  top = stack.Top();
  if (task->Done() && top && top->long_op == task)
    stack.Pop();
}

}  // namespace lop
