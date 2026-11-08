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

#include "core/object_internal/drawer/file.hpp"

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

#include "core/object.hpp"
#include "log/core.hpp"
#include "log/domain_logger.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "utilities/clock.hpp"

#include <charconv>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

// -----------------------------------------------------------------------
// class GraphicsObjectOfFile
GraphicsObjectOfFile::GraphicsObjectOfFile(std::string filename,
                                           std::shared_ptr<SDLSurface> surface)
    : GraphicsObjectOfFile(
          Source{.kind = SourceKind::Asset, .filename = std::move(filename)},
          std::move(surface)) {}

GraphicsObjectOfFile::GraphicsObjectOfFile(Source source,
                                           std::shared_ptr<SDLSurface> surface)
    : animator_(std::make_shared<Clock>()),
      source_(std::move(source)),
      surface_(std::move(surface)),
      frame_time_(0),
      current_frame_(-1) {}

GraphicsObjectOfFile::~GraphicsObjectOfFile() = default;

int GraphicsObjectOfFile::PixelWidth(const GraphicsObject& rp) {
  if (!surface_)
    return 0;
  auto& param = rp.Param();

  const GrpRect& rect = surface_->GetPattern(param.GetPattNo());
  int width = rect.rect.width();
  return int(param.GetWidthScaleFactor() * width);
}

int GraphicsObjectOfFile::PixelHeight(const GraphicsObject& rp) {
  if (!surface_)
    return 0;
  auto& param = rp.Param();

  const GrpRect& rect = surface_->GetPattern(param.GetPattNo());
  int height = rect.rect.height();
  return int(param.GetHeightScaleFactor() * height);
}

std::unique_ptr<GraphicsObjectData> GraphicsObjectOfFile::Clone() const {
  return std::make_unique<GraphicsObjectOfFile>(*this);
}

RGBAColour GraphicsObjectOfFile::GetPixelColor(Point point, int cut_no) const {
  return surface_ ? surface_->GetPixelColor(point, cut_no)
                  : RGBAColour::Clear();
}

void GraphicsObjectOfFile::Execute() {
  if (!surface_ || !animator_.IsPlaying())
    return;

  auto anmtime = animator_.GetAnimationTime();
  size_t current_frame =
      std::chrono::duration_cast<std::chrono::milliseconds>(anmtime).count() /
      frame_time_;

  const auto total_frames = surface_->GetNumPatterns();
  if (current_frame >= total_frames) {
    if (animator_.GetAfterAction() == AFTER_LOOP)
      current_frame %= total_frames;
    else
      current_frame = total_frames - 1;
  }
}

std::shared_ptr<const SDLSurface> GraphicsObjectOfFile::CurrentSurface(
    const GraphicsObject& rp) const {
  return surface_;
}

Rect GraphicsObjectOfFile::SrcRect(const GraphicsObject& go) const {
  if (!surface_)
    return {};
  if (current_frame_ >= 0) {
    // If we've ever been treated as an animation, we need to continue acting
    // as an animation even if we've stopped.
    return surface_->GetPattern(current_frame_).rect;
  }

  return GraphicsObjectData::SrcRect(go);
}

Point GraphicsObjectOfFile::DstOrigin(const GraphicsObject& go) const {
  if (!surface_)
    return {};
  if (current_frame_ >= 0) {
    const GrpRect& rect = surface_->GetPattern(current_frame_);
    return Point(rect.originX, rect.originY);
  }

  return GraphicsObjectData::DstOrigin(go);
}

void GraphicsObjectOfFile::PlaySet(int frame_time) {
  frame_time_ = frame_time;
  current_frame_ = 0;

  if (frame_time_ == 0) {
    std::cerr << "WARNING: GraphicsObjectOfFile::PlaySet(0) is invalid;"
              << " this is probably going to cause a graphical glitch..."
              << std::endl;
    frame_time_ = 10;
  }

  animator_.Reset();
}

Animator const* GraphicsObjectOfFile::GetAnimator() const {
  if (!surface_ || surface_->GetNumPatterns() <= 0)
    return nullptr;
  return &animator_;
}

Animator* GraphicsObjectOfFile::GetAnimator() {
  if (!surface_ || surface_->GetNumPatterns() <= 0)
    return nullptr;
  return &animator_;
}

void GraphicsObjectOfFile::WarnIfCapture() const {
  if (source_.kind == SourceKind::Capture) {
    static DomainLogger logger("serialization");
    logger(Severity::Warn)
        << "Capture objects cannot retain their image in a save. "
           "Mark the object as not saved in Gameexe.ini.";
  }
}

void GraphicsObjectOfFile::RestoreSurface(const SerializationContext& context) {
  switch (source_.kind) {
    case SourceKind::Asset:
      surface_ = context.LoadSurface(source_.filename);
      break;
    case SourceKind::SaveThumbnail:
    case SourceKind::LocalThumbnail:
      surface_ = context.LoadThumbnail(
          source_.thumbnail_number, source_.kind == SourceKind::LocalThumbnail);
      break;
    case SourceKind::Capture:
      surface_.reset();
      break;
  }
}

