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

#include "systems/text_waku_type4.hpp"

#include "systems/sdl/sdl_surface.hpp"
#include "systems/text_waku.hpp"
#include "systems/text_waku_normal.hpp"

// A listing of all the g00 regions in type 4 wakus. The ranges to the right
// are from the file smw01a.g00 in CLANNAD_FV.
enum WakuPart {
  TOP_LEFT_CORNER,      // <region x1="0" y1="0" x2="3" y2="3"/>
  TOP_CENTER,           // <region x1="4" y1="0" x2="19" y2="3"/>
  TOP_RIGHT_CORNER,     // <region x1="20" y1="0" x2="23" y2="3"/>
  LEFT_SIDE,            // <region x1="0" y1="4" x2="1" y2="5"/>
  UNKNOWN_1,            // <region x1="0" y1="4" x2="1" y2="5"/>
  UNKNOWN_2,            // <region x1="0" y1="4" x2="1" y2="5"/>
  RIGHT_SIDE,           // <region x1="22" y1="4" x2="23" y2="5"/>
  UNKNOWN_3,            // <region x1="22" y1="4" x2="23" y2="5"/>
  UNKNOWN_4,            // <region x1="22" y1="4" x2="23" y2="5"/>
  BOTTOM_LEFT_CORNER,   // <region x1="0" y1="20" x2="3" y2="23"/>
  BOTTOM_CENTER,        // <region x1="4" y1="20" x2="19" y2="23"/>
  BOTTOM_RIGHT_CORNER,  // <region x1="20" y1="20" x2="23" y2="23"/>
};

TextWakuType4::TextWakuType4()
    : TextWaku(true, true),
      area_top_(0),
      area_bottom_(0),
      area_left_(0),
      area_right_(0) {}

TextWakuType4::TextWakuType4(const TextWakuType4& source) : TextWaku(source) {
  area_top_ = source.area_top_;
  area_bottom_ = source.area_bottom_;
  area_left_ = source.area_left_;
  area_right_ = source.area_right_;
  waku_main_ = source.waku_main_;
  filter_colour_ = source.filter_colour_;
  use_config_colour_ = source.use_config_colour_;
  use_config_opacity_ = source.use_config_opacity_;
  filter_rendering_ = source.filter_rendering_;
  top_left_ = source.top_left_;
  top_center_ = source.top_center_;
  top_right_ = source.top_right_;
  left_side_ = source.left_side_;
  right_side_ = source.right_side_;
  bottom_left_ = source.bottom_left_;
  bottom_center_ = source.bottom_center_;
  bottom_right_ = source.bottom_right_;

  filter_surface_ =
      source.filter_surface_ ? source.filter_surface_->Clone() : nullptr;
  if (filter_surface_)
    filter_surface_->SetIsMask(true);
  if (source.overlay_)
    overlay_ = std::make_unique<TextWakuNormal>(*source.overlay_);
}

std::unique_ptr<TextWaku> TextWakuType4::Clone() const {
  return std::make_unique<TextWakuType4>(*this);
}

void TextWakuType4::ClearMouseCapture() {
  if (overlay_)
    overlay_->ClearMouseCapture();
}

void TextWakuType4::Execute() {
  if (overlay_)
    overlay_->Execute();

  TextWaku::Execute();
}

void TextWakuType4::Render(Point box_location,
                           Size content_size,
                           RGBAColour colour,
                           bool is_filter) {
  if (waku_main_) {
    // Calculate the location/area and render the filtered background.
    Point backing_point =
        box_location +
        Size(left_side_.rect.width(), top_center_.rect.height()) -
        Size(area_left_, area_top_);
    Size backing_size =
        content_size + Size(area_left_ + area_right_, area_top_ + area_bottom_);
    std::shared_ptr<SDLSurface> backing = filter_surface_;
    if (!backing)
      backing = GetWakuBackingOfSize(backing_size);
    const Rect destination(backing_point, backing_size);
    if (filter_rendering_ == MwndConfig::FilterRendering::AlphaBlend) {
      backing->RenderToScreenWithColourAddition(
          backing->GetRect(), destination,
          use_config_colour_ ? colour.rgb() : RGBColour(0, 0, 0),
          use_config_opacity_ ? colour.a() : 255);
    } else {
      RGBAColour filter_colour = filter_colour_;
      if (use_config_colour_)
        filter_colour = RGBAColour(colour.rgb(), filter_colour.a());
      if (use_config_opacity_)
        filter_colour.set_alpha(colour.a());
      backing->RenderToScreenAsColorMask(backing->GetRect(), destination,
                                         filter_colour, is_filter);
    }

    // Calculate the total size of the waku decoration. We need this to get the
    // size of the non-corners correct.
    Size total_size =
        Size(left_side_.rect.width(), top_center_.rect.height()) +
        content_size +
        Size(right_side_.rect.width(), bottom_center_.rect.height());

    // Top row
    waku_main_->RenderToScreen(top_left_.rect,
                               Rect(box_location, top_left_.rect.size()));

    const Point top_center_p = box_location + Size(top_left_.rect.width(), 0);
    int top_center_width =
        total_size.width() - top_left_.rect.width() - top_right_.rect.width();
    const Size top_center_s = Size(top_center_width, top_center_.rect.height());
    waku_main_->RenderToScreen(top_center_.rect,
                               Rect(top_center_p, top_center_s));

    const Point top_right_p = top_center_p + Size(top_center_width, 0);
    waku_main_->RenderToScreen(top_right_.rect,
                               Rect(top_right_p, top_right_.rect.size()));

    // Center row
    const Point left_side_p = box_location + Size(0, top_left_.rect.height());
    int left_side_height = content_size.height();
    const Size left_side_s = Size(left_side_.rect.width(), left_side_height);
    waku_main_->RenderToScreen(left_side_.rect, Rect(left_side_p, left_side_s));

    const Point right_side_p =
        box_location + Size(total_size.width() - right_side_.rect.width(),
                            top_right_.rect.height());
    int right_side_height = content_size.height();
    const Size right_side_s = Size(right_side_.rect.width(), right_side_height);
    waku_main_->RenderToScreen(right_side_.rect,
                               Rect(right_side_p, right_side_s));

    // Bottom row
    const Point bottom_left_p = left_side_p + Size(0, left_side_height);
    waku_main_->RenderToScreen(bottom_left_.rect,
                               Rect(bottom_left_p, bottom_left_.rect.size()));

    // Sometimes |top_center_width| != |bottom_center_width| (for example, when
    // the decorations have a larger rounded corner on the top).
    const Point bottom_center_p =
        bottom_left_p + Size(bottom_left_.rect.width(), 0);
    int bottom_center_width = total_size.width() - bottom_left_.rect.width() -
                              bottom_right_.rect.width();
    const Size bottom_center_s =
        Size(bottom_center_width, bottom_center_.rect.height());
    waku_main_->RenderToScreen(bottom_center_.rect,
                               Rect(bottom_center_p, bottom_center_s));

    const Point bottom_right_p = bottom_center_p + Size(bottom_center_width, 0);
    waku_main_->RenderToScreen(bottom_right_.rect,
                               Rect(bottom_right_p, bottom_right_.rect.size()));
  }
  if (overlay_)
    overlay_->Render(box_location, content_size, colour, is_filter);

  TextWaku::Render(box_location, content_size, colour, is_filter);
}

