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

#include "libsiglus/siglus_scene_renderer.hpp"

#include "core/gameexe.hpp"
#include "core/mask.hpp"
#include "core/object.hpp"
#include "core/object_internal/objdrawer.hpp"
#include "core/object_internal/object_mask.hpp"
#include "core/stage.hpp"
#include "systems/graphics_system.hpp"
#include "systems/sdl/glrenderer.hpp"
#include "systems/sdl/screen_canvas.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"

#include <GL/glew.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace libsiglus {
namespace {

// Scene textures must remain opaque: object alpha is already resolved into
// their RGB, and the wipe applies its opacity to the completed scene once.
class ScopedTransitionCanvas {
 public:
  ScopedTransitionCanvas() : original_screen_(SDLSurface::screen_) {
    glGetIntegerv(GL_VIEWPORT, viewport_);
    glGetBooleanv(GL_COLOR_WRITEMASK, colour_mask_);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_framebuffer_);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_framebuffer_);
    const Size size = original_screen_->GetSize();
    glViewport(0, 0, size.width(), size.height());
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
  }

  ~ScopedTransitionCanvas() {
    SDLSurface::screen_ = std::move(original_screen_);
    glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
    glColorMask(colour_mask_[0], colour_mask_[1], colour_mask_[2],
                colour_mask_[3]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_framebuffer_);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read_framebuffer_);
  }

 private:
  std::shared_ptr<glFrameBuffer> original_screen_;
  GLint viewport_[4];
  GLboolean colour_mask_[4];
  GLint draw_framebuffer_;
  GLint read_framebuffer_;
};

class ScopedRenderParameters {
 public:
  ScopedRenderParameters(GraphicsObject& object,
                         const StageElement<Effect>::layer_type& effects)
      : object_(object), original_(object.Param()) {
    const int order = original_.z_order;
    const int layer = original_.z_layer;
    for (const auto& effect : effects) {
      if (!effect)
        continue;
      if (effect->Contains(order, layer))
        effect->ApplyTo(object_.Param());
    }
  }

  ~ScopedRenderParameters() { object_.Param() = std::move(original_); }

 private:
  GraphicsObject& object_;
  ObjectParameter original_;
};

}  // namespace

SiglusSceneRenderer::SiglusSceneRenderer(
    Stage& stage,
    System& system,
    std::shared_ptr<MaskList> mask_list,
    std::shared_ptr<QuakeList> screen_quakes,
    std::shared_ptr<Gameexe> local_config)
    : stage_(stage),
      system_(system),
      mask_list_(std::move(mask_list)),
      screen_quakes_(std::move(screen_quakes)),
      local_config_(std::move(local_config)) {}

void SiglusSceneRenderer::ExecuteFrame() {
  stage_.Execute();
  if (mask_list_)
    mask_list_->Execute();
}

void SiglusSceneRenderer::RenderScene() {
  RenderStageObjects();

  if (!system_.graphics().is_interface_hidden())
    system_.text().Render();
}

void SiglusSceneRenderer::RenderStageObjects() {
  if (!stage_.transition_render_range()) {
    RenderObjects(kLayerFg);
    return;
  }

  const auto screen = SDLSurface::screen_;
  ScopedTransitionCanvas render_state;
  const double progress = stage_.foreground_render_alpha();

  RenderObjects(kLayerFg, ObjectRange::Below);
  RenderObjects(progress >= 1.0 ? kLayerFg : kLayerNext, ObjectRange::Within);

  if (progress > 0.0 && progress < 1.0) {
    const Size size = screen->GetSize();
    if (!transition_canvas_ || transition_canvas_->GetSize() != size)
      transition_canvas_ = std::make_shared<ScreenCanvas>(size);

    SDLSurface::screen_ = transition_canvas_;
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glRenderer().ClearBuffer(transition_canvas_, RGBAColour::Black());
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

    // Rebuild from live stage state each frame. ScreenCanvas also supplies
    // safe framebuffer snapshots to colour-filter objects while drawing.
    RenderObjects(kLayerFg, ObjectRange::Below);
    RenderObjects(kLayerFg, ObjectRange::Within);

    SDLSurface::screen_ = screen;
    RenderingConfig config;
    config.alpha = static_cast<float>(progress);
    const Rect bounds(Point(0, 0), size);
    glRenderer().Render(
        {transition_canvas_->glFrameBuffer::GetTexture(), bounds}, config,
        {screen, bounds});
  }

  RenderObjects(kLayerFg, ObjectRange::Above);
}

