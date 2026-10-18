// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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

#include "core/frame_counter.hpp"

#include <limits>
#include <map>
#include <string_view>

struct ObjectParameter;

struct Effect {
  int x = 0;
  int y = 0;
  int z = 0;
  int mono = 0;
  int reverse = 0;
  int bright = 0;
  int dark = 0;
  int color_r = 0;
  int color_g = 0;
  int color_b = 0;
  int color_rate = 0;
  int color_add_r = 0;
  int color_add_g = 0;
  int color_add_b = 0;
  int begin_order = 0;
  int end_order = 0;
  int begin_layer = std::numeric_limits<int>::min();
  int end_layer = std::numeric_limits<int>::max();
  int wipe_copy = 0;
  int wipe_erase = 0;

  Effect() = default;

  void Reset();
  void Execute();

  void StartEvent(int property, FrameCounter counter);
  bool IsEventRunning(int property) const;
  void EndEvent(int property);

  bool Contains(int order, int layer) const;

  void ApplyTo(ObjectParameter& param) const;

  int Get(int idx) const;
  void Set(int idx, int val);
  static std::string_view GetPropertyName(int idx);

 private:
  // Property reads retain their base/target value, while rendering uses the
  // current event value until the event ends, as in legacy Siglus.
  struct Event {
    FrameCounter counter;
    int value;
  };
  std::map<int, Event> events_;

  int GetRenderValue(int property) const;
};
