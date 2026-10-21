// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2007 Elliot Glaysher
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

#include "systems/text_key_cursor.hpp"

#include "core/gameexe.hpp"
#include "systems/event_system.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"

#include <string>
#include <vector>

// -----------------------------------------------------------------------
// TextKeyCursor
// -----------------------------------------------------------------------
TextKeyCursor::TextKeyCursor(System& system, int in_curosr_number)
    : cursor_number_(in_curosr_number),
      current_frame_(0),
      last_time_frame_incremented_(system.event().GetTicks()),
      system_(system) {
  Gameexe& gexe = system.gameexe();
  GameexeInterpretObject cursor = gexe("CURSOR", in_curosr_number);

  if (auto name = cursor("NAME").Str()) {
    SetCursorImage(system, *name);
    SetCursorSize(cursor("SIZE").ToIntVec());
    SetCursorFrameCount(cursor("CONT").ToInt());
    SetCursorFrameSpeed(cursor("SPEED").ToInt());
  }
}

// -----------------------------------------------------------------------

void TextKeyCursor::Execute() {
  unsigned int cur_time = system_.event().GetTicks();

  if (cursor_image_ && last_time_frame_incremented_ + frame_speed_ < cur_time) {
    last_time_frame_incremented_ = cur_time;

    current_frame_++;
    if (current_frame_ >= frame_count_)
      current_frame_ = 0;
  }
}

// -----------------------------------------------------------------------

void TextKeyCursor::Render(TextWindow& text_window) {
  if (cursor_image_) {
    // Get the location to render from text_window
    Point keycur = text_window.KeycursorPosition(frame_size_);

    cursor_image_->RenderToScreen(
        Rect(Point(current_frame_ * frame_size_.width(), 0), frame_size_),
        Rect(keycur, frame_size_), 255);
  }
}

// -----------------------------------------------------------------------

void TextKeyCursor::SetCursorImage(System& system, const std::string& name) {
  if (!name.empty()) {
    cursor_image_ = system.text().LoadImage(name);
    cursor_image_file_ = name;
  } else {
    cursor_image_.reset();
    cursor_image_file_.clear();
  }
}

// -----------------------------------------------------------------------

void TextKeyCursor::SetCursorSize(const std::vector<int>& image_size) {
  frame_size_ = Size(image_size.at(0), image_size.at(1));
}

// -----------------------------------------------------------------------

void TextKeyCursor::SetCursorFrameCount(const int frame_count) {
  frame_count_ = frame_count;
}

// -----------------------------------------------------------------------

void TextKeyCursor::SetCursorFrameSpeed(const int speed) {
  if (frame_count_)
    frame_speed_ = speed / frame_count_;
  else
    frame_speed_ = speed;
}
