// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006 Elliot Glaysher
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

#include "core/frame_counter.hpp"

#include "utilities/clock.hpp"

#include <algorithm>
#include <utility>

FrameCounter::FrameCounter(std::shared_ptr<Clock> clock,
                           Interpolation interpolation,
                           int milliseconds)
    : clock_(std::move(clock)),
      interpolation_(std::move(interpolation)),
      value_(static_cast<float>(interpolation_.StartValue())),
      is_active_(true),
      start_time_(clock_->GetTicks()),
      total_time_(std::chrono::milliseconds(std::max(0, milliseconds))) {
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

void FrameCounter::BeginTimer(std::chrono::milliseconds delay) {
  start_time_ = clock_->GetTicks() + delay;

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

double FrameCounter::ComputeNormalizedTime() const {
  auto now = clock_->GetTicks();
  auto elapsed = now - start_time_;
  return static_cast<double>(elapsed.count()) /
         static_cast<double>(total_time_.count());
}
