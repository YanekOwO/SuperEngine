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

#include "long_operations/zoom_long_operation.hpp"

#include "machine/rlmachine.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/system.hpp"

namespace lop {

// -----------------------------------------------------------------------
// ZoomLongOperation
// -----------------------------------------------------------------------

ZoomLongOperation::ZoomLongOperation(
    RLMachine& machine,
    const std::shared_ptr<SDLSurface>& origSurface,
    const std::shared_ptr<SDLSurface>& srcSurface,
    const Rect& frect,
    const Rect& trect,
    const Rect& drect,
    const int time)
    : machine_(machine),
      orig_surface_(origSurface),
      src_surface_(srcSurface),
      frect_(frect),
      trect_(trect),
      drect_(drect),
      duration_(time),
      start_time_(machine.GetSystem().event().GetTicks()) {}

ZoomLongOperation::~ZoomLongOperation() {}

Task<int> ZoomLongOperation::Run(WaitContext& waits) {
  for (;;) {
    unsigned int time = machine_.GetSystem().event().GetTicks();
    unsigned int currentFrame = time - start_time_;

    bool fastForward = machine_.GetSystem().ShouldFastForward();

    if (currentFrame >= duration_ || fastForward) {
      co_return 0;
    } else {
      // Render to the screen
      GraphicsSystem& graphics = machine_.GetSystem().graphics();
      graphics.RenderCustomFrame([&]() {
        // First blit the original dc0 to the screen
        orig_surface_->RenderToScreen(orig_surface_->GetRect(),
                                      orig_surface_->GetRect(), 255);

        // figure out the new coordinates for the zoom.
        float ratio = currentFrame / float(duration_);
        Point zPt =
            frect_.origin() + ((trect_.origin() - frect_.origin()) * ratio);
        Size zSize = frect_.size() + ((trect_.size() - frect_.size()) * ratio);

        src_surface_->RenderToScreen(Rect(zPt, zSize), drect_, 255);
      });
      co_await waits.Schedule();
    }
  }
}

}  // namespace lop
