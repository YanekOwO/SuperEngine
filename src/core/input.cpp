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

#include "core/input.hpp"

#include <concepts>
#include <type_traits>

void InputListener::OnEvent(std::shared_ptr<Event> event) {
  if (!event)
    return;

  std::visit(
      [&](const auto& event) {
        using T = std::decay_t<decltype(event)>;

        if constexpr (std::same_as<T, KeyDown>) {
          if (IsDecideKey(event.code))
            decide_keys_down_.insert(event.code);
          if (IsCancelKey(event.code))
            cancel_keys_down_.insert(event.code);
        } else if constexpr (std::same_as<T, KeyUp>) {
          if (IsDecideKey(event.code))
            decide_keys_down_.erase(event.code);
          if (IsCancelKey(event.code))
            cancel_keys_down_.erase(event.code);
        } else if constexpr (std::same_as<T, MouseDown>) {
          if (event.button == MouseButton::LEFT)
            left_mouse_down = true;
          else if (event.button == MouseButton::RIGHT)
            right_mouse_down = true;
        } else if constexpr (std::same_as<T, MouseUp>) {
          if (event.button == MouseButton::LEFT)
            left_mouse_down = false;
          else if (event.button == MouseButton::RIGHT)
            right_mouse_down = false;
        } else if constexpr (std::same_as<T, MouseMotion>) {
          mouse_pos = event.pos;
        }
      },
      *event);

  const bool decide_down = left_mouse_down || !decide_keys_down_.empty();
  if (decide.down != decide_down) {
    decide.down = decide_down;
    if (decide_down) {
      decide.on_down = true;
    } else {
      decide.on_up = true;
      decide.down_up = true;
    }
  }

  const bool cancel_down = right_mouse_down || !cancel_keys_down_.empty();
  if (cancel.down != cancel_down) {
    cancel.down = cancel_down;
    if (cancel_down) {
      cancel.on_down = true;
    } else {
      cancel.on_up = true;
      cancel.down_up = true;
    }
  }
}

void InputListener::ResetState() {
  decide.on_down = false;
  decide.on_up = false;
  decide.down_up = false;
  cancel.on_down = false;
  cancel.on_up = false;
  cancel.down_up = false;
}

void InputListener::ReleaseAll() {
  decide_keys_down_.clear();
  cancel_keys_down_.clear();
  left_mouse_down = right_mouse_down = false;
  decide = {};
  cancel = {};
}
