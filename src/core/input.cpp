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
#include <utility>

void InputListener::State::UpdateMouseButton(bool& pending, bool in_down) {
  if (down == in_down)
    return;

  down = in_down;
  if (down) {
    on_down = true;
    pending = true;
  } else {
    on_up = true;
    down_up |= pending;
    pending = false;
  }
}

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
            mouse_left.UpdateMouseButton(mouse_left_pending_, true);
          else if (event.button == MouseButton::RIGHT)
            mouse_right.UpdateMouseButton(mouse_right_pending_, true);
          else if (event.button == MouseButton::WHEELDOWN)
            ++mouse_wheel;
          else if (event.button == MouseButton::WHEELUP)
            --mouse_wheel;
        } else if constexpr (std::same_as<T, MouseUp>) {
          if (event.button == MouseButton::LEFT)
            mouse_left.UpdateMouseButton(mouse_left_pending_, false);
          else if (event.button == MouseButton::RIGHT)
            mouse_right.UpdateMouseButton(mouse_right_pending_, false);
        } else if constexpr (std::same_as<T, MouseMotion>) {
          mouse_pos = event.pos;
        }
      },
      *event);

  const bool decide_down = mouse_left.down || !decide_keys_down_.empty();
  if (decide.down != decide_down) {
    decide.down = decide_down;
    if (decide_down) {
      decide.on_down = true;
    } else {
      decide.on_up = true;
      decide.down_up = true;
    }
  }

  const bool cancel_down = mouse_right.down || !cancel_keys_down_.empty();
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
  decide.ResetEdges();
  cancel.ResetEdges();
  NextMouse();
}

void InputListener::ClearState() {
  decide.ResetEdges();
  cancel.ResetEdges();
  ClearMouse();
}

void InputListener::NextMouse() {
  mouse_left.ResetEdges();
  mouse_right.ResetEdges();
  mouse_wheel = 0;
}

void InputListener::ClearMouse() {
  NextMouse();
  mouse_left_pending_ = mouse_right_pending_ = false;
}

void InputListener::ReleaseAll() {
  decide_keys_down_.clear();
  cancel_keys_down_.clear();
  mouse_left = {};
  mouse_right = {};
  ClearMouse();
  decide = {};
  cancel = {};
}

// ------------------------------------------------------------------------------

void ScriptInput::OnEvent(std::shared_ptr<Event> event) {
  if (!event)
    return;

  std::visit(
      [&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::same_as<T, MouseMotion>) {
          physical_mouse_pos_ = value.pos;
          if (!mouse_events_.empty() &&
              std::holds_alternative<MouseMotion>(mouse_events_.back()))
            mouse_events_.back() = value;
          else
            mouse_events_.push_back(value);
        } else if constexpr (std::same_as<T, MouseDown> ||
                             std::same_as<T, MouseUp>) {
          constexpr bool down = std::same_as<T, MouseDown>;
          if (value.button == MouseButton::LEFT ||
              value.button == MouseButton::RIGHT) {
            bool& physical = value.button == MouseButton::LEFT
                                 ? physical_left_
                                 : physical_right_;
            if (physical != down) {
              physical = down;
              mouse_events_.push_back(value);
            }
          } else if constexpr (down) {
            if (value.button == MouseButton::WHEELUP ||
                value.button == MouseButton::WHEELDOWN)
              mouse_events_.push_back(value);
          }
        } else if constexpr (std::same_as<T, KeyDown> ||
                             std::same_as<T, KeyUp>) {
          if (!IsDecideKey(value.code) && !IsCancelKey(value.code))
            return;
          bool changed;
          if constexpr (std::same_as<T, KeyDown>)
            changed = physical_keys_.insert(value.code).second;
          else
            changed = physical_keys_.erase(value.code) != 0;
          if (changed)
            keyboard_events_.push_back(value);
        }
      },
      *event);
}

void ScriptInput::PublishMouse() {
  if (!std::exchange(mouse_ready_, false))
    return;

  while (!mouse_events_.empty()) {
    auto event = std::move(mouse_events_.front());
    mouse_events_.pop_front();
    if (auto* motion = std::get_if<MouseMotion>(&event)) {
      mouse_pos_ = motion->pos;
      continue;
    }
    const auto* press = std::get_if<MouseDown>(&event);
    const auto button = press ? press->button : std::get<MouseUp>(event).button;
    if (button == MouseButton::WHEELUP) {
      --wheel_;
    } else if (button == MouseButton::WHEELDOWN) {
      ++wheel_;
    } else {
      (button == MouseButton::LEFT ? left_ : right_).Set(press != nullptr);
      // Expose each transition with its pointer position. A fast click must
      // have an observable pressed snapshot before its release is delivered.
      return;
    }
  }
}

void ScriptInput::PublishKeyboard() {
  if (!std::exchange(keyboard_ready_, false) || keyboard_events_.empty())
    return;

  auto event = std::move(keyboard_events_.front());
  keyboard_events_.pop_front();
  if (auto* press = std::get_if<KeyDown>(&event))
    keys_[press->code].Set(true);
  else
    keys_[std::get<KeyUp>(event).code].Set(false);
}

InputListener::State ScriptInput::Get(Source source) {
  PublishMouse();
  PublishKeyboard();
  if (source == Source::Left)
    return left_.state;
  if (source == Source::Right)
    return right_.state;

  auto result = source == Source::Decide ? left_.state : right_.state;
  for (const auto& [code, button] : keys_) {
    if (!(source == Source::Decide ? IsDecideKey(code) : IsCancelKey(code)))
      continue;
    result.down |= button.state.down;
    result.on_down |= button.state.on_down;
    result.on_up |= button.state.on_up;
    result.down_up |= button.state.down_up;
  }
  return result;
}

Point ScriptInput::MousePosition() {
  PublishMouse();
  PublishKeyboard();
  return mouse_pos_;
}

int ScriptInput::MouseWheel() {
  PublishMouse();
  PublishKeyboard();
  return wheel_;
}

void ScriptInput::NextMouse() {
  left_.state.ResetEdges();
  right_.state.ResetEdges();
  wheel_ = 0;
  mouse_ready_ = mouse_advanced_ = true;
}

void ScriptInput::NextKeyboard() {
  for (auto& [code, button] : keys_)
    button.state.ResetEdges();
  keyboard_ready_ = keyboard_advanced_ = true;
}

void ScriptInput::Next() {
  NextMouse();
  NextKeyboard();
}

void ScriptInput::ClearMouse() {
  mouse_events_.clear();
  left_.Clear(physical_left_);
  right_.Clear(physical_right_);
  mouse_pos_ = physical_mouse_pos_;
  wheel_ = 0;
  mouse_ready_ = mouse_advanced_ = true;
}

void ScriptInput::Clear() {
  ClearMouse();
  keyboard_events_.clear();
  for (auto& [code, button] : keys_)
    button.Clear(physical_keys_.contains(code));
  for (auto code : physical_keys_)
    keys_[code].Clear(true);
  keyboard_ready_ = keyboard_advanced_ = true;
}

void ScriptInput::ReleaseAll() {
  physical_left_ = physical_right_ = false;
  physical_keys_.clear();
  Clear();
  mouse_advanced_ = keyboard_advanced_ = false;
}

void ScriptInput::AdvanceFrame() {
  if (!mouse_advanced_)
    NextMouse();
  if (!keyboard_advanced_)
    NextKeyboard();
  mouse_advanced_ = keyboard_advanced_ = false;
}
