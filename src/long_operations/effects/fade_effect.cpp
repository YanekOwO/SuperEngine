// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006 Elliot Glaysher
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
//
// -----------------------------------------------------------------------

#include "long_operations/effects/fade_effect.hpp"

#include "systems/sdl/sdl_surface.hpp"

namespace lop {

// -----------------------------------------------------------------------
// FadeEffect
// -----------------------------------------------------------------------

FadeEffect::FadeEffect(RLMachine& machine,
                       std::shared_ptr<SDLSurface> to,
                       std::shared_ptr<SDLSurface> from,
                       const Size& screen_size,
                       int time)
    : EffectLongOperation(machine, to, from, screen_size, time) {}

FadeEffect::~FadeEffect() {}

void FadeEffect::PerformEffectForTime(int currentTime) {
  // Blit the source image to the screen with the opacity
  int opacity = int((float(currentTime) / duration()) * 255);

  src_surface().RenderToScreen(Rect(0, 0, size()), Rect(0, 0, size()), opacity);
}

bool FadeEffect::BlitOriginalImage() const { return true; }

}  // namespace lop
