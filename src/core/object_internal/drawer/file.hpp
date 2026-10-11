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

#include "core/object_internal/animator.hpp"
#include "core/object_internal/objdrawer.hpp"

#include <memory>
#include <optional>
#include <vector>

class SDLSurface;

// -----------------------------------------------------------------------
// GraphicsObjectData class that encapsulates a G00 or ANM file.
//
// GraphicsObjectOfFile is used for loading individual bitmaps into an
// object. It has support for normal display, and also
class GraphicsObjectOfFile : public GraphicsObjectData {
 public:
  GraphicsObjectOfFile(std::shared_ptr<SDLSurface> surface);
  virtual ~GraphicsObjectOfFile();

  virtual int PixelWidth(const GraphicsObject& rp) override;
  virtual int PixelHeight(const GraphicsObject& rp) override;

  virtual std::unique_ptr<GraphicsObjectData> Clone() const override;

  virtual void Execute() override;

  virtual void PlaySet(int set) override;

  virtual Animator const* GetAnimator() const override;
  virtual Animator* GetAnimator() override;

 protected:
  virtual std::shared_ptr<const SDLSurface> CurrentSurface(
      const GraphicsObject& go) const override;
  virtual Rect SrcRect(const GraphicsObject& go) const override;
  virtual Point DstOrigin(const GraphicsObject& go) const override;

 private:
  Animator animator_;

  // The encapsulated surface to render
  std::shared_ptr<SDLSurface> surface_;

  // Number of milliseconds to spend on a single frame in the
  // animation
  unsigned int frame_time_;

  // Current frame displayed (when animating)
  int current_frame_;
};

// -----------------------------------------------------------------------
// GraphicsObjectData class that encapsulates layered G00 files.

struct CompositeGraphicsObjectLayer {
  std::shared_ptr<SDLSurface> surface;
  Point offset;
  int cut_no = 0;
  int blend_type = 0;
};
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
  explicit CompositeGraphicsObject(
      std::vector<CompositeGraphicsObjectLayer> layers);
  virtual ~CompositeGraphicsObject();

  virtual void Render(const GraphicsObject& go,
                      std::optional<ParentObjState> parent = {},
                      std::optional<ObjectMask> mask = {}) override;

  virtual int PixelWidth(const GraphicsObject& rp) override;
  virtual int PixelHeight(const GraphicsObject& rp) override;

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
  std::vector<CompositeGraphicsObjectLayer> layers_;
  Rect bounds_;
};
