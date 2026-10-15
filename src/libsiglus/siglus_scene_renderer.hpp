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

#pragma once

#include "core/quake.hpp"
#include "core/stage.hpp"
#include "systems/scene_renderer.hpp"

#include <memory>
#include <tuple>
#include <vector>

class GraphicsObject;
struct LocalConfig;
class System;
class MaskList;
struct ScreenCanvas;

namespace libsiglus {

class SiglusSceneRenderer final : public ISceneRenderer {
 public:
  SiglusSceneRenderer(::Stage& stage,
                      ::System& system,
                      std::shared_ptr<MaskList> mask_list,
                      std::shared_ptr<::QuakeList> screen_quakes,
                      ::LocalConfig& local_config);

  void ExecuteFrame() override;
  void RenderScene() override;

  void RenderObject(
      GraphicsObject& object,
      const MaskList* mask_list = nullptr,
      std::optional<ParentObjState> parent = std::nullopt) override;

 private:
  enum class ObjectRange { All, Below, Within, Above };
  // Sort by order, layer, kind (objects before windows), depth, then index.
  using ToRenderVec = std::vector<
      std::tuple<int, int, int, int, int, GraphicsObject*, TextWindow*>>;

  void RenderStageObjects();
  void RenderObjects(int stage_layer, ObjectRange range = ObjectRange::All);
  void RenderQueuedObjects(int stage_layer);
  QuakeTransform TransformForOrder(int order, int stage_layer) const;
  bool QuakeRenderingStopped() const;

  ::Stage& stage_;
  ::System& system_;
  std::shared_ptr<MaskList> mask_list_;
  std::shared_ptr<::QuakeList> screen_quakes_;
  ::LocalConfig& local_config_;
  ToRenderVec to_render_;
  std::shared_ptr<ScreenCanvas> transition_canvas_;
};

}  // namespace libsiglus
