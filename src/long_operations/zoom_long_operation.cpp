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
#include "systems/sdl/sdl_surface.hpp"
#include "systems/system.hpp"

#include <algorithm>

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

Task<int> ZoomLongOperation::Run(WaitContext& waits) {
  while (machine_.GetSystem().event().GetTicks() - start_time_ < duration_ &&
         !machine_.GetSystem().ShouldFastForward()) {
    co_await waits.Schedule();
  }
  co_return 0;
}

FrameRenderer ZoomLongOperation::GetFrameRenderer() {
  return [this] {
    orig_surface_->RenderToScreen(orig_surface_->GetRect(),
                                  orig_surface_->GetRect(), 255);
    const auto elapsed = machine_.GetSystem().event().GetTicks() - start_time_;
    const float ratio =
        duration_ == 0 || machine_.GetSystem().ShouldFastForward()
            ? 1.0f
            : std::min(elapsed, duration_) / float(duration_);
    const Point origin =
        frect_.origin() + ((trect_.origin() - frect_.origin()) * ratio);
    const Size size = frect_.size() + ((trect_.size() - frect_.size()) * ratio);
    src_surface_->RenderToScreen(Rect(origin, size), drect_, 255);
  };
}

}  // namespace lop
