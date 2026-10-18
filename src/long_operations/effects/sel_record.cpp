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

#include "long_operations/effects/sel_record.hpp"

#include "core/gameexe.hpp"
#include "utilities/graphics.hpp"

#include <sstream>
#include <stdexcept>

namespace lop {

selRecord::selRecord() {}

selRecord::selRecord(std::vector<int> selEffect) {
  if (selEffect.size() != 16)
    throw std::invalid_argument(
        "SEL effects must contain exactly 16 parameters.");

  rect = Rect::REC(selEffect[0], selEffect[1], selEffect[2], selEffect[3]);
  point = Point(selEffect[4], selEffect[5]);
  duration = selEffect[6];
  dsp = selEffect[7];
  direction = selEffect[8];
  op[0] = selEffect[9];
  op[1] = selEffect[10];
  op[2] = selEffect[11];
  op[3] = selEffect[12];
  op[4] = selEffect[13];
  transparency = selEffect[14];
  lv = selEffect[15];
}

std::string selRecord::ToString() const {
  std::ostringstream oss;
  oss << '(' << rect.x() << ',' << rect.y() << ',' << rect.x2() << ','
      << rect.y2() << ')';
  oss << '(' << point.x() << ',' << point.y() << ')';
  oss << ' ' << duration << ' ' << dsp << ' ' << direction;
  for (int i = 0; i < 5; ++i)
    oss << ' ' << op[i];
  oss << ' ' << transparency << ' ' << lv;
  return oss.str();
}

selRecord GetSelRecord(Gameexe& gexe, int selNum) {
  selRecord result;
  std::vector<int> selEffect;

  if (gexe("SEL", selNum).Exists()) {
    selEffect = gexe("SEL", selNum).ToIntVec();
    auto rect =
        Rect::GRP(selEffect[0], selEffect[1], selEffect[2], selEffect[3]);
    result = selRecord(selEffect);
    result.rect = rect;
  } else if (gexe("SELR", selNum).Exists()) {
    selEffect = gexe("SELR", selNum).ToIntVec();
    result = selRecord(selEffect);
  } else {
    // Can't find the specified #SEL effect. See if there's a #SEL.000 effect:
    if (gexe("SEL", 0).Exists()) {
      selEffect = gexe("SEL", 0).ToIntVec();
      auto rect =
          Rect::GRP(selEffect[0], selEffect[1], selEffect[2], selEffect[3]);
      result = selRecord(selEffect);
      result.rect = rect;
    } else if (gexe("SELR", 0).Exists()) {
      selEffect = gexe("SELR", 0).ToIntVec();
      result = selRecord(selEffect);
    }
    // If all else fails, return a default SEL effect
    else {
      Size screen = GetScreenSize(gexe);
      result = selRecord({0, 0, screen.width(), screen.height(), 0, 0, 1000, 0,
                          0, 0, 0, 0, 0, 0, 255, 0});
    }
  }

  return result;
}

}  // namespace lop
