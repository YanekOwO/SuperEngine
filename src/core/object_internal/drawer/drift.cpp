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

#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/export.hpp>

#include "core/object_internal/drawer/drift.hpp"

#include "core/localrect.hpp"
#include "core/object.hpp"
#include "core/rect.hpp"
#include "log/domain_logger.hpp"
#include "systems/event_system.hpp"
#include "systems/sdl/glrenderer.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/system.hpp"
#include "utilities/graphics.hpp"

#include <string>
#include <vector>

namespace {

double ScaleAmplitude(int amplitude) {
  // So the amplitude of the curve in RealLive is weird. Some value close to
  // 100 means one width of the screen, 1 is a vary large amount that I can't
  // reliably measure, and values greater than 100 are increasingly smaller.  I
  // can't reliably measure this because I suspect that RL deliberately
  // introduces some randomness here. Oh well. There's probably some curve that
  // fits this, but whatever. I think this might be a valid approximation:
  int x = amplitude / 100;
  return 1 / static_cast<double>(x);
}

}  // namespace

static DomainLogger logger("DriftGraphicsObject");

DriftGraphicsObject::DriftGraphicsObject(std::shared_ptr<ImageResources> images,
                                         std::shared_ptr<Clock> clock,
                                         Rect screen_rect)
    : images_(std::move(images)),
      clock_(std::move(clock)),
      screen_rect_(screen_rect),
      filename_(),
      surface_(nullptr),
      last_rendered_time_(0) {}

DriftGraphicsObject::DriftGraphicsObject(const DriftGraphicsObject& obj)
    : GraphicsObjectData(obj),
      images_(obj.images_),
      clock_(obj.clock_),
      screen_rect_(obj.screen_rect_),
      filename_(obj.filename_),
      surface_(obj.surface_),
      last_rendered_time_(0) {}

DriftGraphicsObject::DriftGraphicsObject(std::shared_ptr<ImageResources> images,
                                         std::shared_ptr<Clock> clock,
                                         Rect screen_rect,
                                         const std::string& filename)
    : images_(std::move(images)),
      clock_(std::move(clock)),
      screen_rect_(screen_rect),
      filename_(filename),
      surface_(nullptr),
      last_rendered_time_(0) {
  LoadFile();
}

DriftGraphicsObject::~DriftGraphicsObject() {}

