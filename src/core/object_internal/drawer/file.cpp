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

#include "core/object_internal/drawer/file.hpp"

#include "core/localrect.hpp"
#include "core/object.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "systems/sdl/glrenderer.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "utilities/clock.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

Rect LayerLocalRect(const CompositeGraphicsObjectLayer& layer) {
  const GrpRect& pattern = layer.surface->GetPattern(layer.cut_no);
  const Point origin(layer.offset.x() - pattern.originX,
                     layer.offset.y() - pattern.originY);
  return Rect(origin, pattern.rect.size());
}

Rect ComputeCompositeBounds(
    const std::vector<CompositeGraphicsObjectLayer>& layers) {
  if (layers.empty())
    throw std::runtime_error("Composite object requires at least one layer");

  bool first = true;
  Rect bounds;
  for (const auto& layer : layers) {
    if (!layer.surface)
      throw std::runtime_error("Composite object layer has no surface");

    const Rect layer_rect = LayerLocalRect(layer);
    if (layer_rect.is_degenerate())
      continue;

    bounds = first ? layer_rect : bounds.Union(layer_rect);
    first = false;
  }

  if (first)
    throw std::runtime_error("Composite object layers are empty");

  return bounds;
}

glm::mat4 BuildCompositeModelMatrix(const RenderGeometry& geometry,
                                    const Rect& dst) {
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

RenderGeometry BuildLayerGeometry(const GraphicsObjectData& data,
                                  const GraphicsObject& go,
                                  const CompositeGraphicsObjectLayer& layer,
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

}  // namespace

// -----------------------------------------------------------------------
// class GraphicsObjectOfFile
GraphicsObjectOfFile::GraphicsObjectOfFile(std::shared_ptr<SDLSurface> surface)
    : animator_(std::make_shared<Clock>()),
      surface_(surface),
      frame_time_(0),
      current_frame_(-1) {}

GraphicsObjectOfFile::~GraphicsObjectOfFile() = default;

int GraphicsObjectOfFile::PixelWidth(const GraphicsObject& rp) {
  auto& param = rp.Param();

  const GrpRect& rect = surface_->GetPattern(param.GetPattNo());
  int width = rect.rect.width();
  return int(param.GetWidthScaleFactor() * width);
}

int GraphicsObjectOfFile::PixelHeight(const GraphicsObject& rp) {
  auto& param = rp.Param();

  const GrpRect& rect = surface_->GetPattern(param.GetPattNo());
  int height = rect.rect.height();
  return int(param.GetHeightScaleFactor() * height);
}

std::unique_ptr<GraphicsObjectData> GraphicsObjectOfFile::Clone() const {
  return std::make_unique<GraphicsObjectOfFile>(*this);
}

void GraphicsObjectOfFile::Execute() {
  if (!animator_.IsPlaying())
    return;

  auto anmtime = animator_.GetAnimationTime();
  size_t current_frame =
      std::chrono::duration_cast<std::chrono::milliseconds>(anmtime).count() /
      frame_time_;

  const auto total_frames = surface_->GetNumPatterns();
  if (current_frame >= total_frames) {
    if (animator_.GetAfterAction() == AFTER_LOOP)
      current_frame %= total_frames;
    else
      current_frame = total_frames - 1;
  }
}

std::shared_ptr<const SDLSurface> GraphicsObjectOfFile::CurrentSurface(
    const GraphicsObject& rp) const {
  return surface_;
}

Rect GraphicsObjectOfFile::SrcRect(const GraphicsObject& go) const {
  if (current_frame_ >= 0) {
    // If we've ever been treated as an animation, we need to continue acting
    // as an animation even if we've stopped.
    return surface_->GetPattern(current_frame_).rect;
  }

  return GraphicsObjectData::SrcRect(go);
}

Point GraphicsObjectOfFile::DstOrigin(const GraphicsObject& go) const {
  if (current_frame_ >= 0) {
    const GrpRect& rect = surface_->GetPattern(current_frame_);
    return Point(rect.originX, rect.originY);
  }

  return GraphicsObjectData::DstOrigin(go);
}

void GraphicsObjectOfFile::PlaySet(int frame_time) {
  frame_time_ = frame_time;
  current_frame_ = 0;

  if (frame_time_ == 0) {
    std::cerr << "WARNING: GraphicsObjectOfFile::PlaySet(0) is invalid;"
              << " this is probably going to cause a graphical glitch..."
              << std::endl;
    frame_time_ = 10;
  }

  animator_.Reset();
}

Animator const* GraphicsObjectOfFile::GetAnimator() const {
  if (surface_->GetNumPatterns() <= 0)
    return nullptr;
  return &animator_;
}

Animator* GraphicsObjectOfFile::GetAnimator() {
  if (surface_->GetNumPatterns() <= 0)
    return nullptr;
  return &animator_;
}

// -----------------------------------------------------------------------
// class CompositeGraphicsObject
CompositeGraphicsObject::CompositeGraphicsObject(
    std::vector<CompositeGraphicsObjectLayer> layers)
    : layers_(std::move(layers)), bounds_(ComputeCompositeBounds(layers_)) {}

CompositeGraphicsObject::~CompositeGraphicsObject() = default;

void CompositeGraphicsObject::Render(const GraphicsObject& go,
                                     std::optional<ParentObjState> parent,
                                     std::optional<ObjectMask> mask) {
  const float parent_alpha = parent ? parent->alpha : 1.f;
  const float alpha = GetRenderingAlpha(go, parent_alpha);
  const std::optional<RenderState> parent_state =
      parent ? std::make_optional(parent->render_state) : std::nullopt;
  const ObjectParameter& param = go.Param();

  for (const auto& layer : layers_) {
    const GrpRect& pattern = layer.surface->GetPattern(layer.cut_no);
    RenderGeometry geometry =
        BuildLayerGeometry(*this, go, layer, pattern, parent_state);

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
      config.model = BuildCompositeModelMatrix(geometry, dst_rect);
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

int CompositeGraphicsObject::PixelWidth(const GraphicsObject& rp) {
  return int(rp.Param().GetWidthScaleFactor() * bounds_.width());
}

int CompositeGraphicsObject::PixelHeight(const GraphicsObject& rp) {
  return int(rp.Param().GetHeightScaleFactor() * bounds_.height());
}

std::unique_ptr<GraphicsObjectData> CompositeGraphicsObject::Clone() const {
  return std::make_unique<CompositeGraphicsObject>(*this);
}

bool CompositeGraphicsObject::HitTestAtSource(const GraphicsObject& go,
                                              const Point& source_point,
                                              bool alpha_test) const {
  if (!alpha_test)
    return !layers_.empty();

  const Point composite_point(bounds_.x() + source_point.x(),
                              bounds_.y() + source_point.y());
  for (const auto& layer : layers_) {
    const GrpRect& pattern = layer.surface->GetPattern(layer.cut_no);
    const Rect local_rect = LayerLocalRect(layer);
    if (!local_rect.Contains(composite_point))
      continue;

    const Point layer_point(
        pattern.rect.x() + composite_point.x() - local_rect.x(),
        pattern.rect.y() + composite_point.y() - local_rect.y());
    if (pattern.rect.Contains(layer_point) &&
        layer.surface->GetRect().Contains(layer_point) &&
        layer.surface->GetPixelAt(layer_point).a() > 0) {
      return true;
    }
  }
  return false;
}

std::shared_ptr<const SDLSurface> CompositeGraphicsObject::CurrentSurface(
    const GraphicsObject& go) const {
  return {};
}

Rect CompositeGraphicsObject::SrcRect(const GraphicsObject& go) const {
  return Rect::REC(0, 0, bounds_.width(), bounds_.height());
}

Point CompositeGraphicsObject::DstOrigin(const GraphicsObject& go) const {
  return Point(-bounds_.x(), -bounds_.y());
}

// ------------------------------------------------------------------------------
// struct CompositeObjectPart
bool IsCompositeObjectName(std::string_view filename) {
  return filename.find('|') != std::string_view::npos;
}
std::vector<CompositeObjectPart> ParseCompositeObjectName(
    std::string_view filename) {
  std::string compact(filename);
  std::erase_if(compact, [](char ch) { return ch == ' '; });
  if (compact.empty())
    throw std::runtime_error("composite filename is empty");

  std::vector<CompositeObjectPart> result;
  std::size_t pos = 0;
  while (pos < compact.size()) {
    CompositeObjectPart part;

    const std::size_t name_begin = pos;
    while (pos < compact.size() && compact[pos] != '|' && compact[pos] != '(' &&
           compact[pos] != ')') {
      ++pos;
    }
    part.file_name = compact.substr(name_begin, pos - name_begin);
    if (part.file_name.empty()) {
      throw std::runtime_error("composite layer filename is empty");
    }

    auto StartsWith = [&](std::string_view key) {
      return pos <= compact.size() && compact.substr(pos, key.size()) == key;
    };
    auto ParseInt = [&](std::string_view name) {
      const char* begin = compact.data() + pos;
      const char* end = compact.data() + compact.size();
      int value = 0;
      auto [ptr, ec] = std::from_chars(begin, end, value);
      if (ec != std::errc{} || ptr == begin) {
        throw std::runtime_error("composite expected integer for " +
                                 std::string(name));
      }
      pos += static_cast<std::size_t>(ptr - begin);
      return value;
    };
    auto Expect = [&](char expected, std::string_view context) {
      if (pos >= compact.size() || compact[pos] != expected) {
        throw std::runtime_error("composite expected '" +
                                 std::string(1, expected) + "' in " +
                                 std::string(context));
      }
      ++pos;
    };
    if (pos < compact.size() && compact[pos] == '(') {
      ++pos;
      part.x = ParseInt("x");
      Expect(',', "composite layer offset");
      part.y = ParseInt("y");

      while (pos < compact.size() && compact[pos] == ',') {
        ++pos;
        if (StartsWith("blend")) {
          pos += 5;
          Expect('=', "composite layer blend");
          part.blend_type = ParseInt("blend");
          if (part.blend_type != 0) {
            throw std::runtime_error("Object.create composite blend=" +
                                     std::to_string(part.blend_type) +
                                     " is not supported");
          }
        } else {
          part.cut_no = ParseInt("cut_no");
        }
      }

      Expect(')', "composite layer parameters");
    }

    result.push_back(std::move(part));

    if (pos == compact.size())
      break;

    if (compact[pos] != '|') {
      throw std::runtime_error(
          "Object.create composite unexpected character '" +
          std::string(1, compact[pos]) + "'");
    }
    ++pos;
    if (pos == compact.size()) {
      throw std::runtime_error(
          "Object.create composite layer filename is empty");
    }
  }

  return result;
}
