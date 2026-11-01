// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 RLVM contributors
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

#include "core/object_internal/object_layers.hpp"

#include "core/localrect.hpp"
#include "systems/sdl/glrenderer.hpp"
#include "systems/sdl/sdl_surface.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

static RGBAColour ComposePixel(RGBAColour dst, RGBAColour src) {
  if (src.a() == 0)
    return dst;
  if (src.a() == 255 || dst.a() == 0)
    return src;

  // Preserve Siglus's integer alpha and channel rounding.
  const int alpha = src.a() + dst.a() - src.a() * dst.a() / 255;
  const int dst_weight = (255 - src.a()) * dst.a();
  auto channel = [&](int d, int s) {
    return ((255 * src.a() * s + dst_weight * d) >> 8) / alpha;
  };
  return RGBAColour(channel(dst.r(), src.r()), channel(dst.g(), src.g()),
                    channel(dst.b(), src.b()), alpha);
}

static int Coordinate(std::int64_t value) {
  if (value < -std::numeric_limits<int>::max() ||
      value > std::numeric_limits<int>::max())
    throw std::out_of_range("Object layer coordinate overflow");
  return static_cast<int>(value);
}

static Rect LayerLocalRect(const ObjectLayer& layer) {
  const GrpRect& pattern = layer.surface->GetPattern(layer.cut_no);
  const auto x = static_cast<std::int64_t>(layer.offset.x()) - pattern.originX;
  const auto y = static_cast<std::int64_t>(layer.offset.y()) - pattern.originY;
  Coordinate(x + pattern.rect.width());
  Coordinate(y + pattern.rect.height());
  return Rect(Point(Coordinate(x), Coordinate(y)), pattern.rect.size());
}

static Rect ComputeLayerBounds(const std::vector<ObjectLayer>& layers) {
  bool first = true;
  std::int64_t left = 0, top = 0, right = 0, bottom = 0;
  for (const auto& layer : layers) {
    if (!layer.surface || layer.surface->GetNumPatterns() == 0)
      throw std::runtime_error("Object layer requires a picture surface");

    const Rect layer_rect = LayerLocalRect(layer);
    if (layer_rect.is_degenerate())
      continue;

    left =
        first ? layer_rect.x() : std::min(left, std::int64_t(layer_rect.x()));
    top = first ? layer_rect.y() : std::min(top, std::int64_t(layer_rect.y()));
    right = first ? layer_rect.x2()
                  : std::max(right, std::int64_t(layer_rect.x2()));
    bottom = first ? layer_rect.y2()
                   : std::max(bottom, std::int64_t(layer_rect.y2()));
    first = false;
  }

  return Rect::REC(Coordinate(left), Coordinate(top), Coordinate(right - left),
                   Coordinate(bottom - top));
}

static RenderGeometry BuildLayerGeometry(
    const GraphicsObjectData& data,
    const GraphicsObject& go,
    const ObjectLayer& layer,
    const GrpRect& pattern,
    std::optional<RenderState> parent_state) {
  RenderGeometry geometry;
  geometry.src = pattern.rect;

  RenderState state = data.BuildRenderState(go);
  if (parent_state)
    state = RenderState::Fold(state, *parent_state);

  const float pivot_x = state.pos_x + state.center_rep_x;
  const float pivot_y = state.pos_y + state.center_rep_y;
  const float center_x = state.center_x + state.center_rep_x + pattern.originX;
  const float center_y = state.center_y + state.center_rep_y + pattern.originY;

  geometry.pivot_x = pivot_x;
  geometry.pivot_y = pivot_y;
  geometry.rotation_degrees = state.rotation_degrees;
  geometry.scale_x = state.scale_x;
  geometry.scale_y = state.scale_y;
  geometry.local_x = static_cast<float>(layer.offset.x()) - center_x;
  geometry.local_y = static_cast<float>(layer.offset.y()) - center_y;
  geometry.UpdateDstFromLocal();

  return geometry;
}

void ObjectLayers::SetLayers(std::vector<ObjectLayer> layers) {
  Rect bounds = ComputeLayerBounds(layers);
  layers_ = std::move(layers);
  bounds_ = bounds;
}

