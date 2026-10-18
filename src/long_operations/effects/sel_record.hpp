// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
// Copyright (C) 2009 Elliot Glaysher
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

#include "core/rect.hpp"

#include <string>
#include <vector>

class Gameexe;

namespace lop {

struct selRecord {
  Rect rect;
  Point point;
  int duration;
  int dsp;
  int direction;
  int op[5];
  int transparency;
  int lv;

  selRecord();
  explicit selRecord(std::vector<int> selEffect);

  std::string ToString() const;
};

selRecord GetSelRecord(Gameexe& gexe, int selNum);

}  // namespace lop
