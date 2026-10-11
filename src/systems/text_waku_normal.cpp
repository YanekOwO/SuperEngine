// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2009 Elliot Glaysher
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

#include "systems/text_waku_normal.hpp"

#include "core/rect.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/text_waku.hpp"
#include "systems/text_window_button.hpp"

#include <algorithm>
#include <memory>
#include <string>

using namespace std::chrono_literals;

// -----------------------------------------------------------------------
// TextWakuNormal
// -----------------------------------------------------------------------
TextWakuNormal::TextWakuNormal() : TextWaku(true, false) {}
TextWakuNormal::~TextWakuNormal() = default;

void TextWakuNormal::AddButton(std::string btn_name,
                               std::unique_ptr<TextWindowButton> btn_impl) {
  if (!btn_impl)
    return;
  buttons_.emplace_back(std::move(btn_name), std::move(btn_impl));
}

void TextWakuNormal::Execute() {
  for (auto& btn : buttons_)
    btn.btn->Execute();
}

void TextWakuNormal::Render(Point box_location,
                            Size namebox_size,
                            RGBAColour colour,
                            bool is_filter) {
  RGBAColour filter_colour = filter_colour_;
  if (use_config_colour_) {
    filter_colour.set_red(colour.r());
    filter_colour.set_green(colour.g());
    filter_colour.set_blue(colour.b());
  }
  if (use_config_opacity_)
    filter_colour.set_alpha(colour.a());

  std::shared_ptr<SDLSurface> backing =
      render_filter_ ? backing_surface_ : nullptr;
  Size backing_size;
  if (backing) {
    backing_size = backing->GetSize();
  } else if (render_filter_) {
    backing_size =
        namebox_size - Size(filter_margin_.x() + filter_margin_.x2(),
                            filter_margin_.y() + filter_margin_.y2());
    if (backing_size.width() > 0 && backing_size.height() > 0 &&
        (!generated_backing_ ||
         generated_backing_->GetSize() != backing_size)) {
      generated_backing_ = std::make_shared<SDLSurface>(backing_size);
      generated_backing_->Fill(RGBAColour::Black());
    }
    backing = generated_backing_;
  }
  if (render_filter_ && backing && !backing_size.is_empty()) {
    const Point filter_position =
        box_location + Size(filter_margin_.x(), filter_margin_.y());
    backing->RenderToScreenAsColorMask(Rect(Point(0, 0), backing_size),
                                       Rect(filter_position, backing_size),
                                       filter_colour, is_filter);
  }

  if (main_surface_) {
    Size main_size = main_surface_->GetSize();
    main_surface_->RenderToScreen(Rect(Point(0, 0), main_size),
                                  Rect(box_location, main_size), 255);
  }

  for (auto& btn : buttons_) {
    auto render = btn.btn->Render();
    if (!render.surface || render.source.is_empty())
      continue;
    Rect dst(render.destination, render.source.size());
    render.surface->RenderToScreen(render.source, dst, render.alpha,
                                   render.bright, render.dark);
  }

  if (icon_visible_ && clock_) {
    const MwndConfig::Icon& icon = page_icon_visible_ ? page_icon_ : key_icon_;
    const std::shared_ptr<SDLSurface>& surface =
        page_icon_visible_ ? page_icon_surface_ : key_icon_surface_;
    if (surface && icon.pattern_count > 0 && icon.speed_ms > 0) {
      const auto elapsed = (clock_->GetTicks() - icon_start_).count();
      const int count = std::min(icon.pattern_count, surface->GetNumPatterns());
      if (count > 0) {
        const int pattern = static_cast<int>(elapsed / icon.speed_ms) % count;
        const Rect source = surface->GetPattern(pattern).rect;
        Point destination = dynamic_icon_position_;
        if (icon_position_type_ == 0) {
          const Rect bounds(box_location, GetSize(namebox_size));
          switch (icon_position_base_) {
            case 1:
              destination =
                  Point(bounds.x2() - icon_position_.x() - source.width(),
                        bounds.y() + icon_position_.y());
              break;
            case 2:
              destination =
                  Point(bounds.x() + icon_position_.x(),
                        bounds.y2() - icon_position_.y() - source.height());
              break;
            case 3:
              destination =
                  Point(bounds.x2() - icon_position_.x() - source.width(),
                        bounds.y2() - icon_position_.y() - source.height());
              break;
            default:
              destination = bounds.origin() + Size(icon_position_);
              break;
          }
        }
        surface->RenderToScreen(source, Rect(destination, source.size()));
      }
    }
  }
}

