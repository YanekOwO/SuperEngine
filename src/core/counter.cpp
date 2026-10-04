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

#include "core/counter.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

Counter::Counter() { Reset(); }

void Counter::Reset() {
  is_running_ = false;
  real_time_ = false;
  frame_mode_ = false;
  frame_loop_ = false;
  frame_start_value_ = 0;
  frame_end_value_ = 0;
  frame_time_ = 0;
  current_time_ = 0;
}

void Counter::Start(bool real_time) {
  is_running_ = true;
  real_time_ = real_time;
  frame_mode_ = false;
  current_time_ = 0;
}

void Counter::StartFrame(int start_value,
                         int end_value,
                         int frame_time,
                         bool real_time,
                         bool loop) {
  is_running_ = true;
  real_time_ = real_time;
  frame_mode_ = true;
  frame_loop_ = loop;
  frame_start_value_ = start_value;
  frame_end_value_ = end_value;
  frame_time_ = frame_time;
  current_time_ = 0;
}

void Counter::Stop() { is_running_ = false; }

void Counter::Resume() { is_running_ = true; }

void Counter::Set(int value) {
  if (!frame_mode_) {
    current_time_ = value;
  } else if (frame_end_value_ == frame_start_value_ || frame_time_ <= 0) {
    current_time_ = 0;
  } else {
    const std::int64_t offset =
        static_cast<std::int64_t>(value) - frame_start_value_;
    const std::int64_t range =
        static_cast<std::int64_t>(frame_end_value_) - frame_start_value_;
    current_time_ = offset * frame_time_ / range;
    const int maximum = frame_loop_ ? frame_time_ - 1 : frame_time_;
    current_time_ = std::clamp<std::int64_t>(current_time_, 0, maximum);
  }
}

void Counter::Advance(std::int64_t game_ms, std::int64_t real_ms) {
  if (!is_running_)
    return;

  const std::int64_t elapsed =
      std::max<std::int64_t>(0, real_time_ ? real_ms : game_ms);
  if (current_time_ > std::numeric_limits<std::int64_t>::max() - elapsed)
    current_time_ = std::numeric_limits<std::int64_t>::max();
  else
    current_time_ += elapsed;

  if (frame_mode_ && !frame_loop_ && current_time_ >= frame_time_)
    is_running_ = false;
}

int Counter::Get() const {
  if (!frame_mode_) {
    return static_cast<int>(
        std::clamp<std::int64_t>(current_time_, std::numeric_limits<int>::min(),
                                 std::numeric_limits<int>::max()));
  }
  if (frame_time_ <= 0 || frame_start_value_ == frame_end_value_)
    return frame_end_value_;

  const std::int64_t range =
      static_cast<std::int64_t>(frame_end_value_) - frame_start_value_;
  if (frame_loop_) {
    // The interpolation repeats once per frame_time_. Reducing first keeps
    // the multiplication in range even after an arbitrarily long runtime,
    // and works for both ascending and descending ranges.
    std::int64_t cycle_time = current_time_ % frame_time_;
    if (cycle_time < 0)
      cycle_time += frame_time_;
    const std::int64_t value = range * cycle_time / frame_time_;
    return static_cast<int>(value + frame_start_value_);
  }

  // A completed one-shot is exactly at its endpoint. Besides avoiding an
  // unnecessary interpolation this prevents a large accumulated time from
  // overflowing the multiplication below.
  if (current_time_ >= frame_time_)
    return frame_end_value_;

  std::int64_t value = range * current_time_ / frame_time_ + frame_start_value_;
  const int minimum = std::min(frame_start_value_, frame_end_value_);
  const int maximum = std::max(frame_start_value_, frame_end_value_);
  return static_cast<int>(std::clamp<std::int64_t>(value, minimum, maximum));
}

CounterList::CounterList(std::size_t size) {
  elements_.reserve(size);
  for (std::size_t i = 0; i < size; ++i)
    elements_.emplace_back(std::make_shared<Counter>());
}

std::weak_ptr<Counter> CounterList::GetHandle(int index) const {
  if (index < 0 || static_cast<std::size_t>(index) >= elements_.size())
    throw std::out_of_range("counter index out of range: " +
                            std::to_string(index));
  return elements_[static_cast<std::size_t>(index)];
}

void CounterList::Advance(std::int64_t game_ms, std::int64_t real_ms) {
  for (const std::shared_ptr<Counter>& counter : elements_)
    counter->Advance(game_ms, real_ms);
}

void CounterList::Reset() {
  for (const std::shared_ptr<Counter>& counter : elements_)
    counter->Reset();
}
