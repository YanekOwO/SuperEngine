// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006 Elliot Glaysher
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

#include "core/object_internal/objdrawer.hpp"

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/matrix.hpp"

#include "core/localrect.hpp"
#include "core/object.hpp"
#include "core/rect.hpp"
#include "core/render_geometry.hpp"
#include "systems/sdl/glrenderer.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "utilities/graphics.hpp"

#include <algorithm>
#include <utility>

glm::mat4 BuildModelMatrix(const RenderGeometry& geometry, const Rect& dst) {
  glm::mat4 model(1.0f);
  model = glm::translate(model,
                         glm::vec3(geometry.pivot_x, geometry.pivot_y, 0.0f));
  model = glm::rotate(model, glm::radians(geometry.rotation_degrees),
                      glm::vec3(0.0f, 0.0f, 1.0f));
  model = glm::translate(model,
                         glm::vec3(-geometry.pivot_x, -geometry.pivot_y, 0.0f));
  model = glm::translate(model, glm::vec3(dst.x(), dst.y(), 0.0f));
  return model;
}

std::optional<RenderGeometry> ApplyClips(
    RenderGeometry geo,
    const GraphicsObject& go,
    const std::optional<ParentObjState>& parent) {
  auto& param = go.Param();
  if (parent && parent->clip) {
    Rect clip = parent->clip.value();
    const Point parent_start(parent->render_state.pos_x,
                             parent->render_state.pos_y);
    const Rect full_parent_clip =
        Rect(parent_start + clip.origin(), clip.size());
    if (!geo.ApplyDstClip(full_parent_clip))
      return std::nullopt;
  }

  if (param.has_own_clip_rect() && !geo.ApplySrcClip(param.own_clip_rect()))
    return std::nullopt;

  if (param.has_clip_rect() && !geo.ApplyDstClip(param.clip_rect()))
    return std::nullopt;

  return geo;
}

void RenderObjectWithMask(glRenderable src,
                          const RenderingConfig& config,
                          glDestination dst,
                          const std::optional<ObjectMask>& mask) {
  if (!mask || !mask->surface) {
    glRenderer().Render(std::move(src), config, std::move(dst));
    return;
  }

  for (SDLSurface::TextureRecord tile : mask->surface->GetTextureArray()) {
    RenderingConfig masked_config = config;
    masked_config.alpha_mask = AlphaMaskConfig{
        .texture = tile.gltexture,
        .screen_rect = Rect(mask->origin + Size(tile.x_, tile.y_),
                            Size(tile.w_, tile.h_))};
    glRenderer().Render(src, std::move(masked_config), dst);
  }
}

// -----------------------------------------------------------------------
// class GraphicsObjectData
GraphicsObjectData::GraphicsObjectData() = default;

GraphicsObjectData::~GraphicsObjectData() = default;

