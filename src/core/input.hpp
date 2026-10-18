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

#include "core/event.hpp"
#include "core/event_listener.hpp"
#include "core/rect.hpp"

#include <set>

inline bool IsDecideKey(KeyCode code) {
  return code == KeyCode::RETURN || code == KeyCode::SPACE ||
         code == KeyCode::KP_ENTER;
}

inline bool IsCancelKey(KeyCode code) { return code == KeyCode::ESCAPE; }

struct InputListener : public EventListener {
  virtual void OnEvent(std::shared_ptr<Event> event) final;

  struct State {
    bool down = false;
    bool on_down = false;
    bool on_up = false;
    bool down_up = false;
  };
  Point mouse_pos;
  bool left_mouse_down = false;
  bool right_mouse_down = false;
  State decide;
  State cancel;

  void ResetState();
  void ReleaseAll();

 private:
  std::set<KeyCode> decide_keys_down_;
  std::set<KeyCode> cancel_keys_down_;
};
