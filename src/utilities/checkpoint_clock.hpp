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

#include "utilities/clock.hpp"

#include <memory>
#include <optional>

// Resources prepared for a load share this clock. Their restored elapsed time
// remains frozen until script execution resumes, including both load fades.
class CheckpointClock final : public Clock {
 public:
  explicit CheckpointClock(std::shared_ptr<Clock> source)
      : source_(std::move(source)),
        frozen_time_(source_->GetTime()),
        frozen_ticks_(source_->GetTicks()) {}

  void Activate() {
    if (!activation_) {
      activation_ = source_->GetTime();
      activation_ticks_ = source_->GetTicks();
    }
  }
  timepoint_t GetTime() const override {
    return activation_ ? frozen_time_ + (source_->GetTime() - *activation_)
                       : frozen_time_;
  }
  duration_t GetTicks() const override {
    return activation_ ? frozen_ticks_ + source_->GetTicks() - activation_ticks_
                       : frozen_ticks_;
  }

 private:
  std::shared_ptr<Clock> source_;
  timepoint_t frozen_time_;
  duration_t frozen_ticks_;
  std::optional<timepoint_t> activation_;
  duration_t activation_ticks_{};
};
