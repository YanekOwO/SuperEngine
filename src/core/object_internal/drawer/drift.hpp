// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2009 Elliot Glaysher
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

#pragma once

#include <boost/serialization/access.hpp>
#include <boost/serialization/split_member.hpp>

#include <memory>
#include <string>
#include <vector>
#include "core/image_resources.hpp"
#include "utilities/clock.hpp"

#include "core/object_internal/objdrawer.hpp"
#include "machine/rlmachine.hpp"
#include "machine/serialization.hpp"
#include "systems/event_system.hpp"
#include "systems/system.hpp"

class GraphicsObject;
class SDLSurface;
class System;

// Draws a collection of particles to the screen. Used to implement snow and
// sakura petals.
//
// This implementation is incomplete, but hopefully a good enough
// approximate. There are full parameters that we don't take use, and
// speculation/approximations of what some things are.
//
// There's an additional set of commands that look like they control how these
// particles fade in and out, which is another thing we don't implement right
// now.
class DriftGraphicsObject : public GraphicsObjectData {
 public:
  DriftGraphicsObject(std::shared_ptr<ImageResources> images,
                      std::shared_ptr<Clock> clock,
                      Rect screen_rect);
  DriftGraphicsObject(std::shared_ptr<ImageResources> images,
                      std::shared_ptr<Clock> clock,
                      Rect screen_rect,
                      const std::string& filename);
  DriftGraphicsObject(const DriftGraphicsObject& system);
  virtual ~DriftGraphicsObject();

  // Implementation of GraphicsObjectData:
  virtual void Render(const GraphicsObject& go,
                      std::optional<ParentObjState> parent = {},
                      std::optional<ObjectMask> mask = {}) override;
  virtual int PixelWidth(const GraphicsObject& rendering_properties) override;
  virtual int PixelHeight(const GraphicsObject& rendering_properties) override;
  virtual std::unique_ptr<GraphicsObjectData> Clone() const override;

 protected:
  virtual std::shared_ptr<const SDLSurface> CurrentSurface(
      const GraphicsObject& go) const override;

 private:
  // Individual particle on screen.
  struct Particle {
    // Randomly generated starting location for this particle.
    int x;
    int y;

    // Current alpha
    int alpha;

    // The number of ticks when this particle was first
    int start_time;
  };

  // Loading step separate for de-serialization purposes.
  void LoadFile();

  // Current machine context.
  std::shared_ptr<ImageResources> images_;
  std::shared_ptr<Clock> clock_;
  Rect screen_rect_;

  // The name of the graphics file that was loaded.
  std::string filename_;

  // The encapsulated surface to render
  std::shared_ptr<const SDLSurface> surface_;

  // The individual particles that make up this drift object.
  std::vector<Particle> particles_;

  // The last time we were rendered. We keep track of this to make sure we
  // don't force refresh in a loop.
  int last_rendered_time_;

  // boost::serialization support
  friend class boost::serialization::access;

  template <class Archive>
  void save(Archive& ar, const unsigned int file_version) const;

  template <class Archive>
  void load(Archive& ar, const unsigned int file_version);

  BOOST_SERIALIZATION_SPLIT_MEMBER()
};

// DriftGraphicsObject doesn't have a default constructor:
namespace boost {
namespace serialization {
template <class Archive>
inline void load_construct_data(Archive& ar,
                                DriftGraphicsObject* t,
                                const unsigned int file_version) {
  ::new (t) DriftGraphicsObject(
      Serialization::currentMachine().image_resources(),
      Serialization::currentSystem().event().GetClock(),
      Serialization::currentMachine().presenter().screen_rect());
}
}  // namespace serialization
}  // namespace boost