void DriftGraphicsObject::Render(const GraphicsObject& go,
                                 std::optional<ParentObjState> parent,
                                 std::optional<ObjectMask> mask) {
  if (parent) {
    logger(Severity::Warn)
        << "parent object takes no effect on drift graphics objects";
    parent = std::nullopt;
  }

  auto& param = go.Param();
  std::shared_ptr<const SDLSurface> surface = CurrentSurface(go);

  if (!surface)
    return;

  int current_time = static_cast<int>(clock_->GetTicks().count());
  last_rendered_time_ = current_time;

  size_t count = param.GetDriftParticleCount();
  bool use_animation = param.GetDriftUseAnimation();
  int start_pattern = param.GetDriftStartPattern();
  int end_pattern = param.GetDriftEndPattern();
  int animation_time = param.GetDriftAnimationTime();
  int yspeed = param.GetDriftYSpeed();
  int period = param.GetDriftPeriod();
  int amplitude = param.GetDriftAmplitude();
  int use_drift = param.GetDriftUseDrift();
  int drift_speed = param.GetDriftDriftSpeed();

  Rect bounding_box = param.drift.drift_area;
  if (bounding_box.x() == -1) {
    bounding_box = screen_rect_;
  }

  double scaled_amplitude =
      bounding_box.size().width() * ScaleAmplitude(amplitude);

  // Grab the drift object
  if (particles_.size() < count) {
    Particle p;
    p.x = rand() % bounding_box.size().width();   // NOLINT
    p.y = rand() % bounding_box.size().height();  // NOLINT
    p.alpha = 255;
    p.start_time = current_time;

    particles_.push_back(p);
  }

  // Now that we have all the particles, update state and render each
  // particle.
  for (const Particle& particle : particles_) {
    int pattern = start_pattern;
    if (use_animation && end_pattern > start_pattern) {
      int number_of_patterns = end_pattern - start_pattern + 1;
      int frame_time = animation_time / number_of_patterns;
      int frame_number = ((current_time - particle.start_time) / frame_time) %
                         number_of_patterns;
      pattern = start_pattern + frame_number;
    }
    Rect src = surface->GetPattern(pattern).rect;

    int dest_x = particle.x;
    int dest_y = particle.y;

    // Add the base yspeed.
    dest_y +=
        bounding_box.size().height() *
        (static_cast<double>((current_time - particle.start_time) % yspeed) /
         static_cast<double>(yspeed));

    // Add the sine wave that defines how the particle moves back and forth.
    if (period != 0 && amplitude != 0) {
      double result = sin(
          (static_cast<double>(current_time - particle.start_time) / period) *
          (2 * 3.14));
      dest_x += scaled_amplitude * result;
    }

    // Add the left drift if we have this bit set.
    if (use_drift) {
      dest_x -= bounding_box.size().width() *
                (static_cast<double>((current_time - particle.start_time) %
                                     drift_speed) /
                 static_cast<double>(drift_speed));
    }

    if (dest_x < 0)
      dest_x += bounding_box.size().width();
    else
      dest_x %= bounding_box.size().width();

    if (dest_y < 0)
      dest_y += bounding_box.size().height();
    else
      dest_y %= bounding_box.size().height();
    Rect dest(bounding_box.origin() + Size(dest_x, dest_y), src.size());

    if (param.has_clip_rect())
      ClipDestination(param.clip_rect(), src, dest);

    for (SDLSurface::TextureRecord it : surface->GetTextureArray()) {
      auto src_rect = src;
      auto dst_rect = dest;
      LocalRect coordinate_system(it.x_, it.y_, it.w_, it.h_);
      if (!coordinate_system.intersectAndTransform(src_rect, dst_rect))
        continue;
      RenderingConfig config;
      config.alpha = particle.alpha / 255.0f;
      RenderObjectWithMask({it.gltexture, src_rect}, config,
                           {SDLSurface::screen_, dst_rect}, mask);
    }
  }
}

int DriftGraphicsObject::PixelWidth(const GraphicsObject& go) {
  auto& rendering_properties = go.Param();
  return rendering_properties.GetDriftArea().width();
}

int DriftGraphicsObject::PixelHeight(const GraphicsObject& go) {
  auto& rendering_properties = go.Param();
  return rendering_properties.GetDriftArea().height();
}

std::unique_ptr<GraphicsObjectData> DriftGraphicsObject::Clone() const {
  return std::make_unique<DriftGraphicsObject>(*this);
}

std::shared_ptr<const SDLSurface> DriftGraphicsObject::CurrentSurface(
    const GraphicsObject& rp) const {
  return surface_;
}

void DriftGraphicsObject::LoadFile() {
  surface_ = images_->GetSurfaceNamed(filename_);
}

template <class Archive>
void DriftGraphicsObject::load(Archive& ar, unsigned int version) {
  ar& boost::serialization::base_object<GraphicsObjectData>(*this) & filename_;

  LoadFile();
}

// -----------------------------------------------------------------------

template <class Archive>
void DriftGraphicsObject::save(Archive& ar, unsigned int version) const {
  ar& boost::serialization::base_object<GraphicsObjectData>(*this) & filename_;
}

// -----------------------------------------------------------------------

BOOST_CLASS_EXPORT(DriftGraphicsObject);

// -----------------------------------------------------------------------

// Explicit instantiations for text archives (since we hide the
// implementation)

template void DriftGraphicsObject::save<boost::archive::text_oarchive>(
    boost::archive::text_oarchive& ar,
    unsigned int version) const;

template void DriftGraphicsObject::load<boost::archive::text_iarchive>(
    boost::archive::text_iarchive& ar,
    unsigned int version);
