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

#pragma once

#include "systems/scene_renderer.hpp"

#include <cstddef>
#include <functional>
#include <tuple>
#include <vector>

class RLMachine;
class GraphicsObject;
class Stage;
class rlSceneRendererTest;

class rlSceneRenderer final : public ISceneRenderer {
 public:
  rlSceneRenderer(RLMachine& machine);

  void ExecuteFrame() override;
  void RenderScene() override;

 private:
  friend class rlSceneRendererTest;

  static void RenderObject(GraphicsObject& object);
  bool ShouldRenderObject(size_t obj_number, const GraphicsObject& object);

  RLMachine& machine_;
  using ToRenderVec =
      std::vector<std::tuple<int, int, int, int, GraphicsObject*>>;
  ToRenderVec to_render_;
};
