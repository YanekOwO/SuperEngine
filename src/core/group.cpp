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

#include "core/group.hpp"

void Group::Reset() {
  order = layer = 0;
  cancel_priority = 0;
  cancel_se.reset();
  InitSel();
}

void Group::InitSel() {
  cancel_priority = 0;
  cancel_se.reset();
  result = Result::None;
  result_button_no.reset();
  decided_button_no.reset();
  status = Status::Disabled;
  cancel_enabled = false;
  ClearTransientInteraction();
}

void Group::ClearTransientInteraction() {
  hit_button_no.reset();
  pushed_button_no.reset();
  pressed_button_no.reset();
}
