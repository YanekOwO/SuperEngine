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

#pragma once

#include <boost/serialization/export.hpp>
#include "utilities/serialization.hpp"

#include "core/object_internal/animator.hpp"
#include "core/object_internal/objdrawer.hpp"
#include "core/object_internal/object_layers.hpp"
#include "core/serialization_context.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

class SDLSurface;

// -----------------------------------------------------------------------
// GraphicsObjectData class that encapsulates a G00 or ANM file.
//
// GraphicsObjectOfFile is used for loading individual bitmaps into an
// object. It has support for normal display, and also
class GraphicsObjectOfFile : public GraphicsObjectData {
 public:
  enum class SourceKind { Asset, SaveThumbnail, LocalThumbnail, Capture };
  struct Source {
    SourceKind kind = SourceKind::Capture;
    std::string filename;
    int thumbnail_number = 0;

   private:
    friend class boost::serialization::access;
    void serialize(auto& ar, unsigned int) {
      ar & kind & filename & thumbnail_number;
      if constexpr (std::decay_t<decltype(ar)>::is_loading::value)
        if (kind < SourceKind::Asset || kind > SourceKind::Capture)
          throw std::runtime_error("Invalid saved image source");
    }
  };

  GraphicsObjectOfFile(std::string filename,
                       std::shared_ptr<SDLSurface> surface);
  GraphicsObjectOfFile(Source source, std::shared_ptr<SDLSurface> surface);
  virtual ~GraphicsObjectOfFile();

  virtual int PixelWidth(const GraphicsObject& rp) override;
  virtual int PixelHeight(const GraphicsObject& rp) override;

  // Original cut dimensions, including padding; absent cuts have zero size.
  Size PatternSize(int cut_no) const;

  RGBAColour GetPixelColor(Point point, int cut_no) const override;

  virtual std::unique_ptr<GraphicsObjectData> Clone() const override;

  virtual void Execute() override;

  virtual void PlaySet(int set) override;

  virtual Animator const* GetAnimator() const override;
  virtual Animator* GetAnimator() override;

 protected:
  Rect HitTestSourceRect(const GraphicsObject& go) const override;

  virtual std::shared_ptr<const SDLSurface> CurrentSurface(
      const GraphicsObject& go) const override;
  virtual Rect SrcRect(const GraphicsObject& go) const override;
  virtual Point DstOrigin(const GraphicsObject& go) const override;

 private:
  GraphicsObjectOfFile() : GraphicsObjectOfFile(Source{}, nullptr) {}

  Animator animator_;
  Source source_;

  // The encapsulated surface to render
  std::shared_ptr<SDLSurface> surface_;

  // Number of milliseconds to spend on a single frame in the
  // animation
  unsigned int frame_time_;

  // Current frame displayed (when animating)
  int current_frame_;

 private:
  friend class boost::serialization::access;
  void save(auto& ar, unsigned int) const {
    WarnIfCapture();
    ar& boost::serialization::base_object<GraphicsObjectData>(*this) & source_ &
        frame_time_ & current_frame_ & animator_;
  }
  void load(auto& ar, unsigned int) {
    ar& boost::serialization::base_object<GraphicsObjectData>(*this) & source_ &
        frame_time_ & current_frame_ & animator_;
    RestoreSurface(SerializationContext::Get(ar));
  }
  BOOST_SERIALIZATION_SPLIT_MEMBER()
  void WarnIfCapture() const;
  void RestoreSurface(const SerializationContext& context);
};

// -----------------------------------------------------------------------
// GraphicsObjectData class that encapsulates layered G00 files.

struct CompositeObjectPart {
  std::string file_name;
  int x = 0;
  int y = 0;
  int cut_no = 0;
  int blend_type = 0;

  bool operator==(const CompositeObjectPart& rhs) const = default;
};
bool IsCompositeObjectName(std::string_view filename);
std::vector<CompositeObjectPart> ParseCompositeObjectName(
    std::string_view filename);

class CompositeGraphicsObject : public GraphicsObjectData {
 public:
  explicit CompositeGraphicsObject(std::vector<ObjectLayer> layers);
  virtual ~CompositeGraphicsObject();

  virtual void Render(const GraphicsObject& go,
                      std::optional<ParentObjState> parent = {},
                      std::optional<ObjectMask> mask = {}) override;

  virtual int PixelWidth(const GraphicsObject& rp) override;
  virtual int PixelHeight(const GraphicsObject& rp) override;

  RGBAColour GetPixelColor(Point point, int cut_no) const override;

  virtual std::unique_ptr<GraphicsObjectData> Clone() const override;

 protected:
  virtual bool HitTestAtSource(const GraphicsObject& go,
                               const Point& source_point,
                               bool alpha_test) const override;
  virtual std::shared_ptr<const SDLSurface> CurrentSurface(
      const GraphicsObject& go) const override;
  virtual Rect SrcRect(const GraphicsObject& go) const override;
  virtual Point DstOrigin(const GraphicsObject& go) const override;

 private:
  CompositeGraphicsObject() = default;

  ObjectLayers layers_;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) {
    ar& boost::serialization::base_object<GraphicsObjectData>(*this) & layers_;
  }
};

BOOST_CLASS_EXPORT_KEY(GraphicsObjectOfFile)
BOOST_CLASS_EXPORT_KEY(CompositeGraphicsObject)
