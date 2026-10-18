// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2009 Elliot Glaysher
// Copyright (C) 2026 RLVM contributors
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

#include "core/colour.hpp"
#include "core/mwnd_config.hpp"

#include <cstddef>
#include <memory>
#include <vector>

class Point;
class Rect;
class RLMachine;
class Size;
class System;
class SDLSurface;
class GraphicsObject;
class ISceneRenderer;
struct ParentObjState;

class TextWaku {
 public:
  TextWaku(bool should_center_w, bool should_center_h);
  virtual ~TextWaku() = default;
  virtual std::unique_ptr<TextWaku> Clone() const = 0;
  virtual void ClearMouseCapture() {}

  virtual void Execute();
  virtual void Render(Point box_location,
                      Size namebox_size,
                      RGBAColour colour,
                      bool is_filter);

  // Possibly returns the size if this TextWaku object has a known size on
  // screen.
  virtual Size GetSize(const Size& text_surface) const = 0;

  virtual void SetMousePosition(const Point& pos) {}
  virtual void ClearMousePosition() {}
  virtual bool HasButtonAt(const Point& pos) const { return false; }
  virtual bool HandleMouseClick(const Point& pos, bool pressed) {
    return false;
  }
  virtual void SetWaitIcon(bool page, const Point& position) {}
  virtual void HideWaitIcon() {}
  virtual void SetMainSurface(std::shared_ptr<const SDLSurface> surface) {}
  virtual void SetFilterSurface(std::shared_ptr<const SDLSurface> surface) {}

  // Fixed-size legacy object_list; touching a slot allocates it lazily.
  std::shared_ptr<GraphicsObject> TouchObject(std::size_t index);
  // Template changes reinitialize contents while preserving slot handles.
  void ResetObjectsFrom(TextWaku& previous);
  std::shared_ptr<GraphicsObject> TouchButton(std::size_t index);
  void ResetButtonsFrom(TextWaku& previous);
  std::shared_ptr<GraphicsObject> TouchFace(std::size_t index);
  void ClearFaces();
  void ResetFacesFrom(TextWaku& previous);
  void RenderFaces(const Point& window_origin);
  ParentObjState ButtonParent(std::size_t index, const Rect& window) const;
  bool ButtonVisible(std::size_t index) const;
  static ParentObjState ObjectParent(const Point& window_origin);

  bool center_w_, center_h_;

  // mwnd.object
  std::vector<std::shared_ptr<GraphicsObject>> objects_;

  // mwnd.button and its template anchors. Script positions are offsets.
  std::vector<std::shared_ptr<GraphicsObject>> button_objects_;
  std::vector<MwndConfig::Button> button_config_;
  std::vector<bool> button_visible_;

  // mwnd.face and its template anchors. Script positions are offsets.
  std::vector<std::shared_ptr<GraphicsObject>> faces_;
  std::vector<Point> face_positions_;

  std::shared_ptr<ISceneRenderer> renderer_;

 protected:
  TextWaku(const TextWaku& source);
};
