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

#include "systems/igraphics_backend.hpp"

#include <string>

struct SDL_Window;
struct SDL_GLContextState;
class glTexture;

class MouseCursor;

class SDLGraphicsBackend : public IGraphicsBackend {
 public:
  SDLGraphicsBackend();

  virtual void InitSystem(Size screen_size, bool is_fullscreen) override;
  virtual void QuitSystem() override;

  virtual Size Resize(Size screen_size, bool is_fullscreen) override;

  virtual std::shared_ptr<SDLSurface> CreateSurface(Size size) override;
  virtual std::shared_ptr<SDLSurface> CreateSurfaceBGRA(
      Size size,
      std::span<char> bgra,
      bool is_alpha_mask) override;

  virtual std::shared_ptr<SDLSurface> LoadSurface(
      const std::filesystem::path& path) override;
  virtual std::shared_ptr<Album> LoadAlbum(
      const std::filesystem::path& path) override;

  virtual void SetWindowTitle(const std::string& title_utf8) override;
  virtual void ShowSystemCursor(bool show) override;

  virtual void RenderFrame(const RenderFrameConfig& config,
                           const DrawCallback& draw_scene,
                           const DrawCallback& draw_renderables,
                           const DrawCallback& draw_cursor) override;

  virtual bool RedrawLastFrame(const RenderFrameConfig& config,
                               const DrawCallback& draw_cursor) override;

  virtual std::shared_ptr<SDLSurface> RenderToSurface(
      const RenderFrameConfig& config,
      const DrawCallback& draw_scene) override;

 private:
  void PresentFrame(const RenderFrameConfig& config);

  SDL_Window* window_ = nullptr;
  SDL_GLContextState* gl_context_ = nullptr;
  std::shared_ptr<glTexture> screen_contents_texture_;
  bool screen_contents_texture_valid_;
  std::string current_window_title_;
};