void SiglusSceneRenderer::RenderObjects(int stage_layer, ObjectRange range) {
  to_render_.clear();
  const auto& objects = stage_.objects[stage_layer];
  for (size_t i = 0; i < objects.size(); ++i) {
    if (!objects[i])
      continue;
    const auto& param = objects[i]->Param();
    if (range != ObjectRange::All) {
      const auto sorter = std::make_pair(param.z_order, param.z_layer);
      const auto& bounds = *stage_.transition_render_range();
      if ((range == ObjectRange::Below && sorter >= bounds.begin) ||
          (range == ObjectRange::Within &&
           (sorter < bounds.begin || sorter > bounds.end)) ||
          (range == ObjectRange::Above && sorter <= bounds.end))
        continue;
    }
    to_render_.emplace_back(param.z_order, param.z_layer, param.z_depth,
                            static_cast<int>(i), objects[i].get());
  }
  RenderQueuedObjects(stage_layer);
}

void SiglusSceneRenderer::RenderQueuedObjects(int stage_layer) {
  std::sort(to_render_.begin(), to_render_.end());

  for (auto& object : to_render_) {
    GraphicsObject& graphics_object = *std::get<4>(object);
    ScopedRenderParameters parameters(graphics_object,
                                      stage_.EffectsForLayer(stage_layer));
    std::optional<ParentObjState> quake_parent;
    const QuakeTransform quake =
        TransformForOrder(graphics_object.Param().z_order, stage_layer);
    if (!quake.IsIdentity()) {
      quake_parent = ParentObjState{};
      quake_parent->render_state.pos_x = quake.translate_x;
      quake_parent->render_state.pos_y = quake.translate_y;
      quake_parent->render_state.scale_x = quake.scale;
      quake_parent->render_state.scale_y = quake.scale;
    }
    RenderObject(graphics_object, mask_list_.get(), std::move(quake_parent));
  }
}

QuakeTransform SiglusSceneRenderer::TransformForOrder(int order,
                                                      int stage_layer) const {
  if (QuakeRenderingStopped())
    return {};

  return ComposeQuakeTransforms(order, screen_quakes_.get(),
                                stage_.QuakesForLayer(stage_layer));
}

bool SiglusSceneRenderer::QuakeRenderingStopped() const {
  return local_config_ && (*local_config_)("quake_stop").Int().value_or(0) != 0;
}

void SiglusSceneRenderer::RenderObject(GraphicsObject& root,
                                       const MaskList* mask_list,
                                       std::optional<ParentObjState> parent) {
  if (!mask_list)
    mask_list = mask_list_.get();
  const auto resolve_mask =
      [mask_list](int index) -> std::optional<ObjectMask> {
    if (!mask_list)
      return std::nullopt;
    try {
      const Mask& mask = mask_list->At(index);
      if (!mask.surface())
        return std::nullopt;
      return ObjectMask{.surface = mask.surface(),
                        .origin = Point(mask.Param().x, mask.Param().y)};
    } catch (const std::out_of_range&) {
      return std::nullopt;
    }
  };

  const auto render_object =
      [&](const auto& self, GraphicsObject& object,
          std::optional<ParentObjState> parent,
          std::optional<ObjectMask> inherited_mask) -> void {
    if (!object.Param().visible())
      return;

    std::optional<ObjectMask> mask = std::move(inherited_mask);
    if (object.Param().mask_no >= 0)
      mask = resolve_mask(object.Param().mask_no);

    if (object.HasDrawer())
      object.GetDrawer().Render(object, parent, mask);

    if (!object.HasChildren())
      return;

    const ParentObjState child_parent = ParentObjState::BuildFrom(object);
    ParentObjState transformed_parent = child_parent;
    if (parent) {
      transformed_parent.render_state = RenderState::Fold(
          transformed_parent.render_state, parent->render_state);
    }
    for (auto& child : object.GetChildren()) {
      if (child)
        self(self, *child, transformed_parent, mask);
    }
  };

  render_object(render_object, root, std::move(parent), std::nullopt);
}

}  // namespace libsiglus
