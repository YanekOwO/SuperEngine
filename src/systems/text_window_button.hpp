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

#pragma once

#include "core/button_action_table.hpp"
#include "core/rect.hpp"
#include "utilities/clock.hpp"

#include <functional>
#include <memory>
#include <optional>

class SDLSurface;

class ButtonActionTable;

// Describes the state of a Waku button
enum class TextWindowButtonState : int {
  Unused = -1,
  Normal = 0,
  Highlighted = 1,
  Pressed = 2,
  Activated = 4,
  Disabled = 3
};

// The base class for text window buttons
class TextWindowButton {
 public:
  struct RenderInfo {
    std::shared_ptr<SDLSurface> surface;
    Rect source;
    Point destination;
    int alpha = 255;
    int bright = 0;
    int dark = 0;
  };

  explicit TextWindowButton(std::shared_ptr<Clock> clock,
                            bool should_use,
                            Rect button_rect);
  Rect GetRect() const;

  // Checks to see if this is a valid, used button
  bool IsValid() const;
  bool IsVisible() const { return visible_; }
  bool BlocksInput() const;
  bool IsCaptured() const { return captured_; }
  void ClearMouseCapture();
  bool HitTest(const Point& pos) const;

  void SetPresentation(bool visible,
                       bool enabled,
                       bool touch_enabled,
                       bool activated);
  void SetHovered(bool hovered);

  // Track the mouse position to see if we need to alter our state
  void SetMousePosition(const Point& pos);

  bool HandleMouseClick(const Point& pos, bool pressed);

  void SetSurface(std::shared_ptr<SDLSurface> surf, int base_pattern);
  void SetActionTableEntry(ButtonActionTable::Entry entry, int cut_no);

  RenderInfo Render() const;
  void SetUsePatternGeometry(bool value) { use_pattern_geometry_ = value; }
  void SetAlphaHitTest(bool value) { alpha_hit_test_ = value; }

  // Called by other execute() calls while the System object has its
  // turn to do any updating
  void Execute();

  // Called before Execute() call, handles periodical updates.
  std::function<void(TextWindowButton&)> on_update_;

  // Called when the button is pressed
  std::function<void()> on_pressed_;

  // Called when the pointer first enters the button.
  std::function<void()> on_hit_;

  // Called when the button is released
  std::function<void()> on_release_;

  // Called while a held button repeats. This is separate from the push edge
  // so push sounds are not replayed on every repeat interval.
  std::function<void()> on_repeat_;

  // When set, repeat the pressed callback periodically
  std::optional<std::chrono::milliseconds> time_between_invocations_;

  TextWindowButtonState state_;

 private:
  struct ResolvedPattern {
    int index;
    ButtonActionTable::State action;
  };

  std::optional<ResolvedPattern> ResolveCurrentPattern() const;
  void UpdateState();

  inline void ButtonPressed() {
    if (on_pressed_) {
      on_pressed_();
      last_invocation_ = clock_->GetTime();
    }
  }

  inline void ButtonReleased() {
    if (on_release_)
      on_release_();
    last_invocation_.reset();
  }

  std::shared_ptr<Clock> clock_;
  std::optional<Clock::timepoint_t> last_invocation_;

  Rect btn_rect_;
  bool registered_ = true;
  bool visible_ = true;
  bool enabled_ = true;
  bool touch_enabled_ = true;
  bool activated_ = false;
  bool hovered_ = false;
  bool captured_ = false;
  bool use_pattern_geometry_ = false;
  bool alpha_hit_test_ = false;

  std::shared_ptr<SDLSurface> button_surface_;
  int base_pattern_;
  std::optional<ButtonActionTable::Entry> action_entry_;
};
