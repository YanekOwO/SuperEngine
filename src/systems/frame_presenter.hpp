// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
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

#include "systems/igraphics_backend.hpp"

// Window geometry and presentation, independent of scene and engine policy.
class FramePresenter {
 public:
  FramePresenter(std::shared_ptr<IGraphicsBackend> backend,
                 Size screen_size,
                 bool fullscreen);
  inline Size screen_size() const { return screen_size_; }
  inline Size display_size() const { return display_size_; }
  inline Rect screen_rect() const { return Rect(Point(), screen_size_); }
  inline std::shared_ptr<IGraphicsBackend> backend() const { return backend_; }

  void Resize(Size display_size, bool fullscreen);
  Point DisplayToScreenPoint(Point display_pos) const;
  void WarpMouse(Point game_pos);
  void RenderFrame(FrameRenderOptions options,
                   const DrawCallback& scene,
                   const DrawCallback& cursor = {},
                   Point origin = {});
  std::shared_ptr<SDLSurface> RenderToSurface(const DrawCallback& scene,
                                              Point origin = {});
  std::shared_ptr<SDLSurface> RenderToSurface(Size size,
                                              const DrawCallback& draw,
                                              bool transparent = false,
                                              Point offset = {});

 private:
  RenderFrameConfig FrameConfig(bool cache_scene, Point origin) const;
  std::shared_ptr<IGraphicsBackend> backend_;
  Size screen_size_;
  Size display_size_;
};
