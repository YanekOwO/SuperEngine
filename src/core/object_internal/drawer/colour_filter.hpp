// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2011 Elliot Glaysher
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

#include <boost/serialization/export.hpp>

#include "core/object_internal/objdrawer.hpp"
#include "core/rect.hpp"
#include "systems/sdl/gl_frame_buffer.hpp"
#include "utilities/serialization.hpp"

#include <memory>

class GraphicsObject;

class ColourFilterObjectData : public GraphicsObjectData {
 public:
  ColourFilterObjectData(const Rect& screen_rect);
  virtual ~ColourFilterObjectData();

  void set_rect(const Rect& screen_rect) { screen_rect_ = screen_rect; }

  // Overridden from GraphicsObjectData:
  virtual void Render(const GraphicsObject& go,
                      std::optional<ParentObjState> parent = {},
                      std::optional<ObjectMask> mask = {}) override;
  virtual int PixelWidth(const GraphicsObject& rendering_properties) override;
  virtual int PixelHeight(const GraphicsObject& rendering_properties) override;
  virtual std::unique_ptr<GraphicsObjectData> Clone() const override;

 protected:
  virtual std::shared_ptr<const SDLSurface> CurrentSurface(
      const GraphicsObject& rp) const override;
  virtual Rect SrcRect(const GraphicsObject& go) const override;
  virtual Point DstPosition(const GraphicsObject& go) const override;

 private:
  ColourFilterObjectData() = default;

  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) {
    ar& boost::serialization::base_object<GraphicsObjectData>(*this) &
        screen_rect_;
  }
  Rect screen_rect_;
};

BOOST_CLASS_EXPORT_KEY(ColourFilterObjectData)
