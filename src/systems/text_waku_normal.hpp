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

#pragma once

#include "core/mwnd_config.hpp"
#include "core/rect.hpp"
#include "systems/text_waku.hpp"
#include "utilities/clock.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

class Gameexe;
class Point;
class Rect;
class RLMachine;
class Size;
class SDLSurface;
class System;
class TextWindowButton;

// Container class that owns all text window decorations.
//
// Window decorations are defined with \#WAKU.<setno>.<no>. Gameexe.ini keys.
class TextWakuNormal : public TextWaku {
 public:
  TextWakuNormal();
  ~TextWakuNormal() override;

  void AddButton(std::string btn_name,
                 std::unique_ptr<TextWindowButton> btn_impl);

  virtual void Execute() override;
  virtual void Render(Point box_location,
                      Size namebox_size,
                      RGBAColour colour,
                      bool is_filter) override;
  virtual Size GetSize(const Size& text_surface) const override;

  // TODO(erg): These two methods shouldn't really exist; I need to redo
  // plumbing of events so that these aren't routed through TextWindow, but are
  // instead some sort of listener. I'm currently thinking that the individual
  // buttons that need to handle events should be listeners.
  virtual void SetMousePosition(const Point& pos) override;
  void ClearMousePosition() override;
  bool HasButtonAt(const Point& pos) const override;
  virtual bool HandleMouseClick(const Point& pos, bool pressed) override;

  void SetWakuMain(std::shared_ptr<const SDLSurface> surface);
  void SetWakuBacking(std::shared_ptr<const SDLSurface> surface);
  void SetMainSurface(std::shared_ptr<const SDLSurface> surface) override {
    SetWakuMain(std::move(surface));
  }
  void SetFilterSurface(std::shared_ptr<const SDLSurface> surface) override {
    SetWakuBacking(std::move(surface));
  }
  void SetFilterConfig(Rect margin,
                       RGBAColour colour,
                       bool use_config_colour,
                       bool use_config_opacity);
  void SetWaitIcons(std::shared_ptr<SDLSurface> key_surface,
                    MwndConfig::Icon key_icon,
                    std::shared_ptr<SDLSurface> page_surface,
                    MwndConfig::Icon page_icon,
                    int position_type,
                    int position_base,
                    Point position,
                    std::shared_ptr<Clock> clock);
  void SetWaitIcon(bool page, const Point& position) override;
  void HideWaitIcon() override;
  void SetRenderFilter(bool render) { render_filter_ = render; }
  void SetFilterRendering(MwndConfig::FilterRendering rendering) {
    filter_rendering_ = rendering;
    generated_backing_.reset();
  }

 private:
  std::shared_ptr<const SDLSurface> main_surface_;
  std::shared_ptr<SDLSurface> backing_surface_;
  std::shared_ptr<SDLSurface> generated_backing_;
  Rect filter_margin_;
  RGBAColour filter_colour_ = RGBAColour(0, 0, 0, 128);
  bool use_config_colour_ = false;
  bool use_config_opacity_ = false;
  bool render_filter_ = true;
  MwndConfig::FilterRendering filter_rendering_ =
      MwndConfig::FilterRendering::ColorMask;
  std::shared_ptr<SDLSurface> key_icon_surface_, page_icon_surface_;
  MwndConfig::Icon key_icon_, page_icon_;
  int icon_position_type_ = 0;
  int icon_position_base_ = 0;
  Point icon_position_;
  Point dynamic_icon_position_;
  std::shared_ptr<Clock> clock_;
  Clock::duration_t icon_start_{};
  bool icon_visible_ = false;
  bool page_icon_visible_ = false;

  struct WakuButton {
    std::string name;
    std::unique_ptr<TextWindowButton> btn;
  };
  std::vector<WakuButton> buttons_;
  std::optional<std::size_t> captured_button_;
};
