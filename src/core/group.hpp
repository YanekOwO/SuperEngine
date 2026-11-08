// -----------------------------------------------------------------------
//
// This file is part of RLVM
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

#include "utilities/serialization.hpp"

#include <optional>

struct Group {
  int order = 0;
  int layer = 0;

  int cancel_priority = 0;
  std::optional<int> cancel_se;

  std::optional<int> hit_button_no;
  std::optional<int> pushed_button_no;
  std::optional<int> decided_button_no;
  enum class Result { None = 0, Decided = 1, NotDecided = -2, Canceled = -1 };
  Result result = Result::None;
  std::optional<int> result_button_no;

  enum class Status { Disabled, Active, Waiting };
  Status status = Status::Disabled;
  bool cancel_enabled = false;

  // Frame-driven interaction state. These values are not promoted by wipes.
  std::optional<int> pressed_button_no;

  void Reset();
  void InitSel();
  void ClearTransientInteraction();

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) {
    ar & order & layer & cancel_priority & cancel_se & hit_button_no &
        pushed_button_no & decided_button_no & result & result_button_no &
        status & cancel_enabled & pressed_button_no;
  }
};
