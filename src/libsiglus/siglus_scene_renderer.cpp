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
#include "systems/system.hpp"
#include "systems/text_system.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace libsiglus {

class ScopedRenderParameters {
 public:
  ScopedRenderParameters(GraphicsObject& object,
                         double multiplier,
                         const StageElement<StageEffect>::layer_type& effects)
      : object_(object), original_(object.Param()) {
    const int order = original_.z_order;
    const int layer = original_.z_layer;
    for (const auto& effect : effects) {
      if (!effect)
        continue;
      if (effect->Contains(order, layer))
        effect->ApplyTo(object_.Param());
    }

    const int alpha = std::clamp(
        static_cast<int>(std::lround(object_.Param().raw_alpha() * multiplier)),
        0, 255);
    object_.Param().SetAlpha(alpha);
  }

  ~ScopedRenderParameters() { object_.Param() = std::move(original_); }

 private:
  GraphicsObject& object_;
  ObjectParameter original_;
};

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
  RenderStageObjects(stage_, to_render_);

  if (!system_.graphics().is_interface_hidden())
    system_.text().Render();
}

void SiglusSceneRenderer::RenderStageObjects(Stage& stage,
                                             ToRenderVec& to_render) {
  to_render.clear();
  QueueObjects(stage.objects[kLayerNext], 0, stage.next_render_alpha(),
               kLayerNext);
  QueueObjects(stage.objects[kLayerFg], 1, stage.foreground_render_alpha(),
               kLayerFg);
  RenderQueuedObjects();
}

void SiglusSceneRenderer::QueueObjects(
    StageElement<GraphicsObject>::layer_type& objects,
    int source_order,
    double alpha_multiplier,
    int effect_layer) {
  for (size_t i = 0; i < objects.size(); ++i)
    if (objects[i])
      to_render_.emplace_back(
          objects[i]->Param().z_order, objects[i]->Param().z_layer,
          objects[i]->Param().z_depth, static_cast<int>(i), source_order,
          objects[i].get(), alpha_multiplier, effect_layer);
}

void SiglusSceneRenderer::RenderQueuedObjects() {
  std::sort(to_render_.begin(), to_render_.end());

  for (auto& object : to_render_) {
    const double alpha_multiplier = std::get<6>(object);
    if (alpha_multiplier <= 0.0)
      continue;

    GraphicsObject& graphics_object = *std::get<5>(object);
    const int effect_layer = std::get<7>(object);
    ScopedRenderParameters parameters(graphics_object, alpha_multiplier,
                                      stage_.EffectsForLayer(effect_layer));
    std::optional<ParentObjState> quake_parent;
    const QuakeTransform quake =
        TransformForOrder(graphics_object.Param().z_order, effect_layer);
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
