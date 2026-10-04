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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class Counter {
 public:
  Counter();

  void Reset();
  void Start(bool real_time);
  void StartFrame(int start_value,
                  int end_value,
                  int frame_time,
                  bool real_time,
                  bool loop);
  void Stop();
  void Resume();
  void Set(int value);
  void Advance(std::int64_t game_ms, std::int64_t real_ms);
  int Get() const;
  bool IsActive() const { return is_running_; }

 private:
  bool is_running_ = false;
  bool real_time_ = false;
  bool frame_mode_ = false;
  bool frame_loop_ = false;
  int frame_start_value_ = 0;
  int frame_end_value_ = 0;
  int frame_time_ = 0;
  std::int64_t current_time_ = 0;
};

class CounterList {
 public:
  explicit CounterList(std::size_t size);

  inline std::size_t size() const { return elements_.size(); }
  std::weak_ptr<Counter> GetHandle(int index) const;
  void Advance(std::int64_t game_ms, std::int64_t real_ms);
  void Reset();

 private:
  std::vector<std::shared_ptr<Counter>> elements_;
};
