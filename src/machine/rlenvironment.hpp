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

#pragma once

#include "core/frame_counter.hpp"
#include "utilities/stopwatch.hpp"

#include <boost/serialization/serialization.hpp>

#include <map>
#include <utility>

class Gameexe;

// Generic values
//
// "RealLive provides two generic settings to permit games using the standard
// system command menu to include custom options in it. The meaning of each
// generic flag is left up to the programmer. Valid values are 0 to 4."
struct Generic {
  int val1, val2;

 private:
  // boost serialization support
  friend class boost::serialization::access;
  void serialize(auto& ar, const unsigned int ver) { ar & val1 & val2; }
};

// -----------------------------------------------------------------------

class RLEnvironment {
 public:
  RLEnvironment() = default;

  void InitFrom(Gameexe&);

  Generic& GetGenerics();

  Stopwatch& GetTimer(int layer, int idx);

  FrameCounter* GetFrameCounter(int layer, int idx);
  void SetFrameCounter(int layer, int idx, FrameCounter counter);
  void ClearFrameCounter(int layer, int idx);

 private:
  Generic generic_;

  std::map<std::pair<int, int>, Stopwatch> rltimer_;

  std::map<std::pair<int, int>, FrameCounter> frame_counter_;

  friend class boost::serialization::access;
  void serialize(auto& ar, const unsigned int ver) { ar & generic_; }
};