// -----------------------------------------------------------------------
// class CompositeGraphicsObject
CompositeGraphicsObject::CompositeGraphicsObject(
    std::vector<ObjectLayer> layers) {
  if (layers.empty())
    throw std::runtime_error("Composite object requires at least one layer");
  layers_.SetLayers(std::move(layers));
  if (layers_.Bounds().is_degenerate())
    throw std::runtime_error("Composite object layers are empty");
}

CompositeGraphicsObject::~CompositeGraphicsObject() = default;

void CompositeGraphicsObject::Render(const GraphicsObject& go,
                                     std::optional<ParentObjState> parent,
                                     std::optional<ObjectMask> mask) {
  layers_.Render(*this, go, std::move(parent), std::move(mask));
}

int CompositeGraphicsObject::PixelWidth(const GraphicsObject& rp) {
  return int(rp.Param().GetWidthScaleFactor() * layers_.Bounds().width());
}

int CompositeGraphicsObject::PixelHeight(const GraphicsObject& rp) {
  return int(rp.Param().GetHeightScaleFactor() * layers_.Bounds().height());
}

std::unique_ptr<GraphicsObjectData> CompositeGraphicsObject::Clone() const {
  return std::make_unique<CompositeGraphicsObject>(*this);
}

RGBAColour CompositeGraphicsObject::GetPixelColor(Point point,
                                                  int cut_no) const {
  return cut_no == 0 ? layers_.GetPixelColor(point) : RGBAColour::Clear();
}

bool CompositeGraphicsObject::HitTestAtSource(const GraphicsObject& go,
                                              const Point& source_point,
                                              bool alpha_test) const {
  return layers_.HitTest(source_point, alpha_test);
}

std::shared_ptr<const SDLSurface> CompositeGraphicsObject::CurrentSurface(
    const GraphicsObject& go) const {
  return {};
}

Rect CompositeGraphicsObject::SrcRect(const GraphicsObject& go) const {
  return Rect(Point(), layers_.Bounds().size());
}

Point CompositeGraphicsObject::DstOrigin(const GraphicsObject& go) const {
  return Point(-layers_.Bounds().x(), -layers_.Bounds().y());
}

// ------------------------------------------------------------------------------
// struct CompositeObjectPart
bool IsCompositeObjectName(std::string_view filename) {
  return filename.find('|') != std::string_view::npos;
}
std::vector<CompositeObjectPart> ParseCompositeObjectName(
    std::string_view filename) {
  std::string compact(filename);
  std::erase_if(compact, [](char ch) { return ch == ' '; });
  if (compact.empty())
    throw std::runtime_error("composite filename is empty");

  std::vector<CompositeObjectPart> result;
  std::size_t pos = 0;
  while (pos < compact.size()) {
    CompositeObjectPart part;

    const std::size_t name_begin = pos;
    while (pos < compact.size() && compact[pos] != '|' && compact[pos] != '(' &&
           compact[pos] != ')') {
      ++pos;
    }
    part.file_name = compact.substr(name_begin, pos - name_begin);
    if (part.file_name.empty()) {
      throw std::runtime_error("composite layer filename is empty");
    }

    auto StartsWith = [&](std::string_view key) {
      return pos <= compact.size() && compact.substr(pos, key.size()) == key;
    };
    auto ParseInt = [&](std::string_view name) {
      const char* begin = compact.data() + pos;
      const char* end = compact.data() + compact.size();
      int value = 0;
      auto [ptr, ec] = std::from_chars(begin, end, value);
      if (ec != std::errc{} || ptr == begin) {
        throw std::runtime_error("composite expected integer for " +
                                 std::string(name));
      }
      pos += static_cast<std::size_t>(ptr - begin);
      return value;
    };
    auto Expect = [&](char expected, std::string_view context) {
      if (pos >= compact.size() || compact[pos] != expected) {
        throw std::runtime_error("composite expected '" +
                                 std::string(1, expected) + "' in " +
                                 std::string(context));
      }
      ++pos;
    };
    if (pos < compact.size() && compact[pos] == '(') {
      ++pos;
      part.x = ParseInt("x");
      Expect(',', "composite layer offset");
      part.y = ParseInt("y");

      while (pos < compact.size() && compact[pos] == ',') {
        ++pos;
        if (StartsWith("blend")) {
          pos += 5;
          Expect('=', "composite layer blend");
          part.blend_type = ParseInt("blend");
          if (part.blend_type != 0) {
            throw std::runtime_error("Object.create composite blend=" +
                                     std::to_string(part.blend_type) +
                                     " is not supported");
          }
        } else {
          part.cut_no = ParseInt("cut_no");
        }
      }

      Expect(')', "composite layer parameters");
    }

    result.push_back(std::move(part));

    if (pos == compact.size())
      break;

    if (compact[pos] != '|') {
      throw std::runtime_error(
          "Object.create composite unexpected character '" +
          std::string(1, compact[pos]) + "'");
    }
    ++pos;
    if (pos == compact.size()) {
      throw std::runtime_error(
          "Object.create composite layer filename is empty");
    }
  }

  return result;
}

BOOST_CLASS_EXPORT_IMPLEMENT(GraphicsObjectOfFile)
BOOST_CLASS_EXPORT_IMPLEMENT(CompositeGraphicsObject)