Size TextWakuType4::GetSize(const Size& text_surface) const {
  Size padding = Size(left_side_.rect.width() + right_side_.rect.width(),
                      top_center_.rect.height() + bottom_center_.rect.height());
  return text_surface + padding;
}

const std::shared_ptr<SDLSurface>& TextWakuType4::GetWakuBackingOfSize(
    Size size) {
  if (!cached_backing_ || cached_backing_->GetSize() != size) {
    cached_backing_ = std::make_shared<SDLSurface>(size);
    cached_backing_->Fill(filter_rendering_ ==
                                  MwndConfig::FilterRendering::AlphaBlend
                              ? filter_colour_
                              : RGBAColour::Black());
  }

  return cached_backing_;
}

void TextWakuType4::SetMainWaku(
    std::shared_ptr<const SDLSurface> waku_surface) {
  if (!waku_surface) {
    waku_main_ = nullptr;
    return;
  }

  waku_main_ = waku_surface;

  top_left_ = waku_main_->GetPattern(TOP_LEFT_CORNER);
  top_center_ = waku_main_->GetPattern(TOP_CENTER);
  top_right_ = waku_main_->GetPattern(TOP_RIGHT_CORNER);

  left_side_ = waku_main_->GetPattern(LEFT_SIDE);
  right_side_ = waku_main_->GetPattern(RIGHT_SIDE);

  bottom_left_ = waku_main_->GetPattern(BOTTOM_LEFT_CORNER);
  bottom_center_ = waku_main_->GetPattern(BOTTOM_CENTER);
  bottom_right_ = waku_main_->GetPattern(BOTTOM_RIGHT_CORNER);
}

void TextWakuType4::SetArea(int top, int bottom, int left, int right) {
  area_top_ = top;
  area_bottom_ = bottom;
  area_left_ = left;
  area_right_ = right;
}

void TextWakuType4::SetFilterSurface(
    std::shared_ptr<const SDLSurface> surface) {
  SetFilter(std::move(surface), filter_colour_, use_config_colour_,
            use_config_opacity_);
}

void TextWakuType4::SetFilter(std::shared_ptr<const SDLSurface> surface,
                              RGBAColour colour,
                              bool use_config_colour,
                              bool use_config_opacity) {
  filter_surface_ = surface ? surface->Clone() : nullptr;
  if (filter_surface_)
    filter_surface_->SetIsMask(true);
  if (filter_colour_ != colour)
    cached_backing_.reset();
  filter_colour_ = colour;
  use_config_colour_ = use_config_colour;
  use_config_opacity_ = use_config_opacity;
}

void TextWakuType4::SetOverlay(std::unique_ptr<TextWakuNormal> overlay) {
  overlay_ = std::move(overlay);
}

void TextWakuType4::SetMousePosition(const Point& pos) {
  if (overlay_)
    overlay_->SetMousePosition(pos);
}

void TextWakuType4::ClearMousePosition() {
  if (overlay_)
    overlay_->ClearMousePosition();
}

bool TextWakuType4::HasButtonAt(const Point& pos) const {
  return overlay_ && overlay_->HasButtonAt(pos);
}

bool TextWakuType4::HandleMouseClick(const Point& pos, bool pressed) {
  return overlay_ && overlay_->HandleMouseClick(pos, pressed);
}

void TextWakuType4::SetWaitIcon(bool page, const Point& position) {
  if (overlay_)
    overlay_->SetWaitIcon(page, position);
}

void TextWakuType4::HideWaitIcon() {
  if (overlay_)
    overlay_->HideWaitIcon();
}
