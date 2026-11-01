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

#include <deque>
#include <map>
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
    inline void ResetEdges() { on_down = on_up = down_up = false; }
    void UpdateMouseButton(bool& pending, bool down);
  };
  Point mouse_pos;
  State mouse_left;
  State mouse_right;
  int mouse_wheel = 0;
  State decide;
  State cancel;

  void ResetState();
  void ClearState();
  // Advance mouse transients while retaining held buttons and pending clicks.
  void NextMouse();
  // Also cancel pending clicks; held buttons and position are preserved.
  void ClearMouse();
  void ReleaseAll();

 private:
  bool mouse_left_pending_ = false;
  bool mouse_right_pending_ = false;
  std::set<KeyCode> decide_keys_down_;
  std::set<KeyCode> cancel_keys_down_;
};

// Script input advances at logical script boundaries, independently of live
// input used by native controls. Reads share a stable snapshot until advanced.
class ScriptInput final : public EventListener {
 public:
  enum class Source { Decide, Cancel, Left, Right };

  void OnEvent(std::shared_ptr<Event> event) override;
  InputListener::State Get(Source source);
  Point MousePosition();
  int MouseWheel();

  void Next();
  void NextMouse();
  void Clear();
  void ClearMouse();
  void ReleaseAll();
  // An explicit next already advances its components for this logical frame.
  void AdvanceFrame();

 private:
  struct Button {
    InputListener::State state;
    bool pending = false;
    void Set(bool down) { state.UpdateMouseButton(pending, down); }
    void Clear(bool down) {
      state = {.down = down};
      pending = false;
    }
  };

  void PublishMouse();
  void PublishKeyboard();
  void NextKeyboard();

  std::deque<Event> mouse_events_, keyboard_events_;
  Button left_, right_;
  std::map<KeyCode, Button> keys_;
  Point mouse_pos_, physical_mouse_pos_;
  int wheel_ = 0;
  bool physical_left_ = false, physical_right_ = false;
  std::set<KeyCode> physical_keys_;
  bool mouse_ready_ = true, keyboard_ready_ = true;
  bool mouse_advanced_ = false, keyboard_advanced_ = false;
};
