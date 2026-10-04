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

#include "core/haikei.hpp"
#include "core/hik.hpp"
#include "core/object.hpp"
#include "core/object_internal/objdrawer.hpp"
#include "core/stage.hpp"
#include "log/domain_logger.hpp"
#include "machine/rlmachine.hpp"
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

rlSceneRenderer::rlSceneRenderer(RLMachine& machine) : machine_(machine) {}

void rlSceneRenderer::ExecuteFrame() { machine_.stage().Execute(); }

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
  for (auto it = stage.foreground_objects.begin(),
            end = stage.foreground_objects.end();
       it != end; ++it) {
    if (!ShouldRenderObject(it.pos(), *it))
      continue;
    to_render_.emplace_back(it->Param().z_order, it->Param().z_layer,
                            it->Param().z_depth, static_cast<int>(it.pos()),
                            &*it);
  }
  std::sort(to_render_.begin(), to_render_.end());
  for (const auto& [order, layer, depth, pos, obj] : to_render_) {
    RenderObject(*obj);
  }

  // Render text
  if (!graphics.is_interface_hidden())
    system.text().Render();
}

void rlSceneRenderer::RenderObject(GraphicsObject& root) {
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

  render_object(render_object, root, std::nullopt);
}

bool rlSceneRenderer::ShouldRenderObject(size_t obj_number,
                                         const GraphicsObject&) {
  GraphicsSystem& graphics = machine_.GetSystem().graphics();
  const ObjectSettings& settings =
      graphics.GetObjectSettings(static_cast<int>(obj_number));
  if (settings.obj_on_off == 1 && graphics.should_show_object1() == false)
    return false;
  if (settings.obj_on_off == 2 && graphics.should_show_object2() == false)
    return false;
  if (settings.weather_on_off && graphics.should_show_weather() == false)
    return false;
  if (settings.space_key && graphics.is_interface_hidden())
    return false;

  return true;
}
