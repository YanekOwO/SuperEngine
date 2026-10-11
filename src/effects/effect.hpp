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

#pragma once

#include <functional>
#include <memory>

#include "core/rect.hpp"
#include "machine/long_operation.hpp"

class SDLSurface;
class RLMachine;

// SEL/SELR transition effects:
//
// recOpen/grpOpen use a large number of transition effects when
// compositing DC1 to DC0. These effects are created either based of
// the \#SEL/\#SELR entries in the Gameexe.ini file, or based off of
// arguments to a recOpen/grpOpen command. These transition effects
// are all LongOperations which take over the screen during the
// transition, surpressing the normal auto-redrawing behaviour.
//
// There are a large number of transition effects. Instead of trying to manually
// create them, use the EffectFactory instead, which will instantiate
// the correct EffectLongOperation subclass based off the parameters

// Transition effect on DCs.
//
// EffectLongOperation is the base class from which all transition effects
// defined on \#SELs derive from. These effects are all implemented as
// LongOperations on the RLMachine, as they are all long and blocking
// operations.
class EffectLongOperation : public LongOperation {
 public:
  // Sets up all other variables, adding 1 to both width and height; RL is the
  // only system I know of where ranges are inclusive...
  EffectLongOperation(RLMachine& machine,
                      std::shared_ptr<SDLSurface> src,
                      std::shared_ptr<SDLSurface> dst,
                      Size size,
                      int time);

  virtual ~EffectLongOperation();

  // Implements the LongOperation calling interface. This simply keeps
  // track of the current time and calls PerformEffectForTime() until
  // time > duration_, when the default implementation simply sets
  // the current dc0 to the original dc0, then blits dc1 onto it.
  virtual bool operator()(RLMachine& machine);

  // Accessors for which surfaces we're composing. These are public as
  // an ugly hack for ScrollOnScrollOff.cpp.
  SDLSurface& src_surface() { return *src_surface_; }
  SDLSurface& dst_surface() { return *dst_surface_; }

 protected:
  Size size() const { return screen_size_; }
  int width() const { return screen_size_.width(); }
  int height() const { return screen_size_.height(); }
  int duration() const { return duration_; }

  // Implements the effect. Usually, this is all that needs to be
  // overriden, other then the public constructor.
  virtual void PerformEffectForTime(int currentTime) = 0;

 private:
  // Whether the orriginal dc0 should be blitted onto the target
  // surface before we pass control to the effect
  virtual bool BlitOriginalImage() const = 0;

  // Defines the size of the screen; since effects update the entire screen.
  Size screen_size_;

  // Defines the duration of in milliseconds
  unsigned int duration_;

  // The time since startup when this effect started (in milliseconds)
  unsigned int start_time_;

  // Keep track of what machine we're running on so we can send an
  // appropriate endRealtimeTask() to the eventsystem on destruction
  RLMachine& machine_;

  // The source surface (previously known as DC1, before I realized
  // that temporary surfaces could in fact be part of effects)
  std::shared_ptr<SDLSurface> src_surface_;

  // The destination surface (previously known as DC0)
  std::shared_ptr<SDLSurface> dst_surface_;
};

// LongOperationDecorator used in cases where we need to blit an image
// to the screen after an EffectLongOperation finishes. This is most of the
// cases. This is optional and isn't part of EffectLongOperation because
// {rec,grp}OpenBg can take '?' as the name of the image file to load, in which
// case the Blit doesn't happen.
class BlitAfterEffectFinishes : public PerformAfterLongOperationDecorator {
 public:
  BlitAfterEffectFinishes(LongOperation* in,
                          std::shared_ptr<SDLSurface> src,
                          std::shared_ptr<SDLSurface> dst,
                          const Rect& srcRect,
                          const Rect& destRect);

  virtual ~BlitAfterEffectFinishes();

 private:
  virtual void PerformAfterLongOperation(RLMachine& machine) override;

  // The source surface (previously known as DC1, before I realized
  // that temporary surfaces could in fact be part of effects)
  std::shared_ptr<SDLSurface> src_surface_;

  // The destination surface (previously known as DC0)
  std::shared_ptr<SDLSurface> dst_surface_;

  Rect src_rect_, dest_rect_;
};
