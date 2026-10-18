// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
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

#include "long_operations/effects/effect_factory.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>

#include "long_operations/effects/blind_effect.hpp"
#include "long_operations/effects/drawer_effect.hpp"
#include "long_operations/effects/fade_effect.hpp"
#include "long_operations/effects/sel_record.hpp"
#include "long_operations/effects/wipe_effect.hpp"
#include "machine/rlmachine.hpp"
#include "systems/graphics_system.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/system.hpp"

namespace lop {

enum ScreenDirection {
  TOP_TO_BOTTOM = 0,  // From the top to the bottom
  BOTTOM_TO_TOP = 1,  // From the bottom to the top
  LEFT_TO_RIGHT = 2,  // From left to right
  RIGHT_TO_LEFT = 3   // From right to left
};

// -----------------------------------------------------------------------
// Factory methods for EffectFactory
// -----------------------------------------------------------------------

EffectLongOperation* BuildDrawerEffect(RLMachine& machine,
                                       std::shared_ptr<SDLSurface> src,
                                       std::shared_ptr<SDLSurface> dst,
                                       const Size& screen_size,
                                       int time,
                                       int style,
                                       int direction) {
  static const std::unordered_map<int, DrawerEffectDetails::Direction>
      map_direction = {
          {TOP_TO_BOTTOM, DrawerEffectDetails::Direction::TopToBottom},
          {BOTTOM_TO_TOP, DrawerEffectDetails::Direction::BottomToTop},
          {LEFT_TO_RIGHT, DrawerEffectDetails::Direction::LeftToRight},
          {RIGHT_TO_LEFT, DrawerEffectDetails::Direction::RightToLeft}};
  auto effect_direction = map_direction.at(direction);

  switch (style) {
    case 15:
      return new DrawerEffect<DrawerEffectDetails::ScrollStrategy,
                              DrawerEffectDetails::ScrollStrategy>(
          machine, effect_direction, src, dst, screen_size, time);
    case 16:
      return new DrawerEffect<DrawerEffectDetails::ScrollStrategy,
                              DrawerEffectDetails::SquashStrategy>(
          machine, effect_direction, src, dst, screen_size, time);
    case 17:
      return new DrawerEffect<DrawerEffectDetails::SquashStrategy,
                              DrawerEffectDetails::ScrollStrategy>(
          machine, effect_direction, src, dst, screen_size, time);

    case 18:
      return new DrawerEffect<DrawerEffectDetails::SquashStrategy,
                              DrawerEffectDetails::SquashStrategy>(
          machine, effect_direction, src, dst, screen_size, time);
    case 20:
      return new DrawerEffect<DrawerEffectDetails::SlideStrategy,
                              DrawerEffectDetails::NoneStrategy>(
          machine, effect_direction, src, dst, screen_size, time);
    case 21:
      return new DrawerEffect<DrawerEffectDetails::NoneStrategy,
                              DrawerEffectDetails::SlideStrategy>(
          machine, effect_direction, src, dst, screen_size, time);
    default:
      throw std::logic_error("Impossible drawer effect style number: " +
                             std::to_string(style));
  }
};

// Creates a specific subclass of WipeEffect for \#SEL #10, Wipe.
EffectLongOperation* BuildWipeEffect(RLMachine& machine,
                                     std::shared_ptr<SDLSurface> src,
                                     std::shared_ptr<SDLSurface> dst,
                                     const Size& screen_size,
                                     int time,
                                     int direction,
                                     int interpolation) {
  switch (direction) {
    case TOP_TO_BOTTOM:
      return new WipeTopToBottomEffect(machine, src, dst, screen_size, time,
                                       interpolation);
    case BOTTOM_TO_TOP:
      return new WipeBottomToTopEffect(machine, src, dst, screen_size, time,
                                       interpolation);
    case LEFT_TO_RIGHT:
      return new WipeLeftToRightEffect(machine, src, dst, screen_size, time,
                                       interpolation);
    case RIGHT_TO_LEFT:
      return new WipeRightToLeftEffect(machine, src, dst, screen_size, time,
                                       interpolation);
    default:
      std::cerr << "WARNING! Unsupported direction " << direction
                << " in EffectFactory::buildWipeEffect. Returning Top to"
                << " Bottom effect." << std::endl;
      return new WipeTopToBottomEffect(machine, src, dst, screen_size, time,
                                       interpolation);
  }
}

// Creates a specific subclass of BlindEffect for \#SEL #120, Blind.
EffectLongOperation* BuildBlindEffect(RLMachine& machine,
                                      std::shared_ptr<SDLSurface> src,
                                      std::shared_ptr<SDLSurface> dst,
                                      const Size& screen_size,
                                      int time,
                                      int direction,
                                      int xsize,
                                      int ysize) {
  // RL does something really weird: if the wrong xsize/ysize was set
  // (the correct one is zero), it uses the other.
  switch (direction) {
    case TOP_TO_BOTTOM:
      if (xsize == 0 && ysize > 0)
        xsize = ysize;
      return new BlindTopToBottomEffect(machine, src, dst, screen_size, time,
                                        xsize);
    case BOTTOM_TO_TOP:
      if (xsize == 0 && ysize > 0)
        xsize = ysize;
      return new BlindBottomToTopEffect(machine, src, dst, screen_size, time,
                                        xsize);
    case LEFT_TO_RIGHT:
      if (ysize == 0 && xsize > 0)
        ysize = xsize;
      return new BlindLeftToRightEffect(machine, src, dst, screen_size, time,
                                        ysize);
    case RIGHT_TO_LEFT:
      if (ysize == 0 && xsize > 0)
        ysize = xsize;
      return new BlindRightToLeftEffect(machine, src, dst, screen_size, time,
                                        ysize);

    default:
      std::cerr << "WARNING! Unsupported direction " << direction
                << " in EffectFactory::buildWipeEffect. Returning Top to"
                << " Bottom effect." << std::endl;
      if (xsize == 0 && ysize > 0)
        xsize = ysize;
      return new BlindTopToBottomEffect(machine, src, dst, screen_size, time,
                                        xsize);
  }
}

// -----------------------------------------------------------------------
// EffectFactory
// -----------------------------------------------------------------------

EffectLongOperation* EffectFactory::BuildFromSEL(
    RLMachine& machine,
    std::shared_ptr<SDLSurface> src,
    std::shared_ptr<SDLSurface> dst,
    int selNum) {
  auto sel_record = GetSelRecord(machine.GetSystem().gameexe(), selNum);
  return Build(machine, src, dst, sel_record);
}

EffectLongOperation* EffectFactory::Build(RLMachine& machine,
                                          std::shared_ptr<SDLSurface> src,
                                          std::shared_ptr<SDLSurface> dst,
                                          selRecord record) {
  Size screen_size = machine.GetSystem().graphics().screen_size();

  const auto time = record.duration;
  const auto direction = record.direction;
  const auto style = record.dsp;
  switch (style) {
    case 10: {
      const auto interpolation = record.op[0];
      return BuildWipeEffect(machine, src, dst, screen_size, time, direction,
                             interpolation);
    }
    // We have the bunch of similar effects that are all implemented by
    // class DrawerEffect
    case 15:
    case 16:
    case 17:
    case 18:
    case 20:
    case 21:
      return BuildDrawerEffect(machine, src, dst, screen_size, time, style,
                               direction);
    case 120: {
      const auto xsize = record.op[1];
      const auto ysize = record.op[2];
      return BuildBlindEffect(machine, src, dst, screen_size, time, direction,
                              xsize, ysize);
    }
    case 0:
    case 50:
    default:
      return new FadeEffect(machine, src, dst, screen_size, time);
  }
}

}  // namespace lop
