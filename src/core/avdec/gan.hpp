// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2007 Elliot Glaysher
// Copyright (C) 2025 Serina Sakurai
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
//
// -----------------------------------------------------------------------

#pragma once

#include "utilities/serialization.hpp"

#include <string>
#include <vector>

class GanDecoder {
 public:
  struct Frame {
    int pattern;
    int x;
    int y;
    int time;
    int alpha;
    int other;  // No idea what this is.
   private:
    friend class boost::serialization::access;
    void serialize(auto& ar, unsigned int) {
      ar & pattern & x & y & time & alpha & other;
    }
  };

  // Constructor
  explicit GanDecoder(std::vector<char> gan_data);

  // Public data
  std::vector<std::vector<Frame>> animation_sets;
  std::string raw_file_name;

 private:
  void ThrowBadFormat(const std::string& msg);
  Frame ReadSetFrame(const char*& data);
};
