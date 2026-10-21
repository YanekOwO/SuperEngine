// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2011 Elliot Glaysher
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

#include "core/object_internal/drawer/colour_filter.hpp"

#include "core/object.hpp"
#include "core/render_geometry.hpp"
#include "systems/sdl/gl_frame_buffer.hpp"
#include "systems/sdl/glrenderer.hpp"
#include "systems/sdl/gltexture.hpp"
#include "systems/sdl/sdl_surface.hpp"

ColourFilterObjectData::ColourFilterObjectData(const Rect& screen_rect)
    : screen_rect_(screen_rect) {}

ColourFilterObjectData::~ColourFilterObjectData() {}

void ColourFilterObjectData::Render(const GraphicsObject& go,
                                    std::optional<ParentObjState> parent,
                                    std::optional<ObjectMask> mask) {
  auto screen_canvas = SDLSurface::screen_;
  auto background = screen_canvas->GetTexture();

  RenderGeometry geometry = BuildRenderGeometry(
      go, parent ? std::make_optional(parent->render_state) : std::nullopt);
  const float parent_alpha = parent ? parent->alpha : 1.f;
  const float alpha = GetRenderingAlpha(go, parent_alpha);

  if (auto geo = ApplyClips(geometry, go, parent))
    geometry = *geo;
  else
    return;

  auto& param = go.Param();
  RenderingConfig cfg;
  cfg.alpha = alpha;
  cfg.model = BuildModelMatrix(geometry, geometry.dst);
  cfg.blend_type = param.composite_mode;
  cfg.color = param.colour();
  cfg.tint = param.tint();
  cfg.mono = param.mono() / 255.f;
  cfg.invert = param.invert() / 255.f;
  const float bright = param.GetNormalizedBright();
  const float dark = param.GetNormalizedDark();
  cfg.bright = parent ? parent->EffectiveBright(bright) : bright;
  cfg.dark = parent ? parent->EffectiveDark(dark) : dark;
  cfg.sample_texture_in_screen_space = true;

  const Rect framebuffer(Point(0, 0), background->GetSize());
  RenderObjectWithMask({background, framebuffer}, cfg,
                       {screen_canvas, geometry.dst}, mask);
}

int ColourFilterObjectData::PixelWidth(const GraphicsObject&) {
  return screen_rect_.width();
}

int ColourFilterObjectData::PixelHeight(const GraphicsObject&) {
  return screen_rect_.height();
}

std::unique_ptr<GraphicsObjectData> ColourFilterObjectData::Clone() const {
  return std::make_unique<ColourFilterObjectData>(screen_rect_);
}

std::shared_ptr<const SDLSurface> ColourFilterObjectData::CurrentSurface(
    const GraphicsObject&) const {
  return std::shared_ptr<const SDLSurface>();
}

Rect ColourFilterObjectData::SrcRect(const GraphicsObject&) const {
  return Rect(Point(0, 0), screen_rect_.size());
}

Point ColourFilterObjectData::DstPosition(const GraphicsObject& go) const {
  return GraphicsObjectData::DstPosition(go) + screen_rect_.origin();
}
