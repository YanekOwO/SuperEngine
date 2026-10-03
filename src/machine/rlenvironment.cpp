// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
// Copyright (C) 2006, 2007 Elliot Glaysher
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
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
//
// -----------------------------------------------------------------------

#include "machine/rlenvironment.hpp"

#include "core/gameexe.hpp"
#include "log/domain_logger.hpp"
#include "utilities/clock.hpp"

#include <utility>

Generic& RLEnvironment::GetGenerics() { return generic_; }

void RLEnvironment::InitFrom(Gameexe& gexe) {
  generic_.val1 = gexe("INIT_ORIGINALSETING1_MOD").Int().value_or(0);
  generic_.val2 = gexe("INIT_ORIGINALSETING2_MOD").Int().value_or(0);
}

Stopwatch& RLEnvironment::GetTimer(int layer, int idx) {
  static DomainLogger logger("RLTimer");
  if (layer < 0 || layer >= 2 || idx < 0 || idx >= 255) {
    auto rec = logger(Severity::Warn);
    rec << "Invalid key provided when requesting timer. ";
    rec << "(layer=" << layer << " ,idx=" << idx << ')';
  }

  const auto key = std::make_pair(layer, idx);
  static std::shared_ptr<Clock> clock = std::make_shared<Clock>();
  if (!rltimer_.contains(key)) {
    Stopwatch timer(clock);
    timer.Apply(Stopwatch::Action::Run);
    rltimer_.emplace(key, std::move(timer));
  }
  return rltimer_.find(key)->second;
}

FrameCounter* RLEnvironment::GetFrameCounter(int layer, int idx) {
  static DomainLogger logger("FrameCounter");
  if (layer < 0 || layer >= 2 || idx < 0 || idx >= 255) {
    auto rec = logger(Severity::Warn);
    rec << "Invalid key provided when requesting frame counter. ";
    rec << "(layer=" << layer << " ,idx=" << idx << ')';
  }

  auto it = frame_counter_.find(std::make_pair(layer, idx));
  return it == frame_counter_.end() ? nullptr : &it->second;
}

void RLEnvironment::SetFrameCounter(int layer,
                                    int idx,
                                    FrameCounter counter) {
  static DomainLogger logger("FrameCounter");
  if (layer < 0 || layer >= 2 || idx < 0 || idx >= 255) {
    auto rec = logger(Severity::Warn);
    rec << "Invalid key provided when requesting frame counter. ";
    rec << "(layer=" << layer << " ,idx=" << idx << ')';
  }

  frame_counter_.insert_or_assign(std::make_pair(layer, idx),
                                  std::move(counter));
}

void RLEnvironment::ClearFrameCounter(int layer, int idx) {
  static DomainLogger logger("FrameCounter");
  if (layer < 0 || layer >= 2 || idx < 0 || idx >= 255) {
    auto rec = logger(Severity::Warn);
    rec << "Invalid key provided when clearing frame counter. ";
    rec << "(layer=" << layer << " ,idx=" << idx << ')';
  }

  frame_counter_.erase(std::make_pair(layer, idx));
}
