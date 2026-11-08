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

#include "core/counter.hpp"
#include "core/frame_action_manager.hpp"
#include "core/game_config.hpp"
#include "core/stage.hpp"

#include <memory>
#include <vector>

// EX resources have their own lifetime and continue while ordinary time stops.
struct Excall {
  struct AllocationSizes {
    int flag_count = 0;
    int counter_count = 0;
    int frame_action_count = 0;
    int object_count = 0;
    int effect_count = 0;
    int group_count = 0;
    int quake_count = 0;
    int window_count = 0;
  };

  Excall();
  // Sizes must be nonnegative and already normalized by the caller.
  void Alloc(const AllocationSizes& sizes);
  void Free(bool finish_actions);
  void Reset();
  void RequireAllocated() const;

  std::shared_ptr<char> lifetime;
  bool allocated = false;
  bool releasing = false;
  Stage stage;
  std::vector<int> flags;
  std::shared_ptr<CounterList> counters;
  LocalConfig config;
  std::shared_ptr<FrameActionManager> actions;
};
