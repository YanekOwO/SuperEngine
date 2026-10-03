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

#pragma once

#include "core/interpolation.hpp"

#include <chrono>
#include <memory>

class Clock;

// Frame Counters
//
// "Frame counters are designed to make it simple to ensure events happen at a
// constant speed regardless of the host system's specifications. Once a frame
// counter has been initialized, it will count from one arbitrary number to
// another, over a given length of time. The counter can be queried at any
// point to get its current value."

class FrameCounter {
 public:
  FrameCounter(std::shared_ptr<Clock> clock,
               Interpolation interpolation,
               int milliseconds);

  // Returns the current frame value.
  float ReadFrame();

  // Stops the timer and freezes it at |value|.
  void SetFrame(int value);

  // Starts or restarts the timer, optionally after |delay|.
  void BeginTimer(
      std::chrono::milliseconds delay = std::chrono::milliseconds(0));

  // Terminates the frame counter. One-shot counters yield the final value;
  // repeating counters freeze at their current value.
  void EndTimer();

  inline bool IsFinished() const { return !is_active_; }
  inline bool IsActive() const { return is_active_; }

 private:
  // Computes an un-clamped fraction of how far along we are, i.e.
  // 0.0 at start_time_, 1.0 at exactly total_time_, and >1.0 if time is beyond
  // total_time_.
  double ComputeNormalizedTime() const;

  std::shared_ptr<Clock> clock_;
  Interpolation interpolation_;
  float value_;
  bool is_active_;

  std::chrono::milliseconds start_time_;
  std::chrono::milliseconds total_time_;
};
