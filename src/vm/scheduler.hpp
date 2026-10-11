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

#pragma once

#include "utilities/clock.hpp"

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <queue>
#include <vector>

namespace serilang {
struct Fiber;

struct IPoller {
  virtual ~IPoller() = default;
  virtual void Wait(std::chrono::milliseconds timeout);
};

struct TimerEntry {
  Clock::timepoint_t when;
  Fiber* fib = nullptr;
  std::function<void()> callback;
  inline bool operator>(const TimerEntry& other) const {
    return when > other.when;
  }
};

class Scheduler {
 public:
  Scheduler(std::unique_ptr<IPoller> poller = std::make_unique<IPoller>(),
            std::unique_ptr<Clock> clock = std::make_unique<Clock>());

  bool IsIdle() const noexcept;
  Clock::timepoint_t GetTime() const;

  void DrainExpiredTimers();
  void DrainExpiredTimers(Clock::timepoint_t deadline);
  Fiber* NextTask();
  void WaitForNext();
  void WaitForNext(Clock::timepoint_t deadline);

  void PushTask(Fiber* f);
  void PushMicroTask(Fiber* f);
  void PushAt(Fiber* f, Clock::timepoint_t when);
  void PushAfter(Fiber* f, std::chrono::milliseconds delay);
  void PushCallbackAt(std::function<void()> fn, Clock::timepoint_t when);
  void PushCallbackAfter(std::function<void()> fn,
                         std::chrono::milliseconds delay);

 private:
  std::deque<Fiber*> runq_;
  std::deque<Fiber*> microq_;
  std::priority_queue<TimerEntry,
                      std::vector<TimerEntry>,
                      std::greater<TimerEntry>>
      timers_;

  std::unique_ptr<IPoller> poller_;
  std::unique_ptr<Clock> clock_;
};

}  // namespace serilang
