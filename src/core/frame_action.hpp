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

#include "utilities/serialization.hpp"

#include "core/counter.hpp"
#include "core/script_call.hpp"

#include <memory>
#include <vector>

struct FrameAction {
  FrameAction() = default;
  FrameAction(const FrameAction& source);
  FrameAction& operator=(const FrameAction& source);
  FrameAction(FrameAction&& source) noexcept;
  FrameAction& operator=(FrameAction&& source) noexcept;

  std::shared_ptr<Counter> GetCounter();
  inline bool IsActive() const { return active; }
  void Reset();

  // Resolve the selected scene entry when invoking the callback.
  ScriptCall call;
  bool active = false;

  // Created on first use; retained Counter handles own this allocation.
  std::shared_ptr<Counter> counter;
  int end_time = 0;
  bool end_action = false;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) {
    ar & call & active & counter & end_time;
  }
};

using FrameActionList = std::vector<std::shared_ptr<FrameAction>>;

std::shared_ptr<FrameAction> TouchFrameAction(
    std::shared_ptr<FrameAction>& action);
