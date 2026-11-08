// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
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

#include "utilities/stopwatch.hpp"

#include "utilities/clock.hpp"

#include <chrono>
#include <sstream>
#include <stdexcept>

Stopwatch::Stopwatch(std::shared_ptr<Clock> clock)
    : clock_(clock),
      state_(State::Paused),
      time_(duration_t::zero()),
      lap_time_(duration_t::zero()) {
  if (clock_ == nullptr) {
    throw std::invalid_argument("Stopwatch: no clock provided.");
  }
  last_tick_ = clock_->GetTime();  // Initialize last_tick_ to the current time
}

void Stopwatch::Apply(Action action) {
  Update();

  switch (action) {
    case Action::Pause:
      if (state_ != State::Stopped) {
        state_ = State::Paused;
      }
      break;

    case Action::Run:
      state_ = State::Running;
      break;

    case Action::Reset:
      time_ = duration_t::zero();
      lap_time_ = duration_t::zero();
      state_ = State::Stopped;
      break;

    default:
      throw std::invalid_argument("Stopwatch: invalid action " +
                                  std::to_string(static_cast<int>(action)));
  }
}

Stopwatch::State Stopwatch::GetState() const { return state_; }

Stopwatch::duration_t Stopwatch::GetReading() {
  Update();
  return time_;
}

Stopwatch::duration_t Stopwatch::LapTime() {
  Update();
  const auto result = lap_time_;
  lap_time_ = duration_t::zero();
  return result;
}

void Stopwatch::Update() {
  auto now = clock_->GetTime();
  if (now < last_tick_) {
    std::ostringstream oss;
    oss << "Stopwatch error: expected clock to be monotonic, ";
    oss << "but since last observation at "
        << last_tick_.time_since_epoch().count() << ", ";
    oss << "the clock went backward to " << now.time_since_epoch().count()
        << '.';
    throw std::runtime_error(oss.str());
  }

  if (state_ == State::Running) {
    const auto deltaTime =
        std::chrono::duration_cast<duration_t>(now - last_tick_);
    time_ += deltaTime;
    lap_time_ += deltaTime;
  }
  last_tick_ = now;
}

void Stopwatch::RebaseClock() { last_tick_ = clock_->GetTime(); }
