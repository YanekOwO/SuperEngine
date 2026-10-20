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

#include "long_operations/reallive_operations.hpp"

#include "core/object.hpp"
#include "core/object_internal/drawer/gan.hpp"
#include "core/stage.hpp"
#include "machine/rlmachine.hpp"
#include "modules/module_obj.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"

namespace lop {

Task<int> MenuReseter::Run(WaitContext&) {
  system_.in_menu_ = false;
  co_return 0;
}

// -----------------------------------------------------------------------

Task<int> RestoreTextSystemVisibility::Run(WaitContext&) {
  text_.set_system_visible(true);
  co_return 0;
}

// -----------------------------------------------------------------------

Task<int> ClearAndRestoreWindow::Run(WaitContext&) {
  text_.HideAllTextWindows();
  text_.set_active_window(window_);
  co_return 0;
}

// -----------------------------------------------------------------------

WaitForGanToFinish::WaitForGanToFinish(RLMachine& machine,
                                       int layer,
                                       int parent,
                                       int object)
    : machine_(machine),
      graphics_(machine.GetSystem().graphics()),
      mode_(machine.screen_update_mode()),
      layer_(layer),
      parent_(parent),
      object_(object) {
  machine_.SetScreenUpdateMode(RLMachine::SCREENUPDATEMODE_AUTOMATIC);
}

Task<int> WaitForGanToFinish::Run(WaitContext& waits) {
  while (true) {
    GraphicsObject& object = GetObject();
    if (!object.HasDrawer() || !object.GetDrawer().IsAnimation() ||
        !object.GetDrawer().GetAnimator()->IsPlaying()) {
      machine_.SetScreenUpdateMode(mode_);
      co_return 0;
    }
    co_await waits.Schedule();
  }
}

GraphicsObject& WaitForGanToFinish::GetObject() {
  if (parent_ != -1) {
    GraphicsObject& parent = machine_.stage().GetObject(layer_, parent_);
    EnsureIsParentObject(parent, graphics_.GetObjectLayerSize());
    return parent.TouchChild(object_);
  }
  return machine_.stage().GetObject(layer_, object_);
}

}  // namespace lop