void GraphicsObjectData::Render(const GraphicsObject& go,
                                std::optional<ParentObjState> parent,
                                std::optional<ObjectMask> mask) {
  std::shared_ptr<const SDLSurface> surface = CurrentSurface(go);
  if (!surface)
    return;

  RenderGeometry geometry = BuildRenderGeometry(
      go, parent ? std::make_optional(parent->render_state) : std::nullopt);
  const float parent_alpha = parent ? parent->alpha : 1.f;
  const float alpha = GetRenderingAlpha(go, parent_alpha);

  if (auto geo = ApplyClips(geometry, go, parent))
    geometry = *geo;
  else
    return;

  auto& param = go.Param();
  for (SDLSurface::TextureRecord it : surface->GetTextureArray()) {
    auto src_rect = geometry.src, dst_rect = geometry.dst;
    LocalRect coordinate_system(it.x_, it.y_, it.w_, it.h_);
    if (!coordinate_system.intersectAndTransform(src_rect, dst_rect))
      continue;

    RenderingConfig config;
    config.alpha = alpha;
    config.model = BuildModelMatrix(geometry, dst_rect);
    config.blend_type = param.composite_mode;
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

Rect GraphicsObjectData::SrcRect(const GraphicsObject& go) const {
  return CurrentSurface(go)->GetPattern(go.Param().GetPattNo()).rect;
}

Point GraphicsObjectData::DstOrigin(const GraphicsObject& go) const {
  auto& param = go.Param();
  std::shared_ptr<const SDLSurface> surface = CurrentSurface(go);
  if (surface) {
    return Point(surface->GetPattern(param.GetPattNo()).originX,
                 surface->GetPattern(param.GetPattNo()).originY);
  }

  return Point();
}

Rect GraphicsObjectData::DstRect(const GraphicsObject& go,
                                 const GraphicsObject* parent) {
  if (parent == nullptr)
    return BuildRenderGeometry(go, std::nullopt).dst;
  else {
    ParentObjState state = ParentObjState ::BuildFrom(*parent);
    return BuildRenderGeometry(go, state.render_state).dst;
  }
}

bool GraphicsObjectData::HitTest(const GraphicsObject& go,
                                 const Point& point,
                                 std::optional<ParentObjState> parent) {
  if (!HitTestAtSource(go, Point(), false))
    return false;

  RenderGeometry geometry = BuildRenderGeometry(
      go, parent ? std::make_optional(parent->render_state) : std::nullopt);
  if (auto geo = ApplyClips(geometry, go, parent))
    geometry = *geo;
  else
    return false;

  float hit_x = static_cast<float>(point.x());
  float hit_y = static_cast<float>(point.y());
  if (geometry.rotation_degrees != 0.0f) {
    auto [rotated_x, rotated_y] =
        RotateAround(hit_x, hit_y, geometry.pivot_x, geometry.pivot_y,
                     -geometry.rotation_degrees);
    hit_x = rotated_x;
    hit_y = rotated_y;
  }

  if (hit_x < geometry.dst.x() || hit_x >= geometry.dst.x2() ||
      hit_y < geometry.dst.y() || hit_y >= geometry.dst.y2())
    return false;

  if (!go.Param().alpha_test)
    return true;

  if (geometry.dst.width() == 0 || geometry.dst.height() == 0)
    return false;

  const float source_x_ratio =
      (hit_x - geometry.dst.x()) / static_cast<float>(geometry.dst.width());
  const float source_y_ratio =
      (hit_y - geometry.dst.y()) / static_cast<float>(geometry.dst.height());
  int source_x = geometry.src.x() +
                 static_cast<int>(source_x_ratio *
                                  static_cast<float>(geometry.src.width()));
  int source_y = geometry.src.y() +
                 static_cast<int>(source_y_ratio *
                                  static_cast<float>(geometry.src.height()));
  source_x = std::clamp(source_x, geometry.src.x(), geometry.src.x2() - 1);
  source_y = std::clamp(source_y, geometry.src.y(), geometry.src.y2() - 1);

  return HitTestAtSource(go, Point(source_x, source_y), true);
}

bool GraphicsObjectData::HitTestAtSource(const GraphicsObject& go,
                                         const Point& source_point,
                                         bool alpha_test) const {
  std::shared_ptr<const SDLSurface> surface = CurrentSurface(go);
  if (!surface)
    return false;
  if (!alpha_test)
    return true;
  if (!surface->GetRect().Contains(source_point))
    return false;

  return surface->GetPixelAt(source_point).a() > 0;
}

Point GraphicsObjectData::DstPosition(const GraphicsObject& go) const {
  auto& param = go.Param();
  Point position(param.x() + param.GetXAdjustmentSum(),
                 param.y() + param.GetYAdjustmentSum());
  if (param.GetButtonUsingOverides())
    position += param.GetButtonOffsetOverride();

  return position;
}

RenderState GraphicsObjectData::BuildRenderState(
    const GraphicsObject& go) const {
  const auto& param = go.Param();
  const Point position = go.GetDrawer().DstPosition(go);
  return RenderState::Build(param, position);
}
RenderGeometry GraphicsObjectData::BuildRenderGeometry(
    const GraphicsObject& go,
    std::optional<RenderState> parent_state) const {
  RenderGeometry geometry;
  geometry.src = SrcRect(go);

  RenderState state = BuildRenderState(go);
  if (parent_state)
    state = RenderState::Fold(state, *parent_state);

  const Point texture_origin = DstOrigin(go);
  const float pivot_x = state.pos_x + state.center_rep_x;
  const float pivot_y = state.pos_y + state.center_rep_y;
  const float center_x =
      state.center_x + state.center_rep_x + texture_origin.x();
  const float center_y =
      state.center_y + state.center_rep_y + texture_origin.y();

  geometry.pivot_x = pivot_x;
  geometry.pivot_y = pivot_y;
  geometry.rotation_degrees = state.rotation_degrees;
  geometry.scale_x = state.scale_x;
  geometry.scale_y = state.scale_y;
  geometry.local_x = -center_x;
  geometry.local_y = -center_y;
  geometry.UpdateDstFromLocal();

  return geometry;
}

float GraphicsObjectData::GetRenderingAlpha(const GraphicsObject& go,
                                            float parent_alpha) const {
  const float alpha = go.Param().GetNormalizedAlpha();
  return alpha * parent_alpha;
}

void GraphicsObjectData::PlaySet(int set) {}

void GraphicsObjectData::Execute() {}