Size TextWakuNormal::GetSize(const Size& text_surface) const {
  if (main_surface_)
    return main_surface_->GetSize();
  else if (backing_surface_)
    return backing_surface_->GetSize();
  else
    return text_surface;
}

void TextWakuNormal::SetMousePosition(const Point& pos) {
  if (captured_button_ && *captured_button_ < buttons_.size()) {
    for (std::size_t i = 0; i < buttons_.size(); ++i)
      buttons_[i].btn->SetHovered(i == *captured_button_ &&
                                  buttons_[i].btn->HitTest(pos));
    return;
  }

  std::optional<std::size_t> hovered;
  for (std::size_t i = buttons_.size(); i-- > 0;) {
    if (buttons_[i].btn->BlocksInput() && buttons_[i].btn->HitTest(pos)) {
      hovered = i;
      break;
    }
  }
  for (std::size_t i = 0; i < buttons_.size(); ++i)
    buttons_[i].btn->SetHovered(hovered && *hovered == i);
}

void TextWakuNormal::ClearMousePosition() {
  if (captured_button_)
    return;
  for (auto& button : buttons_)
    button.btn->SetHovered(false);
}

bool TextWakuNormal::HasButtonAt(const Point& pos) const {
  return std::any_of(buttons_.begin(), buttons_.end(), [&](const auto& button) {
    return button.btn->BlocksInput() && button.btn->HitTest(pos);
  });
}

bool TextWakuNormal::HandleMouseClick(const Point& pos, bool pressed) {
  if (pressed && captured_button_)
    return true;

  if (!pressed && captured_button_) {
    const std::size_t index = *captured_button_;
    captured_button_.reset();
    if (index < buttons_.size()) {
      const bool handled = buttons_[index].btn->HandleMouseClick(pos, false);
      SetMousePosition(pos);
      return handled;
    }
    return true;
  }

  if (pressed) {
    for (std::size_t i = buttons_.size(); i-- > 0;) {
      auto& button = buttons_[i].btn;
      if (button->BlocksInput() && button->HitTest(pos)) {
        captured_button_ = i;
        for (std::size_t j = 0; j < buttons_.size(); ++j)
          buttons_[j].btn->SetHovered(j == i);
        return button->HandleMouseClick(pos, true);
      }
    }
    return false;
  }

  // A release with no capture never decides an action. It is nevertheless
  // consumed by a registered button, matching Siglus' button manager.
  for (std::size_t i = buttons_.size(); i-- > 0;)
    if (buttons_[i].btn->BlocksInput() && buttons_[i].btn->HitTest(pos))
      return true;
  return false;
}

void TextWakuNormal::SetWakuMain(std::shared_ptr<const SDLSurface> surface) {
  main_surface_ = surface;
}

void TextWakuNormal::SetWakuBacking(std::shared_ptr<const SDLSurface> surface) {
  if (!surface) {
    backing_surface_ = nullptr;
    return;
  }

  backing_surface_ = surface->Clone();
  backing_surface_->SetIsMask(true);
}

void TextWakuNormal::SetFilterConfig(Rect margin,
                                     RGBAColour colour,
                                     bool use_config_colour,
                                     bool use_config_opacity) {
  filter_margin_ = margin;
  filter_colour_ = colour;
  use_config_colour_ = use_config_colour;
  use_config_opacity_ = use_config_opacity;
}

void TextWakuNormal::SetWaitIcons(std::shared_ptr<SDLSurface> key_surface,
                                  MwndConfig::Icon key_icon,
                                  std::shared_ptr<SDLSurface> page_surface,
                                  MwndConfig::Icon page_icon,
                                  int position_type,
                                  int position_base,
                                  Point position,
                                  std::shared_ptr<Clock> clock) {
  key_icon_surface_ = std::move(key_surface);
  key_icon_ = std::move(key_icon);
  page_icon_surface_ = std::move(page_surface);
  page_icon_ = std::move(page_icon);
  icon_position_type_ = position_type;
  icon_position_base_ = position_base;
  icon_position_ = position;
  clock_ = std::move(clock);
}

void TextWakuNormal::SetWaitIcon(bool page, const Point& position) {
  page_icon_visible_ = page;
  dynamic_icon_position_ = position;
  icon_visible_ = true;
  if (clock_)
    icon_start_ = clock_->GetTicks();
}

void TextWakuNormal::HideWaitIcon() { icon_visible_ = false; }
