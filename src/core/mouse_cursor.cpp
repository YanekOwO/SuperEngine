// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2008 Elliot Glaysher
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

#include "core/mouse_cursor.hpp"

#include "systems/event_system.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/system.hpp"

const int CURSOR_SIZE_INT = 32;
const Size CURSOR_SIZE = Size(CURSOR_SIZE_INT, CURSOR_SIZE_INT);

const int HOTSPOTMASK_X_OFFSET = 8;
const int HOTSPOTMASK_Y_OFFSET = 48;

// -----------------------------------------------------------------------
// MouseCursor (public)
// -----------------------------------------------------------------------
MouseCursor::MouseCursor(
    std::shared_ptr<Clock> clock,
    const std::shared_ptr<const SDLSurface>& cursor_surface,
    int count,
    int speed)
    : cursor_surface_(cursor_surface),
      count_(count),
      frame_speed_(std::chrono::milliseconds(speed / count_)),
      current_frame_(0),
      last_time_frame_incremented_(clock->GetTime()),
      clock_(clock) {
  // TODO(erg): Technically, each frame might have a hotspot. In practice, the
  // hotspot is in the same place every frame.
  FindHotspot();

  RGBAColour pixel = cursor_surface->GetPixelAt(Point(0, 0));

  cursor_surface_ = cursor_surface->ClipAsColorMask(
      Rect(8, 8, Size(CURSOR_SIZE_INT * count_, CURSOR_SIZE_INT)), pixel.r(),
      pixel.g(), pixel.b());
}

MouseCursor::~MouseCursor() = default;

void MouseCursor::Execute() {
  Clock::timepoint_t cur_time = clock_->GetTime();

  if (last_time_frame_incremented_ + frame_speed_ < cur_time) {
    last_time_frame_incremented_ = cur_time;

    current_frame_++;
    if (current_frame_ >= count_)
      current_frame_ = 0;
  }
}

void MouseCursor::RenderHotspotAt(const Point& mouse_location) {
  Point render_point = GetTopLeftForHotspotAt(mouse_location);
  cursor_surface_->RenderToScreen(
      Rect(current_frame_ * CURSOR_SIZE_INT, 0, CURSOR_SIZE),
      Rect(render_point, CURSOR_SIZE));
}

// -----------------------------------------------------------------------
// MouseCursor (private)
// -----------------------------------------------------------------------

Point MouseCursor::GetTopLeftForHotspotAt(const Point& mouse_location) {
  return mouse_location - hotspot_offset_;
}

void MouseCursor::FindHotspot() {
  for (int x = HOTSPOTMASK_X_OFFSET; x < HOTSPOTMASK_X_OFFSET + CURSOR_SIZE_INT;
       ++x) {
    for (int y = HOTSPOTMASK_Y_OFFSET;
         y < HOTSPOTMASK_Y_OFFSET + CURSOR_SIZE_INT; ++y) {
      RGBAColour pixel = cursor_surface_->GetPixelAt(Point(x, y));

      if (pixel.r() == 255 && pixel.g() == 255 && pixel.b() == 255) {
        hotspot_offset_ =
            Size(x - HOTSPOTMASK_X_OFFSET, y - HOTSPOTMASK_Y_OFFSET);
        return;
      }
    }
  }
}
