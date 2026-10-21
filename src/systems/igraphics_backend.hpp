// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
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
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
//
// -----------------------------------------------------------------------

#pragma once

#include "core/rect.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <span>

class Album;
// TODO: Extract SDLSurface abstraction from SDLSurface class
class SDLSurface;

struct RenderFrameConfig {
  Size screen_size;
  Size display_size;
  Point screen_origin;
  bool cache_scene;
  // Offscreen captures can preserve alpha and translate the scene in pixels.
  bool transparent = false;
  Point render_offset;
};

using DrawCallback = std::function<void()>;

struct FrameRenderOptions {
  bool redraw_scene = true;
  bool cache_scene = false;
};

class IGraphicsBackend {
 public:
  virtual ~IGraphicsBackend() = default;

  virtual void InitSystem(Size screen_size, bool is_fullscreen) = 0;
  virtual void QuitSystem() = 0;

  virtual Size Resize(Size screen_size, bool is_fullscreen) = 0;

  virtual std::shared_ptr<SDLSurface> CreateSurface(Size size) = 0;
  virtual std::shared_ptr<SDLSurface> CreateSurfaceBGRA(Size size,
                                                        std::span<char> bgra,
                                                        bool is_alpha_mask) = 0;

  virtual std::shared_ptr<SDLSurface> LoadSurface(
      const std::filesystem::path& path) = 0;
  virtual std::shared_ptr<Album> LoadAlbum(
      const std::filesystem::path& path) = 0;

  virtual void SetWindowTitle(const std::string& title_utf8) = 0;
  virtual void ShowSystemCursor(bool show) = 0;
  virtual void WarpMouse(Point window_pos) = 0;

  virtual void RenderFrame(const RenderFrameConfig& config,
                           const DrawCallback& draw_scene,
                           const DrawCallback& draw_cursor) = 0;

  virtual bool RedrawLastFrame(const RenderFrameConfig& config,
                               const DrawCallback& draw_cursor) = 0;

  virtual std::shared_ptr<SDLSurface> RenderToSurface(
      const RenderFrameConfig& config,
      const DrawCallback& draw_scene) = 0;
};
