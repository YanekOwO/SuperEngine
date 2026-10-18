// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
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

#include "utilities/graphics.hpp"

#include "core/gameexe.hpp"
#include "core/rect.hpp"
#include "machine/rlmachine.hpp"
#include "systems/system.hpp"
#include "systems/system_error.hpp"

#include <cmath>
#include <iostream>
#include <numbers>
#include <sstream>
#include <vector>

std::vector<int> GetSELEffect(RLMachine& machine, int selNum) {
  Gameexe& gexe = machine.GetSystem().gameexe();
  std::vector<int> selEffect;

  if (gexe("SEL", selNum).Exists()) {
    selEffect = gexe("SEL", selNum).ToIntVec();
    grp_to_rec_coordinates(selEffect[0], selEffect[1], selEffect[2],
                           selEffect[3]);
  } else if (gexe("SELR", selNum).Exists()) {
    selEffect = gexe("SELR", selNum).ToIntVec();
  } else {
    // Can't find the specified #SEL effect. See if there's a #SEL.000 effect:
    if (gexe("SEL", 0).Exists()) {
      selEffect = gexe("SEL", 0).ToIntVec();
      grp_to_rec_coordinates(selEffect[0], selEffect[1], selEffect[2],
                             selEffect[3]);
    } else if (gexe("SELR", 0).Exists()) {
      selEffect = gexe("SELR", 0).ToIntVec();
    } else {
      // Crap! Couldn't fall back on the default one either, so instead return
      // a SEL vector that is a screenwide, short fade because we absolutely
      // can't fail here.
      Size screen = GetRealliveScreenSize(gexe);
      selEffect = {0,
                   0,
                   screen.width(),
                   screen.height(),
                   0,
                   0,
                   1000,
                   000,
                   0,
                   0,
                   0,
                   0,
                   0,
                   0,
                   255,
                   0};
    }
  }

  return selEffect;
}

void GetSELPointAndRect(RLMachine& machine,
                        int selNum,
                        Rect& rect,
                        Point& point) {
  std::vector<int> selEffect = GetSELEffect(machine, selNum);
  rect = Rect::REC(selEffect[0], selEffect[1], selEffect[2], selEffect[3]);
  point = Point(selEffect[4], selEffect[5]);
}

Size GetRealliveScreenSize(Gameexe& gameexe) {
  std::vector<int> graphicsMode = gameexe("SCREENSIZE_MOD").ToIntVec();
  if (graphicsMode.size()) {
    if (graphicsMode[0] == 0) {
      return Size(640, 480);
    } else if (graphicsMode[0] == 1) {
      return Size(800, 600);
    } else if (graphicsMode[0] == 999 && graphicsMode.size() >= 3) {
      return Size(graphicsMode[1], graphicsMode[2]);
    } else {
      std::ostringstream oss;
      oss << "Illegal #SCREENSIZE_MOD value: " << graphicsMode[0];
      throw SystemError(oss.str());
    }
  }

  std::ostringstream oss;
  oss << "Missing #SCREENSIZE_MOD key";
  throw SystemError(oss.str());
}

Size GetSiglusScreenSize(Gameexe& gameexe) {
  if (!gameexe.Exists("SCREEN_SIZE"))
    return Size(800, 600);

  const auto dimensions = gameexe("SCREEN_SIZE").IntVec();
  if (!dimensions || dimensions->size() != 2 || (*dimensions)[0] <= 0 ||
      (*dimensions)[1] <= 0)
    throw SystemError("Siglus SCREEN_SIZE must contain two positive integers");

  return Size((*dimensions)[0], (*dimensions)[1]);
}

void ClipDestination(const Rect& clip_rect, Rect& src, Rect& dest) {
  Rect intersection = clip_rect.Intersection(dest);
  if (intersection.size().is_empty()) {
    src = Rect();
    dest = Rect();
    return;
  }

  // TODO(erg): Doesn't deal with clipping the right side because we don't
  // really want an intersection here; we want it clipped to.

  if (src.size() == dest.size()) {
    Size top_left_offset = intersection.origin() - dest.origin();
    dest = intersection;
    src = Rect(src.origin() + top_left_offset, intersection.size());
  } else {
    std::cerr << "Doesn't deal with different sizes in ClipDestination!"
              << std::endl;
  }
}

float deg2rad(float degrees) {
  return degrees * std::numbers::pi_v<float> / 180.0f;
}

std::pair<float, float> RotateAround(float x,
                                     float y,
                                     float center_x,
                                     float center_y,
                                     float degrees) {
  const float radians = deg2rad(degrees);
  const float cosv = std::cos(radians);
  const float sinv = std::sin(radians);
  const float dx = x - center_x;
  const float dy = y - center_y;
  return {dx * cosv - dy * sinv + center_x, dx * sinv + dy * cosv + center_y};
}
