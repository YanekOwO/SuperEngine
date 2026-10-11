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

#include "systems/text_window_button.hpp"

#include "core/rect.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/text_window.hpp"

namespace {
ButtonState ToButtonState(TextWindowButtonState state) {
  switch (state) {
    case TextWindowButtonState::Highlighted:
      return ButtonState::Hit;
    case TextWindowButtonState::Pressed:
      return ButtonState::Push;
    case TextWindowButtonState::Activated:
      return ButtonState::Select;
    case TextWindowButtonState::Disabled:
    case TextWindowButtonState::Unused:
      return ButtonState::Disable;
    case TextWindowButtonState::Normal:
    default:
      return ButtonState::Normal;
  }
}
}  // namespace

// -----------------------------------------------------------------------
// TextWindowButton
// -----------------------------------------------------------------------

TextWindowButton::TextWindowButton(std::shared_ptr<Clock> clock,
                                   bool should_use,
                                   Rect button_rect)
    : state_(TextWindowButtonState::Normal),
      clock_(clock),
      btn_rect_(button_rect),
      registered_(should_use),
      button_surface_(nullptr),
      base_pattern_(0) {
  UpdateState();
}

bool TextWindowButton::IsValid() const {
  return visible_ && !GetRect().is_empty();
}

bool TextWindowButton::BlocksInput() const {
  return visible_ && registered_ && !GetRect().is_empty();
}

std::optional<TextWindowButton::ResolvedPattern>
TextWindowButton::ResolveCurrentPattern() const {
  if (!button_surface_)
    return std::nullopt;

  ButtonActionTable::State action;
  int pattern = base_pattern_ + static_cast<int>(state_);
  if (action_entry_) {
    action = action_entry_->GetState(ToButtonState(state_));
    pattern = base_pattern_ + action.pattern;
  }
  if (pattern < 0 || pattern >= button_surface_->GetNumPatterns())
    return std::nullopt;

  return ResolvedPattern{.index = pattern, .action = action};
}

Rect TextWindowButton::GetRect() const {
  if (!use_pattern_geometry_ || !button_surface_)
    return btn_rect_;

  int pattern = base_pattern_;
  if (action_entry_)
    pattern += action_entry_->normal.pattern;
  if (pattern < 0 || pattern >= button_surface_->GetNumPatterns())
    return btn_rect_;
  const GrpRect& source = button_surface_->GetPattern(pattern);
  return Rect(btn_rect_.origin() - Size(source.originX, source.originY),
              source.rect.size());
}

bool TextWindowButton::HitTest(const Point& pos) const {
  if (!alpha_hit_test_)
    return GetRect().Contains(pos);

  const auto pattern = ResolveCurrentPattern();
  if (!pattern)
    return false;

  const GrpRect& source = button_surface_->GetPattern(pattern->index);
  Rect destination = btn_rect_;
  if (use_pattern_geometry_) {
    destination =
        Rect(btn_rect_.origin() - Size(source.originX, source.originY),
             source.rect.size());
  }
  if (destination.is_degenerate() || source.rect.is_degenerate() ||
      !destination.Contains(pos)) {
    return false;
  }

  const int source_x =
      source.rect.x() +
      static_cast<int>(static_cast<long long>(pos.x() - destination.x()) *
                       source.rect.width() / destination.width());
  const int source_y =
      source.rect.y() +
      static_cast<int>(static_cast<long long>(pos.y() - destination.y()) *
                       source.rect.height() / destination.height());
  const Point source_point(source_x, source_y);
  if (!source.rect.Contains(source_point) ||
      !button_surface_->GetRect().Contains(source_point)) {
    return false;
  }

  return button_surface_->GetPixelAt(source_point).a() > 0;
}

void TextWindowButton::SetPresentation(bool visible,
                                       bool enabled,
                                       bool touch_enabled,
                                       bool activated) {
  visible_ = visible;
  enabled_ = enabled;
  touch_enabled_ = touch_enabled;
  activated_ = activated;
  if (!visible_) {
    captured_ = false;
    hovered_ = false;
    last_invocation_.reset();
  }
  UpdateState();
}

void TextWindowButton::SetHovered(bool hovered) {
  const bool changed = hovered_ != hovered;
  hovered_ = hovered;
  UpdateState();
  if (changed && hovered_ && enabled_ && touch_enabled_ && on_hit_)
    on_hit_();
}

void TextWindowButton::SetMousePosition(const Point& pos) {
  SetHovered(BlocksInput() && HitTest(pos));
}

void TextWindowButton::UpdateState() {
  if (!visible_) {
    state_ = TextWindowButtonState::Unused;
  } else if (!enabled_) {
    state_ = TextWindowButtonState::Disabled;
  } else if (!touch_enabled_) {
    state_ = TextWindowButtonState::Normal;
  } else if (captured_ && hovered_) {
    state_ = TextWindowButtonState::Pressed;
  } else if (hovered_) {
    state_ = TextWindowButtonState::Highlighted;
  } else if (activated_) {
    state_ = TextWindowButtonState::Activated;
  } else {
    state_ = TextWindowButtonState::Normal;
  }
}

bool TextWindowButton::HandleMouseClick(const Point& pos, bool pressed) {
  if (pressed) {
    if (!BlocksInput() || !HitTest(pos))
      return false;
    captured_ = true;
    hovered_ = true;
    UpdateState();
    if (enabled_ && touch_enabled_)
      ButtonPressed();
    return true;
  }

  if (!captured_)
    return false;

  const bool hit = HitTest(pos);
  const bool decide = enabled_ && touch_enabled_ && hovered_ && hit;
  captured_ = false;
  hovered_ = hit;
  if (decide)
    ButtonReleased();
  else
    last_invocation_.reset();
  UpdateState();
  return true;
}

void TextWindowButton::SetSurface(std::shared_ptr<SDLSurface> surf,
                                  int base_pattern) {
  button_surface_ = surf;
  base_pattern_ = base_pattern;
}

void TextWindowButton::SetActionTableEntry(ButtonActionTable::Entry entry,
                                           int cut_no) {
  action_entry_ = std::move(entry);
  base_pattern_ = cut_no;
}

TextWindowButton::RenderInfo TextWindowButton::Render() const {
  if (!IsValid() || !button_surface_)
    return {};
  const auto pattern = ResolveCurrentPattern();
  if (!pattern)
    return {};
  const GrpRect& src = button_surface_->GetPattern(pattern->index);
  Point destination = GetRect().origin() + pattern->action.rep_pos;
  if (use_pattern_geometry_) {
    destination = btn_rect_.origin() - Size(src.originX, src.originY) +
                  pattern->action.rep_pos;
  }
  return RenderInfo{.surface = button_surface_,
                    .source = src.rect,
                    .destination = destination,
                    .alpha = action_entry_ ? pattern->action.rep_tr : 255,
                    .bright = action_entry_ ? pattern->action.rep_bright : 0,
                    .dark = action_entry_ ? pattern->action.rep_dark : 0};
}

void TextWindowButton::Execute() {
  if (on_update_)
    on_update_(*this);

  UpdateState();

  if (!last_invocation_ || !time_between_invocations_)
    return;

  auto cur_time = clock_->GetTime();
  if (*last_invocation_ + *time_between_invocations_ <= cur_time)
    if (on_repeat_) {
      on_repeat_();
      last_invocation_ = cur_time;
    }
}
