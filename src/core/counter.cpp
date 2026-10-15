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
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include <boost/multiprecision/cpp_int.hpp>

using ms = std::chrono::milliseconds;

Counter::Counter() { Reset(); }

void Counter::Reset() {
  is_running_ = false;
  use_real_time_ = false;
  mode_ = Timer{};
  current_time_ = ms::zero();
}

void Counter::StartReal() {
  is_running_ = true;
  use_real_time_ = true;
  mode_ = Timer{};
  current_time_ = ms::zero();
}

void Counter::StartGame() {
  is_running_ = true;
  use_real_time_ = false;
  mode_ = Timer{};
  current_time_ = ms::zero();
}

void Counter::StartFrameReal(int start, int end, ms duration, bool loop) {
  is_running_ = true;
  use_real_time_ = true;
  mode_ = Frame{.loop = loop, .start = start, .end = end, .duration = duration};
  current_time_ = ms::zero();
}

void Counter::StartFrameGame(int start, int end, ms duration, bool loop) {
  is_running_ = true;
  use_real_time_ = false;
  mode_ = Frame{.loop = loop, .start = start, .end = end, .duration = duration};
  current_time_ = ms::zero();
}

void Counter::Stop() { is_running_ = false; }

void Counter::Resume() { is_running_ = true; }

void Counter::Set(int value) {
  const Frame* frame = std::get_if<Frame>(&mode_);
  if (!frame) {
    current_time_ = ms(value);
  } else if (frame->end == frame->start || frame->duration <= ms::zero()) {
    current_time_ = ms::zero();
  } else {
    const std::int64_t offset = static_cast<std::int64_t>(value) - frame->start;
    const std::int64_t range =
        static_cast<std::int64_t>(frame->end) - frame->start;
    // Chrono durations can span more than an int's worth of milliseconds;
    // keep the intermediate product wide until after clamping.
    using WideInt = boost::multiprecision::int128_t;
    const WideInt elapsed = WideInt(offset) * frame->duration.count() / range;
    const auto maximum =
        frame->loop ? frame->duration - ms(1) : frame->duration;
    current_time_ = ms(
        static_cast<ms::rep>(std::clamp<WideInt>(elapsed, 0, maximum.count())));
  }
}

void Counter::Advance(ms game_time, ms real_time) {
  if (!is_running_)
    return;

  const auto elapsed =
      std::max(ms::zero(), use_real_time_ ? real_time : game_time);
  if (current_time_ > ms::max() - elapsed)
    current_time_ = ms::max();
  else
    current_time_ += elapsed;

  const Frame* frame = std::get_if<Frame>(&mode_);
  if (frame && !frame->loop && current_time_ >= frame->duration)
    is_running_ = false;
}

int Counter::Get() const {
  const Frame* frame = std::get_if<Frame>(&mode_);
  if (!frame) {
    return static_cast<int>(std::clamp<ms::rep>(
        current_time_.count(), std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max()));
  }
  if (frame->duration <= ms::zero() || frame->start == frame->end)
    return frame->end;

  const std::int64_t range =
      static_cast<std::int64_t>(frame->end) - frame->start;
  using WideInt = boost::multiprecision::int128_t;
  if (frame->loop) {
    // The interpolation repeats once per frame duration, for both ascending
    // and descending ranges. A wide product also handles long durations.
    auto cycle_time = current_time_ % frame->duration;
    if (cycle_time < ms::zero())
      cycle_time += frame->duration;
    const std::int64_t value = static_cast<std::int64_t>(
        WideInt(range) * cycle_time.count() / frame->duration.count());
    return static_cast<int>(value + frame->start);
  }

  // A completed one-shot is exactly at its endpoint.
  if (current_time_ >= frame->duration)
    return frame->end;

  const std::int64_t value =
      static_cast<std::int64_t>(WideInt(range) * current_time_.count() /
                                frame->duration.count()) +
      frame->start;
  const int minimum = std::min(frame->start, frame->end);
  const int maximum = std::max(frame->start, frame->end);
  return static_cast<int>(std::clamp<std::int64_t>(value, minimum, maximum));
}

// ------------------------------------------------------------------------------

CounterList::CounterList(std::size_t size) {
  elements_.reserve(size);
  for (std::size_t i = 0; i < size; ++i)
    elements_.emplace_back(std::make_shared<Counter>());
}

std::shared_ptr<Counter> CounterList::GetHandle(int index) const {
  if (index < 0 || static_cast<std::size_t>(index) >= elements_.size())
    throw std::out_of_range("counter index out of range: " +
                            std::to_string(index));
  return elements_[static_cast<std::size_t>(index)];
}

void CounterList::Advance(ms game_time, ms real_time) {
  for (const std::shared_ptr<Counter>& counter : elements_)
    counter->Advance(game_time, real_time);
}

void CounterList::Reset() {
  for (const std::shared_ptr<Counter>& counter : elements_)
    counter->Reset();
}
