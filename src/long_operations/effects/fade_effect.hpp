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

#pragma once

#include "long_operations/effects/effect.hpp"

// Simple fade in.
namespace lop {

class FadeEffect : public EffectLongOperation {
 public:
  FadeEffect(RLMachine& machine,
             std::shared_ptr<SDLSurface> src,
             std::shared_ptr<SDLSurface> dst,
             const Size& screen_size,
             int time);
  virtual ~FadeEffect();

 protected:
  virtual void PerformEffectForTime(int currentTime) final;

 private:
  virtual bool BlitOriginalImage() const final;
};

}  // namespace lop
