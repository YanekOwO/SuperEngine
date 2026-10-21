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

#include <boost/serialization/split_member.hpp>

#include "core/image_resources.hpp"
#include "core/object_internal/objdrawer.hpp"
#include "machine/rlmachine.hpp"
#include "machine/serialization.hpp"

#include <memory>
#include <string>

class GraphicsObject;
class SDLSurface;
class System;

class DigitsGraphicsObject : public GraphicsObjectData {
 public:
  explicit DigitsGraphicsObject(std::shared_ptr<ImageResources> images);
  DigitsGraphicsObject(std::shared_ptr<ImageResources> images,
                       const std::string& font);
  virtual ~DigitsGraphicsObject();

  virtual int PixelWidth(const GraphicsObject& rendering_properties) override;
  virtual int PixelHeight(const GraphicsObject& rendering_properties) override;

  virtual std::unique_ptr<GraphicsObjectData> Clone() const override;

 protected:
  virtual std::shared_ptr<const SDLSurface> CurrentSurface(
      const GraphicsObject& go) const override;

 private:
  void UpdateSurface(const GraphicsObject& rp);
  bool NeedsUpdate(const GraphicsObject& rendering_properties);

  std::shared_ptr<ImageResources> images_;

  // The actual numerical value to display.
  int value_;

  // The source font.
  std::string font_name_;
  std::shared_ptr<const SDLSurface> font_;

  // The current composited surface.
  std::shared_ptr<SDLSurface> surface_;

  // boost::serialization support
  friend class boost::serialization::access;

  template <class Archive>
  void save(Archive& ar, const unsigned int file_version) const;

  template <class Archive>
  void load(Archive& ar, const unsigned int file_version);

  BOOST_SERIALIZATION_SPLIT_MEMBER()
};

// We need help creating DigitsGraphicsObject s since they don't have a default
// constructor:
namespace boost {
namespace serialization {
template <class Archive>
inline void load_construct_data(Archive& ar,
                                DigitsGraphicsObject* t,
                                const unsigned int file_version) {
  ::new (t)
      DigitsGraphicsObject(Serialization::currentMachine().image_resources());
}
}  // namespace serialization
}  // namespace boost
