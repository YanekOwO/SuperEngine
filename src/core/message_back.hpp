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

#include <boost/circular_buffer.hpp>
#include <cstddef>
#include <string>
#include <vector>

class MessageBack {
 public:
  struct MsgbkEntry {
    std::string msg;
    std::string original_name;
    std::string display_name;
    std::string debug_msg;
    std::vector<int> koe_no_list;
    std::vector<int> chara_no_list;
    int scene_no = -1;
    int line_no = -1;
    bool pct_flag = false;
    std::string image_file;
    int image_x = 0;
  };

  MessageBack();

  void Reset();

  void AddMessage(std::string msg);
  void InsertImage(std::string file, int x = 0);
  void AddNamae(std::string name);
  void AddKoe(int koe_no, int chara_no);

  void GoNextMsg();

  void ReadyMsg();

  std::vector<MsgbkEntry> GetEntries() const;

 private:
  static constexpr std::size_t kMaxHistory = 256;
  boost::circular_buffer<MsgbkEntry> entries_;
  std::size_t current_msg_pos_ = 0;
  bool new_msg_ = true;
};
