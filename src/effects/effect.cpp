// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006 Elliot Glaysher
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

#include "effects/effect.hpp"

#include "machine/rlmachine.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"
#include "systems/event_system.hpp"
#include "systems/sdl/sdl_surface.hpp"

// -----------------------------------------------------------------------
// EffectLongOperation
// -----------------------------------------------------------------------

EffectLongOperation::EffectLongOperation(RLMachine& machine,
                                         std::shared_ptr<SDLSurface> src,
                                         std::shared_ptr<SDLSurface> dst,
                                         Size size,
                                         int time)
    : screen_size_(size),
      duration_(time),
      start_time_(machine.GetSystem().event().GetTicks()),
      machine_(machine),
      src_surface_(src),
      dst_surface_(dst) {
  machine.GetSystem().graphics().set_is_responsible_for_update(false);
}

EffectLongOperation::~EffectLongOperation() {
  machine_.GetSystem().graphics().set_is_responsible_for_update(true);
}

bool EffectLongOperation::operator()(RLMachine& machine) {
  // TODO: this is ugly. Currently we have to manually manage the state of the
  // graphic system, and prevent it from automatically drawing during effects.
  // But when we have more than 2 effects queued up, the first one will change
  // the state of the graphic system to enable update in its destructor.
  // Consider introducing a concept of render layers later to make this cleaner.
  machine.GetSystem().graphics().set_is_responsible_for_update(false);

  unsigned int time = machine.GetSystem().event().GetTicks();
  unsigned int current_frame = time - start_time_;

  bool fast_forward = machine.GetSystem().ShouldFastForward();

  if (current_frame >= duration_ || fast_forward) {
    return true;
  } else {
    GraphicsSystem& graphics = machine.GetSystem().graphics();
    graphics.RenderCustomFrame([&]() {
      if (BlitOriginalImage()) {
        dst_surface().RenderToScreen(Rect(Point(0, 0), size()),
                                     Rect(Point(0, 0), size()), 255);
      }
      PerformEffectForTime(current_frame);
    });
    return false;
  }
}

// -----------------------------------------------------------------------
// BlitAfterEffectFinishes
// -----------------------------------------------------------------------

BlitAfterEffectFinishes::BlitAfterEffectFinishes(LongOperation* in,
                                                 std::shared_ptr<SDLSurface> src,
                                                 std::shared_ptr<SDLSurface> dst,
                                                 const Rect& srcRect,
                                                 const Rect& destRect)
    : PerformAfterLongOperationDecorator(in),
      src_surface_(src),
      dst_surface_(dst),
      src_rect_(srcRect),
      dest_rect_(destRect) {}

BlitAfterEffectFinishes::~BlitAfterEffectFinishes() {}

void BlitAfterEffectFinishes::PerformAfterLongOperation(RLMachine& machine) {
  // Blit DC1 onto DC0, with full opacity, and end the operation
  src_surface_->BlitToSurface(*dst_surface_, src_rect_, dest_rect_, 255);

  // Now force a screen refresh
  machine.GetSystem().graphics().ForceRefresh();
}
