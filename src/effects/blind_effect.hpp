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

#include "effects/effect.hpp"

// Base class for implementing \#SEL transition style \#10, Blind.
class BlindEffect : public EffectLongOperation {
 public:
  BlindEffect(RLMachine& machine,
              std::shared_ptr<SDLSurface> src,
              std::shared_ptr<SDLSurface> dst,
              const Size& screen_size,
              int time,
              int blindSize);
  virtual ~BlindEffect();

 protected:
  const int blind_size() const { return blind_size_; }

  void ComputeGrowing(int maxSize, int currentTime);
  void ComputeDecreasing(int maxSize, int currentTime);

  virtual void RenderPolygon(int polyStart, int polyEnd) = 0;

 private:
  virtual bool BlitOriginalImage() const final;

  int blind_size_;
};

class BlindTopToBottomEffect : public BlindEffect {
 public:
  BlindTopToBottomEffect(RLMachine& machine,
                         std::shared_ptr<SDLSurface> src,
                         std::shared_ptr<SDLSurface> dst,
                         const Size& screen_size,
                         int time,
                         int blindSize);
  virtual ~BlindTopToBottomEffect();

 protected:
  virtual void PerformEffectForTime(int currentTime) override;
  virtual void RenderPolygon(int polyStart, int polyEnd) override;
};

class BlindBottomToTopEffect : public BlindEffect {
 public:
  BlindBottomToTopEffect(RLMachine& machine,
                         std::shared_ptr<SDLSurface> src,
                         std::shared_ptr<SDLSurface> dst,
                         const Size& screen_size,
                         int time,
                         int blindSize);
  virtual ~BlindBottomToTopEffect();

 protected:
  virtual void PerformEffectForTime(int currentTime) final;
  virtual void RenderPolygon(int polyStart, int polyEnd) final;
};

class BlindLeftToRightEffect : public BlindEffect {
 public:
  BlindLeftToRightEffect(RLMachine& machine,
                         std::shared_ptr<SDLSurface> src,
                         std::shared_ptr<SDLSurface> dst,
                         const Size& screen_size,
                         int time,
                         int blindSize);
  virtual ~BlindLeftToRightEffect();

 protected:
  virtual void PerformEffectForTime(int currentTime) final;
  virtual void RenderPolygon(int polyStart, int polyEnd) final;
};

class BlindRightToLeftEffect : public BlindEffect {
 public:
  BlindRightToLeftEffect(RLMachine& machine,
                         std::shared_ptr<SDLSurface> src,
                         std::shared_ptr<SDLSurface> dst,
                         const Size& screen_size,
                         int time,
                         int blindSize);
  virtual ~BlindRightToLeftEffect();

 protected:
  virtual void PerformEffectForTime(int currentTime) final;
  virtual void RenderPolygon(int polyStart, int polyEnd) final;
};
