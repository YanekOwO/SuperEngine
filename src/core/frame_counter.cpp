// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006 Elliot Glaysher
// Copyright (C) 2025 Serina Sakurai
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

#include "core/frame_counter.hpp"

#include "core/counter.hpp"
#include "utilities/clock.hpp"
#include "utilities/overload.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

using ms = std::chrono::milliseconds;

FrameCounter::FrameCounter(std::shared_ptr<Clock> clock,
                           Interpolation interpolation,
                           int milliseconds)
    : FrameCounter(std::move(interpolation),
                   milliseconds,
                   TimeSource{std::move(clock)}) {}

FrameCounter::FrameCounter(std::shared_ptr<Counter> counter,
                           Interpolation interpolation,
                           int milliseconds)
    : FrameCounter(std::move(interpolation),
                   milliseconds,
                   TimeSource{std::move(counter)}) {}

FrameCounter::FrameCounter(Interpolation interpolation,
                           int milliseconds,
                           TimeSource time_source)
    : time_source_(std::move(time_source)),
      interpolation_(std::move(interpolation)),
      value_(static_cast<float>(interpolation_.StartValue())),
      is_active_(true),
      start_time_(CurrentTime()),
      total_time_(ms(std::max(0, milliseconds))) {
  if (total_time_.count() == 0 || interpolation_.IsConstant()) {
    value_ = static_cast<float>(interpolation_.EndValue());
    is_active_ = false;
  }
}

float FrameCounter::ReadFrame() {
  if (!is_active_)
    return value_;

  double fraction = ComputeNormalizedTime();
  if (interpolation_.IsOneShot() && fraction >= 1.0) {
    value_ = static_cast<float>(interpolation_.EndValue());
    is_active_ = false;
    return value_;
  }

  value_ = static_cast<float>(interpolation_.ValueAt(fraction));
  return value_;
}

void FrameCounter::SetFrame(int value) {
  value_ = static_cast<float>(value);
  is_active_ = false;
}

void FrameCounter::BeginTimer(ms delay) {
  start_time_ = CurrentTime() + delay;

  if (total_time_.count() == 0 || interpolation_.IsConstant()) {
    value_ = static_cast<float>(interpolation_.EndValue());
    is_active_ = false;
    return;
  }

  value_ = static_cast<float>(interpolation_.StartValue());
  is_active_ = true;
}

void FrameCounter::EndTimer() {
  if (!is_active_)
    return;

  if (interpolation_.IsOneShot()) {
    value_ = static_cast<float>(interpolation_.EndValue());
  } else {
    value_ =
        static_cast<float>(interpolation_.ValueAt(ComputeNormalizedTime()));
  }
  is_active_ = false;
}

ms FrameCounter::CurrentTime() const {
  return std::visit(
      overload(
          [](const std::shared_ptr<Clock>& it) { return it->GetTicks(); },
          [](const std::shared_ptr<Counter>& it) { return ms(it->Get()); }),
      time_source_);
}

double FrameCounter::ComputeNormalizedTime() const {
  const ms now = CurrentTime();
  const ms elapsed = now - start_time_;
  return static_cast<double>(elapsed.count()) /
         static_cast<double>(total_time_.count());
}
