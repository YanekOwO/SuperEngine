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

#include "core/message_back.hpp"

MessageBack::MessageBack() : entries_(kMaxHistory) {}

std::vector<MessageBack::MsgbkEntry> MessageBack::GetEntries() const {
  return {entries_.cbegin(), entries_.cend()};
}

void MessageBack::Reset() {
  entries_.clear();
  current_msg_pos_ = 0;
  new_msg_ = true;
}

void MessageBack::AddNamae(std::string name) {
  if (name.empty())
    return;

  ReadyMsg();
  entries_[current_msg_pos_].original_name = name;
  entries_[current_msg_pos_].display_name = name;
}

void MessageBack::AddKoe(int koe_no, int chara_no) {
  ReadyMsg();
  entries_[current_msg_pos_].koe_no_list.push_back(koe_no);
  entries_[current_msg_pos_].chara_no_list.push_back(chara_no);
}

void MessageBack::GoNextMsg() {
  if (new_msg_)
    return;
  if (entries_.empty())
    return;

  const MsgbkEntry& entry = entries_[current_msg_pos_];
  if (!entry.pct_flag && entry.msg.empty())
    return;

  new_msg_ = true;
}

void MessageBack::AddMessage(std::string msg) {
  if (msg.empty())
    return;

  ReadyMsg();
  MsgbkEntry& entry = entries_[current_msg_pos_];
  entry.msg += msg;
  entry.debug_msg = msg;
}

void MessageBack::InsertImage(std::string file, int x) {
  GoNextMsg();
  ReadyMsg();
  auto& entry = entries_[current_msg_pos_];
  entry.pct_flag = true;
  entry.image_file = std::move(file);
  entry.image_x = x;
  GoNextMsg();
}

void MessageBack::ReadyMsg() {
  if (!new_msg_)
    return;

  new_msg_ = false;
  if (entries_.full())
    entries_.pop_front();

  entries_.push_back(MsgbkEntry{});
  current_msg_pos_ = entries_.size() - 1;
}
