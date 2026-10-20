// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
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

#include "machine/scene_renderer.hpp"

#include "core/gameexe.hpp"
#include "core/haikei.hpp"
#include "core/hik.hpp"
#include "core/object.hpp"
#include "core/object_internal/objdrawer.hpp"
#include "core/selection.hpp"
#include "core/stage.hpp"
#include "log/domain_logger.hpp"
#include "machine/rlmachine.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "systems/object_settings.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"

#include <algorithm>
#include <optional>

namespace {

DomainLogger logger("rlSceneRenderer");

}  // namespace

rlSceneRenderer::rlSceneRenderer(RLMachine& machine) : machine_(machine) {
  // RealLive has one scene-wide quake on the foreground stage.
  if (machine_.stage().quakes.Size(kLayerFg) == 0)
    machine_.stage().quakes.Resize(kLayerFg, 1);
  last_quake_tick_ = machine_.GetSystem().event().GetTicks();
}

void rlSceneRenderer::ExecuteFrame() {
  Stage& stage = machine_.stage();
  stage.Execute();
  const unsigned int now = machine_.GetSystem().event().GetTicks();
  const Point previous_origin = GetScreenOrigin();
  const bool was_active = stage.HasActiveQuakes();
  stage.AdvanceQuakes(now - last_quake_tick_);
  last_quake_tick_ = now;
  if (GetScreenOrigin() != previous_origin ||
      (was_active && !stage.HasActiveQuakes())) {
    machine_.GetSystem().graphics().ForceRefresh();
  }
}

void rlSceneRenderer::QueueShake(std::vector<QuakeState::OffsetFrame> frames) {
  machine_.stage().GetQuake(kLayerFg, 0)->QueueOffsets(frames);
  last_quake_tick_ = machine_.GetSystem().event().GetTicks();
  machine_.GetSystem().graphics().ForceRefresh();
}

Point rlSceneRenderer::GetScreenOrigin() const {
  const auto transform = ComposeQuakeTransforms(
      0, nullptr, machine_.stage().QuakesForLayer(kLayerFg));
  return Point(static_cast<int>(transform.translate_x),
               static_cast<int>(transform.translate_y));
}

void rlSceneRenderer::RenderScene() {
  Haikei& haikei = machine_.haikei();
  Stage& stage = machine_.stage();
  System& system = machine_.GetSystem();
  GraphicsSystem& graphics = system.graphics();

  // Render background
  switch (haikei.background_type()) {
    case BACKGROUND_DC0: {
      haikei.GetDC(0)->RenderToScreen(graphics.screen_rect(),
                                      graphics.screen_rect(), 255);
      break;
    }
    case BACKGROUND_HIK: {
      if (HIKRenderer* renderer = haikei.hik_renderer()) {
        renderer->Render();
      } else {
        haikei.GetHaikei()->RenderToScreen(graphics.screen_rect(),
                                           graphics.screen_rect(), 255);
      }
    }
  }

  // Render objects
  to_render_.clear();
  auto& foreground_objects = stage.objects[kLayerFg];
  for (size_t i = 0; i < foreground_objects.size(); ++i) {
    if (!foreground_objects[i] ||
        !ShouldRenderObject(i, *foreground_objects[i]))
      continue;
    to_render_.emplace_back(foreground_objects[i]->Param().z_order,
                            foreground_objects[i]->Param().z_layer,
                            foreground_objects[i]->Param().z_depth,
                            static_cast<int>(i), foreground_objects[i].get());
  }
  std::sort(to_render_.begin(), to_render_.end());
  for (const auto& [order, layer, depth, pos, obj] : to_render_) {
    RenderObject(*obj);
  }

  // Render text
  if (!graphics.is_interface_hidden())
    system.text().Render();
  if (auto selection = stage.selection[kLayerFg])
    SelState::Render(*selection, system);
}

void rlSceneRenderer::RenderObject(GraphicsObject& root,
                                   const MaskList* mask_list,
                                   std::optional<ParentObjState> parent) {
  const auto render_object = [&](const auto& self, GraphicsObject& object,
                                 std::optional<ParentObjState> parent) -> void {
    if (!object.Param().visible())
      return;

    if (object.HasDrawer())
      object.GetDrawer().Render(object, parent);

    if (!object.HasChildren())
      return;

    if (parent)
      logger(Severity::Warn) << "Nested parents are not supported yet.";

    const ParentObjState child_parent = ParentObjState::BuildFrom(object);
    for (auto& child : object.GetChildren()) {
      if (child)
        self(self, *child, child_parent);
    }
  };

  render_object(render_object, root, std::move(parent));
}

bool rlSceneRenderer::ShouldRenderObject(size_t obj_number,
                                         const GraphicsObject&) {
  GraphicsSystem& graphics = machine_.GetSystem().graphics();
  const ObjectSettings& settings =
      graphics.GetObjectSettings(static_cast<int>(obj_number));
  const auto& display_flags = machine_.GetGlobalConfig().object_disp_flag;
  if (settings.obj_on_off == 1 && display_flags.at(0) == 0)
    return false;
  if (settings.obj_on_off == 2 && display_flags.at(1) == 0)
    return false;
  if (settings.weather_on_off && display_flags.at(2) == 0)
    return false;
  if (settings.space_key && graphics.is_interface_hidden())
    return false;

  return true;
}
