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

#pragma once

#include "long_operations/long_operation.hpp"
#include "long_operations/wait_context.hpp"
#include "utilities/clock.hpp"

#include <memory>

class CallStack;

namespace lop {

// The RealLive stack owns and drives each operation. Input only makes a wait
// ready; Advance resumes it once when the interpreter reaches this stack frame.
class RealliveTask final : public EventListener, private IWaitBackend {
 public:
  RealliveTask(std::shared_ptr<LongOperation> operation,
               std::shared_ptr<Clock> clock);
  ~RealliveTask() override;

  RealliveTask(const RealliveTask&) = delete;
  RealliveTask& operator=(const RealliveTask&) = delete;
  RealliveTask(RealliveTask&&) = delete;
  RealliveTask& operator=(RealliveTask&&) = delete;

  void Advance(bool fast_forward = false);
  bool Done() const noexcept;
  Clock::timepoint_t ReadyUntil() const noexcept;
  void OnEvent(std::shared_ptr<Event> event) override;

 private:
  struct Registration;

  std::unique_ptr<IWaitRegistration> Register(std::chrono::milliseconds delay,
                                              bool interrupt_on_input,
                                              Completion completion) override;

  std::shared_ptr<LongOperation> operation_;
  std::shared_ptr<Clock> clock_;
  Clock::timepoint_t ready_until_ = Clock::timepoint_t::min();
  Registration* pending_ = nullptr;
  bool started_ = false;
  bool finished_ = false;
  WaitContext waits_;
  // Destroy the coroutine and its wait registration before their dependencies.
  Task<int> task_;
};

// Drive only the top operation. A newly pushed child remains on top even if
// its parent finishes during this step.
void AdvanceLongOperation(CallStack& stack, bool fast_forward = false);

}  // namespace lop
