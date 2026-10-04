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

#include "core/mask.hpp"
#include "core/object.hpp"
#include "core/object_internal/objdrawer.hpp"
#include "core/object_internal/object_mask.hpp"
#include "core/stage.hpp"
#include "log/domain_logger.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace libsiglus {

static DomainLogger logger("SiglusSceneRenderer");

class ScopedRenderParameters {
 public:
  ScopedRenderParameters(GraphicsObject& object,
                         double multiplier,
                         const std::vector<StageEffect>& effects)
      : object_(object), original_(object.Param()) {
    const int order = original_.z_order;
    const int layer = original_.z_layer;
    for (const StageEffect& effect : effects)
      if (effect.Contains(order, layer))
        effect.ApplyTo(object_.Param());

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

SiglusSceneRenderer::SiglusSceneRenderer(Stage& stage,
                                         System& system,
                                         std::shared_ptr<MaskList> mask_list)
    : stage_(stage), system_(system), mask_list_(std::move(mask_list)) {}

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
  QueueObjects(stage.next_objects, 0, stage.next_render_alpha(), kLayerNext);
  QueueObjects(stage.foreground_objects, 1, stage.foreground_render_alpha(),
               kLayerFg);
  RenderQueuedObjects();
}

void SiglusSceneRenderer::QueueObjects(LazyArray<GraphicsObject>& objects,
                                       int source_order,
                                       double alpha_multiplier,
                                       int effect_layer) {
  for (auto it = objects.begin(), end = objects.end(); it != end; ++it) {
    to_render_.emplace_back(it->Param().z_order, it->Param().z_layer,
                            it->Param().z_depth, static_cast<int>(it.pos()),
                            source_order, &*it, alpha_multiplier, effect_layer);
  }
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
    RenderObject(graphics_object, mask_list_.get());
  }
}

void SiglusSceneRenderer::RenderObject(GraphicsObject& root,
                                       const MaskList* mask_list) {
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

    if (parent)
      logger(Severity::Warn) << "Nested parents are not supported yet.";

    const ParentObjState child_parent = ParentObjState::BuildFrom(object);
    for (auto& child : object.GetChildren()) {
      if (child)
        self(self, *child, child_parent, mask);
    }
  };

  render_object(render_object, root, std::nullopt, std::nullopt);
}

}  // namespace libsiglus
