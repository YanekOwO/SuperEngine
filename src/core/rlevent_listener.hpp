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

#include "core/event_listener.hpp"
#include "systems/event_system.hpp"
#include "utilities/clock.hpp"

// Keyboard and Mouse Input (Reallive style)
//
// RealLive applications poll for input, with all the problems that sort of
// event handling has. We therefore provide an interface for polling.
//
// Don't use it. This interface is provided for RealLive
// bytecode. EventListeners should be used within rlvm code, instead.
class RLEventListener : public EventListener {
 public:
  explicit RLEventListener(const EventSystem& event_system)
      : event_system_(event_system) {}

  // Overridden from EventListener
  void OnEvent(std::shared_ptr<Event> event) override {
    std::visit(
        [&](const auto& event) {
          using T = std::decay_t<decltype(event)>;

          if constexpr (std::same_as<T, KeyDown>) {
            switch (event.code) {
              case KeyCode::LSHIFT:
              case KeyCode::RSHIFT:
                shift_pressed_ = true;
                break;
              case KeyCode::LCTRL:
              case KeyCode::RCTRL:
                ctrl_pressed_ = true;
                break;
              default:
                break;
            }
          }

          if constexpr (std::same_as<T, KeyUp>) {
            switch (event.code) {
              case KeyCode::LSHIFT:
              case KeyCode::RSHIFT:
                shift_pressed_ = false;
                break;
              case KeyCode::LCTRL:
              case KeyCode::RCTRL:
                ctrl_pressed_ = false;
                break;
              default:
                break;
            }
          }

          if constexpr (std::same_as<T, MouseUp> ||
                        std::same_as<T, MouseDown>) {
            if (event_system_.mouse_inside_window()) {
              constexpr int press_code = std::same_as<T, MouseDown> ? 1 : 2;

              if (event.button == MouseButton::LEFT)
                button1_state_ = press_code;
              else if (event.button == MouseButton::RIGHT)
                button2_state_ = press_code;
            }
          }

          if constexpr (std::same_as<T, MouseMotion>) {
            last_mouse_move_time_ = clock_.GetTicks().count();
            mouse_pos_ = event.pos;
          }
        },
        *event);
  }

  // Returns whether shift is currently pressed.
  bool ShiftPressed() const noexcept { return shift_pressed_; }

  // Returns whether ctrl has been pressed since the last invocation of
  // ctrlPresesd().
  bool CtrlPressed() const noexcept { return ctrl_pressed_; }

  // Returns the current cursor hotspot.
  Point GetCursorPos() const noexcept { return mouse_pos_; }

  // Gets the location of the mouse cursor and the button states.
  //
  // The following values are used to indicate a button's status:
  // - 0 if unpressed
  // - 1 if being pressed
  // - 2 if pressed and released.
  void GetCursorPos(Point& position, int& button1, int& button2) {
    position = mouse_pos_;
    button1 = button1_state_;
    button2 = button2_state_;
  }

  // Resets the state of the mouse buttons.
  void FlushMouseClicks() noexcept {
    button1_state_ = 0;
    button2_state_ = 0;
  }

  // Returns the time in ticks of the last mouse movement.
  unsigned int TimeOfLastMouseMove() const noexcept {
    return last_mouse_move_time_;
  }

 private:
  const EventSystem& event_system_;
  Clock clock_;

  bool shift_pressed_ = false;
  bool ctrl_pressed_ = false;

  Point mouse_pos_;

  int button1_state_ = 0;
  int button2_state_ = 0;

  // The last time we received a mouse move notification.
  unsigned int last_mouse_move_time_ = 0;
};
