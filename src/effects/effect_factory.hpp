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

#include <memory>

class RLMachine;
class SDLSurface;
class EffectLongOperation;
struct selRecord;

// Factory that creates all Effects. This factory is called with
// either a Gameexe and the \#SEL or \#SELR number, or it is passed the
// equivalent parameters.
class EffectFactory {
 public:
  // Builds an EffectLongOperation based off the \#SEL.selnum line in the
  // Gameexe.ini file. The coordinates, which are in grp* format (x1,
  // y1, x2, y2), are converted to rec* format and then based to the
  // build() method.
  static EffectLongOperation* BuildFromSEL(RLMachine& machine,
                                           std::shared_ptr<SDLSurface> src,
                                           std::shared_ptr<SDLSurface> dst,
                                           int selnum);

  // Returns a constructed LongOperation with the following properties
  // to perform a transition.
  static EffectLongOperation* Build(RLMachine& machine,
                                    std::shared_ptr<SDLSurface> src,
                                    std::shared_ptr<SDLSurface> dst,
                                    selRecord record);
};