void ObjectLayers::Render(const GraphicsObjectData& drawer,
                          const GraphicsObject& go,
                          std::optional<ParentObjState> parent,
                          std::optional<ObjectMask> mask) const {
  const float parent_alpha = parent ? parent->alpha : 1.f;
  const float alpha = drawer.GetRenderingAlpha(go, parent_alpha);
  const std::optional<RenderState> parent_state =
      parent ? std::make_optional(parent->render_state) : std::nullopt;
  const ObjectParameter& param = go.Param();

  for (const auto& layer : layers_) {
    const GrpRect& pattern = layer.surface->GetPattern(layer.cut_no);
    if (pattern.rect.is_degenerate())
      continue;
    RenderGeometry geometry =
        BuildLayerGeometry(drawer, go, layer, pattern, parent_state);

    if (auto geo = ApplyClips(geometry, go, parent))
      geometry = *geo;
    else
      continue;

    for (SDLSurface::TextureRecord it : layer.surface->GetTextureArray()) {
      auto src_rect = geometry.src, dst_rect = geometry.dst;
      LocalRect coordinate_system(it.x_, it.y_, it.w_, it.h_);
      if (!coordinate_system.intersectAndTransform(src_rect, dst_rect))
        continue;

      RenderingConfig config;
      config.alpha = alpha;
      config.model = BuildModelMatrix(geometry, dst_rect);
      config.blend_type =
          layer.blend_type == 0 ? param.composite_mode : layer.blend_type;
      config.color = param.colour();
      config.tint = param.tint();
      config.mono = param.mono() / 255.f;
      config.invert = param.invert() / 255.f;
      const float bright = param.GetNormalizedBright();
      const float dark = param.GetNormalizedDark();
      config.bright = parent ? parent->EffectiveBright(bright) : bright;
      config.dark = parent ? parent->EffectiveDark(dark) : dark;

      RenderObjectWithMask({it.gltexture, src_rect}, config,
                           {SDLSurface::screen_, dst_rect}, mask);
    }
  }
}

RGBAColour ObjectLayers::GetPixelColor(Point point) const {
  if (layers_.empty())
    return RGBAColour::Clear();

  auto cut = [](const ObjectLayer& layer) {
    return std::clamp(layer.cut_no, 0,
                      std::max(0, layer.surface->GetNumPatterns() - 1));
  };
  // The first cut fixes the canvas; its placement offset does not apply.
  const auto& base = layers_.front();
  const auto region = base.surface->GetPixelRegion(cut(base));
  if (!region || !region->Contains(point))
    return RGBAColour::Clear();

  auto color = base.surface->GetPixelColor(point, cut(base));
  for (std::size_t i = 1; i < layers_.size(); ++i) {
    const auto& layer = layers_[i];
    const auto x = static_cast<std::int64_t>(point.x()) - layer.offset.x();
    const auto y = static_cast<std::int64_t>(point.y()) - layer.offset.y();
    if (x < std::numeric_limits<int>::min() ||
        x > std::numeric_limits<int>::max() ||
        y < std::numeric_limits<int>::min() ||
        y > std::numeric_limits<int>::max())
      continue;
    color = ComposePixel(
        color,
        layer.surface->GetPixelColor(
            Point(static_cast<int>(x), static_cast<int>(y)), cut(layer)));
  }
  return color;
}

bool ObjectLayers::HitTest(Point source_point, bool alpha_test) const {
  if (!Rect(Point(), bounds_.size()).Contains(source_point))
    return false;
  if (!alpha_test)
    return true;

  const Point composite_point(bounds_.x() + source_point.x(),
                              bounds_.y() + source_point.y());
  for (const auto& layer : layers_) {
    const GrpRect& pattern = layer.surface->GetPattern(layer.cut_no);
    const Rect local_rect = LayerLocalRect(layer);
    if (!local_rect.Contains(composite_point))
      continue;

    const Point layer_point(
        pattern.rect.x() + (composite_point.x() - local_rect.x()),
        pattern.rect.y() + (composite_point.y() - local_rect.y()));
    if (pattern.rect.Contains(layer_point) &&
        layer.surface->GetRect().Contains(layer_point) &&
        layer.surface->GetPixelAt(layer_point).a() > 0) {
      return true;
    }
  }
  return false;
}
