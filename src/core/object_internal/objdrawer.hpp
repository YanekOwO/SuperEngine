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

#include <boost/serialization/access.hpp>

#include "core/object.hpp"
#include "core/object_internal/object_mask.hpp"
#include "core/rect.hpp"
#include "core/render_geometry.hpp"
#include "glm/mat4x4.hpp"

#include <memory>
#include <optional>

class GraphicsObject;
class SDLSurface;
class Animator;
struct glDestination;
struct glRenderable;
struct RenderingConfig;

glm::mat4 BuildModelMatrix(const RenderGeometry& geometry, const Rect& dst);

std::optional<RenderGeometry> ApplyClips(
    RenderGeometry geo,
    const GraphicsObject& go,
    const std::optional<ParentObjState>& parent = {});

void RenderObjectWithMask(glRenderable src,
                          const RenderingConfig& config,
                          glDestination dst,
                          const std::optional<ObjectMask>& mask);

class GraphicsObjectData {
 public:
  GraphicsObjectData();
  virtual ~GraphicsObjectData();

  virtual void PlaySet(int set);

  virtual void Render(const GraphicsObject& go,
                      std::optional<ParentObjState> parent = {},
                      std::optional<ObjectMask> mask = {});

  virtual int PixelWidth(const GraphicsObject& rendering_properties) = 0;
  virtual int PixelHeight(const GraphicsObject& rendering_properties) = 0;
  // Returns the destination rectangle on the screen to draw srcRect()
  // to. Override to return custom rectangles in the case of a custom animation
  // format.
  virtual Rect DstRect(const GraphicsObject& go, const GraphicsObject* parent);

  // Tests whether |point| hits this object using the same geometry used for
  // rendering. Button selection uses this instead of testing DstRect directly
  // so rotated and alpha-tested buttons behave like Siglus.
  bool HitTest(const GraphicsObject& go,
               const Point& point,
               std::optional<ParentObjState> parent = {});

  virtual std::unique_ptr<GraphicsObjectData> Clone() const = 0;

  bool IsAnimation() const { return GetAnimator() != nullptr; }
  virtual Animator const* GetAnimator() const { return nullptr; }
  virtual Animator* GetAnimator() { return nullptr; }
  virtual void Execute();

  // Template method used during rendering to get the surface to render.
  // Return a null shared_ptr to disable rendering.
  virtual std::shared_ptr<const SDLSurface> CurrentSurface(
      const GraphicsObject& rp) const = 0;

  // Returns the rectangle in currentSurface() to draw to the screen. Override
  // to return custom rectangles in the case of a custom animation format.
  virtual Rect SrcRect(const GraphicsObject& go) const;

  // Returns the offset to the destination, which is set on a per surface
  // basis. This template method can be ignored if you override dstRect().
  virtual Point DstOrigin(const GraphicsObject& go) const;

  // Returns the unparented object position. Animation formats can override this
  // when frame data contributes a position offset.
  virtual Point DstPosition(const GraphicsObject& go) const;

  // Controls the alpha during rendering. Default implementation just consults
  // the GraphicsObject.
  virtual float GetRenderingAlpha(const GraphicsObject& go,
                                  float parent_alpha = 1.0f) const;

  RenderState BuildRenderState(const GraphicsObject& go) const;
  RenderGeometry BuildRenderGeometry(
      const GraphicsObject& go,
      std::optional<RenderState> parent_state) const;

 protected:
  // Tests a point after HitTest() has mapped it into this drawer's source
  // coordinate system. Drawers with custom rendering geometry can override
  // this without duplicating clipping, scaling, and rotation handling.
  virtual bool HitTestAtSource(const GraphicsObject& go,
                               const Point& source_point,
                               bool alpha_test) const;

 private:
  // boost::serialization support
  friend class boost::serialization::access;
  template <class Archive>
  void serialize(Archive& ar, unsigned int version) {}
};
