// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2007 Elliot Glaysher
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

// This code is heavily based off Haeleth's O'caml implementation
// (which translates binary GAN files to and from an XML
// representation), found at rldev/src/rlxml/gan.ml.

#include "core/object_internal/drawer/gan.hpp"

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

#include "core/object.hpp"
#include "core/object_internal/animator.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "utilities/clock.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

// -----------------------------------------------------------------------
// GanGraphicsObjectData
// -----------------------------------------------------------------------

GanGraphicsObjectData::GanGraphicsObjectData(
    std::string filename,
    std::shared_ptr<SDLSurface> image,
    std::vector<std::vector<GanDecoder::Frame>> frames,
    std::shared_ptr<Clock> clock)
    : animator_(clock ? std::move(clock) : std::make_shared<Clock>()),
      filename_(std::move(filename)),
      image_(image),
      animation_sets(std::move(frames)),
      current_set_(-1),
      current_frame_(-1),
      delta_time_(0) {}

GanGraphicsObjectData::~GanGraphicsObjectData() = default;

bool GanGraphicsObjectData::HasSet(int set) const {
  return set >= 0 && static_cast<std::size_t>(set) < animation_sets.size() &&
         !animation_sets[set].empty();
}

void GanGraphicsObjectData::PrimeSet(int set) {
  if (!HasSet(set))
    throw std::out_of_range("GanGraphicsObjectData::PrimeSet invalid set");

  current_set_ = set;
  current_frame_ = 0;
  delta_time_ = 0;
  animator_.SetIsFinished(true);
}

int GanGraphicsObjectData::PixelWidth(const GraphicsObject& go) {
  auto& rendering_properties = go.Param();

  if (current_set_ != -1 && current_frame_ != -1) {
    const Frame& frame = animation_sets.at(current_set_).at(current_frame_);
    if (frame.pattern != -1) {
      const GrpRect& rect = image_->GetPattern(frame.pattern);
      return int(rendering_properties.GetWidthScaleFactor() *
                 rect.rect.width());
    }
  }

  return 0;
}

int GanGraphicsObjectData::PixelHeight(const GraphicsObject& go) {
  auto& rendering_properties = go.Param();

  if (current_set_ != -1 && current_frame_ != -1) {
    const Frame& frame = animation_sets.at(current_set_).at(current_frame_);
    if (frame.pattern != -1) {
      const GrpRect& rect = image_->GetPattern(frame.pattern);
      return int(rendering_properties.GetHeightScaleFactor() *
                 rect.rect.height());
    }
  }

  return 0;
}

std::unique_ptr<GraphicsObjectData> GanGraphicsObjectData::Clone() const {
  return std::make_unique<GanGraphicsObjectData>(*this);
}

RGBAColour GanGraphicsObjectData::GetPixelColor(Point point, int cut_no) const {
  return image_ ? image_->GetPixelColor(point, cut_no) : RGBAColour::Clear();
}

void GanGraphicsObjectData::Execute() {
  // obtain delta time first to avoid adding up when paused
  unsigned int deltaTime = static_cast<unsigned int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          animator_.GetDeltaTime())
          .count());

  if (!animator_.IsPlaying())
    return;

  delta_time_ += deltaTime;
  if (current_frame_ >= 0) {
    const std::vector<Frame>& current_set = animation_sets.at(current_set_);
    const auto total_frames = current_set.size();

    while (delta_time_ >= static_cast<unsigned int>(
                              std::max(current_set[current_frame_].time, 1))) {
      delta_time_ -= static_cast<unsigned int>(
          std::max(current_set[current_frame_].time, 1));
      ++current_frame_;

      if (current_frame_ >= total_frames) {
        if (animator_.GetAfterAction() == AFTER_LOOP) {
          current_frame_ %= total_frames;
        } else {
          delta_time_ = 0;
          current_frame_ = total_frames - 1;
          animator_.SetIsFinished(true);
          break;
        }
      }
    }
  }
}

std::shared_ptr<const SDLSurface> GanGraphicsObjectData::CurrentSurface(
    const GraphicsObject&) const {
  if (current_set_ != -1 && current_frame_ != -1) {
    const Frame& frame = animation_sets.at(current_set_).at(current_frame_);

    if (frame.pattern != -1) {
      // We are currently rendering an animation AND the current frame says to
      // render something to the screen.
      return image_;
    }
  }

  return std::shared_ptr<const SDLSurface>();
}

Rect GanGraphicsObjectData::SrcRect(const GraphicsObject&) const {
  const Frame& frame = animation_sets.at(current_set_).at(current_frame_);
  if (frame.pattern != -1) {
    return image_->GetPattern(frame.pattern).rect;
  }

  return Rect();
}

Point GanGraphicsObjectData::DstOrigin(const GraphicsObject& go) const {
  const Frame& frame = animation_sets.at(current_set_).at(current_frame_);
  if (frame.pattern != -1) {
    const GrpRect& rect = image_->GetPattern(frame.pattern);
    return Point(rect.originX, rect.originY);
  }

  return Point();
}

Point GanGraphicsObjectData::DstPosition(const GraphicsObject& go) const {
  const Frame& frame = animation_sets.at(current_set_).at(current_frame_);
  return GraphicsObjectData::DstPosition(go) + Size(frame.x, frame.y);
}

float GanGraphicsObjectData::GetRenderingAlpha(const GraphicsObject& go,
                                               float parent_alpha) const {
  auto& param = go.Param();

  const Frame& frame = animation_sets.at(current_set_).at(current_frame_);
  if (frame.pattern != -1) {
    // Calculate the combination of our frame alpha with the current object
    // alpha.
    const float frame_alpha = frame.alpha / 255.0f;
    return frame_alpha * param.GetNormalizedAlpha() * parent_alpha;
  } else {
    // Should never happen.
    return param.GetNormalizedAlpha();
  }
}

void GanGraphicsObjectData::PlaySet(int set) {
  if (!HasSet(set))
    throw std::out_of_range("GanGraphicsObjectData::PlaySet invalid set");

  animator_.Reset();
  current_set_ = set;
  current_frame_ = 0;
  delta_time_ = 0;
}

BOOST_CLASS_EXPORT_IMPLEMENT(GanGraphicsObjectData)
