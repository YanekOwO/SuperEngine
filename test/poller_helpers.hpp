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

#include "mock_clock.hpp"

#include "vm/scheduler.hpp"

#include <chrono>

using namespace std::chrono_literals;

namespace serilang {

class AdvancingPoller : public IPoller {
 public:
  explicit AdvancingPoller(MockClock& clock) : clock_(clock) {}

  void Wait(std::chrono::milliseconds timeout) override {
    clock_.AdvanceTime(std::max(timeout, 1ms));
  }

 private:
  MockClock& clock_;
};

struct FakePoller : public IPoller {
  void Wait(std::chrono::milliseconds timeout) override {
    ++wait_calls;
    last_timeout = timeout;
  }

  int wait_calls = 0;
  std::chrono::milliseconds last_timeout{};
};

struct RecordingPoller : public IPoller {
  void Wait(std::chrono::milliseconds timeout) override {
    ++wait_calls;
    last_timeout = timeout;
  }

  int wait_calls = 0;
  std::chrono::milliseconds last_timeout{};
};

}  // namespace serilang
