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

#include <chrono>
#include <cstddef>
#include <memory>
#include <variant>
#include <vector>

// A caller-driven |Counter| that either counts elapsed milliseconds or
// linearly interpolates between integer values over a duration. Time does not
// advance automatically: callers supply deltas through Advance(), and reading
// the counter never queries a clock.
class Counter {
 public:
  // Creates an inactive elapsed-time counter with value zero.
  Counter();

  // Stops the counter, clears its value and frame settings, and selects game
  // time in elapsed-time mode.
  void Reset();

  // Starts an elapsed-time counter from zero. Advance() uses real-time delta.
  void StartReal();
  // Starts an elapsed-time counter from zero. Advance() uses game-time delta.
  void StartGame();

  // Starts a linear transition from |start| to |end|, resetting
  // elapsed time. Descending ranges are supported. StartFrameReal() uses
  // real-time delta; StartFrameGame() uses game-time delta.
  //
  // A non-looping transition holds the endpoint after the duration is reached;
  // Advance() then marks it inactive. A looping transition repeats every
  // |duration| milliseconds, returning the start value at each cycle boundary
  // and remaining active until stopped.
  //
  // If |duration| is nonpositive or the endpoints are equal, Get() always
  // returns |end|. Starting still marks the counter active; a non-looping
  // counter becomes inactive on Advance() once its duration is reached.
  void StartFrameReal(int start,
                      int end,
                      std::chrono::milliseconds duration,
                      bool should_loop);
  void StartFrameGame(int start,
                      int end,
                      std::chrono::milliseconds duration,
                      bool should_loop);

  // Freezes the current value without clearing elapsed time or mode settings.
  void Stop();

  // Marks the counter active without resetting its value or mode. A completed
  // non-looping transition becomes inactive again on the next Advance().
  void Resume();

  // Sets elapsed milliseconds in elapsed-time mode. In frame mode, converts
  // |value| to an elapsed time using integer division, then clamps that time to
  // [0, duration] for a non-looping transition or [0, duration - 1] for a
  // loop.
  // Does not change whether the counter is active.
  void Set(int value);

  // Advances an active counter by the selected delta in milliseconds. Negative
  // deltas are treated as zero, and accumulated time saturates at
  // std::chrono::milliseconds::max(). Inactive counters are unchanged.
  void Advance(std::chrono::milliseconds game_time,
               std::chrono::milliseconds real_time);

  // Returns elapsed milliseconds clamped to the int range, or the current
  // interpolated frame value. Fractional interpolation offsets are truncated
  // toward zero. Does not advance time or change active status.
  int Get() const;

  // Returns whether the counter is currently running.
  inline bool IsActive() const { return is_running_; }

  // Returns whether the counter is configured for a frame transition.
  inline bool IsFrameMode() const {
    return std::holds_alternative<Frame>(mode_);
  }

 private:
  bool is_running_ = false;
  bool use_real_time_ = false;

  struct Timer {};
  struct Frame {
    bool loop = false;
    int start = 0;
    int end = 0;
    std::chrono::milliseconds duration = std::chrono::milliseconds::zero();
  };
  std::variant<Timer, Frame> mode_;

  // Elapsed milliseconds; looping frame reads reduce this modulo the duration.
  std::chrono::milliseconds current_time_ = std::chrono::milliseconds::zero();
};

struct CounterList {
  std::vector<std::shared_ptr<Counter>> elements_;
  explicit CounterList(std::size_t size);

  inline std::size_t size() const { return elements_.size(); }
  std::shared_ptr<Counter> GetHandle(int index) const;

  // Passes the elapsed millisecond deltas to every counter; each counter uses
  // its selected time domain and advances only if active.
  void Advance(std::chrono::milliseconds game_time,
               std::chrono::milliseconds real_time);

  // Resets every counter in place, preserving the list size and existing
  // handles.
  void Reset();
};
