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

#include "GL/glew.h"

#include "systems/sdl/graphics_backend.hpp"

#include "core/album.hpp"
#include "core/avdec/image_decoder.hpp"
#include "core/colour.hpp"
#include "log/domain_logger.hpp"
#include "systems/sdl/gl_utils.hpp"
#include "systems/sdl/glcanvas.hpp"
#include "systems/sdl/glrenderer.hpp"
#include "systems/sdl/gltexture.hpp"
#include "systems/sdl/screen_canvas.hpp"
#include "systems/sdl/sdl_surface.hpp"

#include <SDL3/SDL.h>

#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <cstring>

using std::string_literals::operator""s;

static DomainLogger logger("SDLGraphicsBackend");

static std::string LoadFile(const std::filesystem::path& pth) {
  std::ifstream ifs(pth, std::ios::binary);
  if (!ifs) {
    logger(Severity::Error) << "Cannot open file: " << pth.string();
    return {};
  }

  return std::string(std::istreambuf_iterator<char>(ifs),
                     std::istreambuf_iterator<char>());
}

namespace {

using SDL_SurfacePtr =
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

SDL_SurfacePtr CreateRGBASurface(Size size) {
  return SDL_SurfacePtr(
      SDL_CreateSurface(size.width(), size.height(), SDL_PIXELFORMAT_RGBA32),
      SDL_DestroySurface);
}

void SaveFrameBMP(const RenderFrameConfig& config, unsigned int framebuffer) {
  if (!config.frame_dump_path)
    return;

  const auto& path = *config.frame_dump_path;
  const int width = config.screen_size.width();
  const int height = config.screen_size.height();
  if (width <= 0 || height <= 0) {
    logger(Severity::Warn) << "Skipping frame dump for invalid screen size "
                           << config.screen_size.DebugString();
    return;
  }

  std::error_code ec;
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      logger(Severity::Warn)
          << "Could not create frame dump directory " << parent.string()
          << ": " << ec.message();
      return;
    }
  }

  std::vector<GLubyte> pixels(static_cast<size_t>(width) * height * 4);
  GLint previous_pack_alignment = 0;
  glGetIntegerv(GL_PACK_ALIGNMENT, &previous_pack_alignment);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, previous_pack_alignment);
  ShowGLErrors();

  SDL_SurfacePtr surface = CreateRGBASurface(config.screen_size);
  if (!surface) {
    logger(Severity::Warn) << "Could not allocate frame dump surface: "
                           << SDL_GetError();
    return;
  }

  if (SDL_MUSTLOCK(surface.get()) && !SDL_LockSurface(surface.get())) {
    logger(Severity::Warn) << "Could not lock frame dump surface: "
                           << SDL_GetError();
    return;
  }

  for (int y = 0; y < height; ++y) {
    void* dst = static_cast<uint8_t*>(surface->pixels) + y * surface->pitch;
    const void* src = pixels.data() +
                      static_cast<size_t>(height - y - 1) * width * 4;
    std::memcpy(dst, src, static_cast<size_t>(width) * 4);
  }

  if (SDL_MUSTLOCK(surface.get()))
    SDL_UnlockSurface(surface.get());

  if (!SDL_SaveBMP(surface.get(), path.string().c_str())) {
    logger(Severity::Warn) << "Could not save frame dump " << path.string()
                           << ": " << SDL_GetError();
  }
}

}  // namespace

SDLGraphicsBackend::SDLGraphicsBackend()
    : screen_contents_texture_(nullptr), screen_contents_texture_valid_(false) {
}

void SDLGraphicsBackend::InitSystem(Size screen_size, bool is_fullscreen) {
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                      SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);

  SDL_WindowFlags window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
  if (is_fullscreen)
    window_flags |= SDL_WINDOW_FULLSCREEN;

  window_ = SDL_CreateWindow("", screen_size.width(), screen_size.height(),
                             window_flags);
  if (!window_)
    throw std::runtime_error("Window creation failed: "s + SDL_GetError());

  gl_context_ = SDL_GL_CreateContext(window_);
  if (!gl_context_)
    throw std::runtime_error("GL context creation failed: "s + SDL_GetError());

  InitGLEW();

  ShowGLErrors();

  SDLSurface::screen_ = std::make_shared<ScreenCanvas>(screen_size);

  Resize(screen_size, is_fullscreen);
}
void SDLGraphicsBackend::QuitSystem() {}

Size SDLGraphicsBackend::Resize(Size display_size, bool is_fullscreen) {
  const bool now_fullscreen =
      SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN;
  if (is_fullscreen != now_fullscreen)
    SDL_SetWindowFullscreen(window_, is_fullscreen);
  if (!is_fullscreen) {
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    if (Size(w, h) != display_size)
      SDL_SetWindowSize(window_, display_size.width(), display_size.height());
  }
  SDL_SyncWindow(window_);

  int w = 0, h = 0;
  SDL_GetWindowSize(window_, &w, &h);
  return Size(w, h);
}

// -----------------------------------------------------------------------

std::shared_ptr<SDLSurface> SDLGraphicsBackend::CreateSurface(Size size) {
  if (size.width() <= 0 || size.height() <= 0)
    throw std::invalid_argument("Cannot create a surface of size " +
                                size.DebugString());
  return std::make_shared<SDLSurface>(size);
}

std::shared_ptr<SDLSurface> SDLGraphicsBackend::CreateSurfaceBGRA(
    Size size,
    std::span<char> bgra,
    bool is_alpha_mask) {
  // Note to self: These describe the byte order IN THE RAW G00 DATA!
  // These should NOT be switched to native byte order.
  SDL_Surface* tmp = SDL_CreateSurfaceFrom(
      size.width(), size.height(),
      is_alpha_mask ? SDL_PIXELFORMAT_ARGB8888 : SDL_PIXELFORMAT_XRGB8888,
      bgra.data(), size.width() * 4);
  if (!tmp)
    throw std::runtime_error("SDL_CreateSurfaceFrom failed: "s +
                             SDL_GetError());

  SDL_Surface* surf = SDL_DuplicateSurface(tmp);
  SDL_DestroySurface(tmp);
  if (!surf)
    throw std::runtime_error("SDL_DuplicateSurface failed: "s + SDL_GetError());

  return std::make_shared<SDLSurface>(surf);
}

std::shared_ptr<SDLSurface> SDLGraphicsBackend::LoadSurface(
    const std::filesystem::path& pth) {
  std::string raw = LoadFile(pth);
  ImageDecoder dec(raw);

  const auto width = dec.width;
  const auto height = dec.height;

  // do not free until SDL_DestroySurface() is called on the surface using it
  char* mem = dec.mem.data();
  bool is_mask = dec.ismask;
  if (is_mask) {
    int len = width * height;
    uint32_t* d = reinterpret_cast<uint32_t*>(mem);
    int i;
    for (i = 0; i < len; i++) {
      if ((*d & 0xff000000) != 0xff000000)
        break;
      d++;
    }
    if (i == len) {
      is_mask = false;
    }
  }

  std::shared_ptr<SDLSurface> s =
      CreateSurfaceBGRA(Size(width, height), dec.mem, is_mask);

  return std::make_shared<SDLSurface>(s->Release(),
                                      std::move(dec.region_table));
}

std::shared_ptr<Album> SDLGraphicsBackend::LoadAlbum(
    const std::filesystem::path& path) {
  std::string raw = LoadFile(path);
  ImageDecoder dec(raw);

  const auto width = dec.width;
  const auto height = dec.height;

  // do not free until SDL_DestroySurface() is called on the surface using it
  char* mem = dec.mem.data();
  bool is_mask = dec.ismask;
  if (is_mask) {
    int len = width * height;
    uint32_t* d = reinterpret_cast<uint32_t*>(mem);
    int i;
    for (i = 0; i < len; i++) {
      if ((*d & 0xff000000) != 0xff000000)
        break;
      d++;
    }
    if (i == len) {
      is_mask = false;
    }
  }

  std::shared_ptr<SDLSurface> s =
      CreateSurfaceBGRA(Size(width, height), dec.mem, is_mask);

  return std::make_shared<Album>(s, std::move(dec.region_table));
}

void SDLGraphicsBackend::SetWindowTitle(const std::string& title_utf8) {
  if (title_utf8 == current_window_title_)
    return;

  SDL_SetWindowTitle(window_, title_utf8.c_str());
  current_window_title_ = title_utf8;
}

void SDLGraphicsBackend::ShowSystemCursor(bool show) {
  if (show)
    SDL_ShowCursor();
  else
    SDL_HideCursor();
}

void SDLGraphicsBackend::PresentFrame(const RenderFrameConfig& config) {
  int window_width = 0, window_height = 0;
  SDL_GetWindowSizeInPixels(window_, &window_width, &window_height);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  const Rect view =
      AspectFitRect(config.screen_size, Size(window_width, window_height));
  const float scale =
      static_cast<float>(view.width()) / config.screen_size.width();
  const Point origin =
      view.origin() + Point(static_cast<int>(config.screen_origin.x() * scale),
                            static_cast<int>(config.screen_origin.y() * scale));

  glBindFramebuffer(GL_READ_FRAMEBUFFER, SDLSurface::screen_->GetID());
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glBlitFramebuffer(0, 0, config.screen_size.width(),
                    config.screen_size.height(), origin.x(),
                    window_height - (origin.y() + view.height()),
                    origin.x() + view.width(), window_height - origin.y(),
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SDLGraphicsBackend::RenderFrame(const RenderFrameConfig& config,
                                     const DrawCallback& draw_scene,
                                     const DrawCallback& draw_renderables,
                                     const DrawCallback& draw_cursor) {
  glRenderer renderer;
  renderer.SetUp();
  renderer.ClearBuffer(SDLSurface::screen_, RGBAColour(0, 0, 0, 255));
  ShowGLErrors();

  glViewport(0, 0, config.screen_size.width(), config.screen_size.height());

  if (draw_scene)
    draw_scene();
  if (draw_renderables)
    draw_renderables();

  if (config.manual_update_mode) {
    if (!screen_contents_texture_)
      screen_contents_texture_ = std::make_shared<glTexture>(config.screen_size);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, SDLSurface::screen_->GetID());
    glBindTexture(GL_TEXTURE_2D, screen_contents_texture_->GetID());
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                        config.screen_size.width(),
                        config.screen_size.height());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    screen_contents_texture_valid_ = true;
  } else {
    screen_contents_texture_valid_ = false;
  }

  if (draw_cursor)
    draw_cursor();

  PresentFrame(config);
  glFlush();
  SaveFrameBMP(config, SDLSurface::screen_->GetID());
  SDL_GL_SwapWindow(window_);
  ShowGLErrors();
}

bool SDLGraphicsBackend::RedrawLastFrame(const RenderFrameConfig& config,
                                         const DrawCallback& draw_cursor) {
  if (!config.manual_update_mode || !screen_contents_texture_valid_ ||
      !screen_contents_texture_)
    return false;

  glViewport(0, 0, config.screen_size.width(), config.screen_size.height());

  glRenderer renderer;
  renderer.Render(
      {screen_contents_texture_, Rect(Point(0, 0), config.screen_size)},
      {SDLSurface::screen_, Rect(Point(0, 0), config.screen_size)});

  if (draw_cursor)
    draw_cursor();

  PresentFrame(config);
  glFlush();
  SaveFrameBMP(config, SDLSurface::screen_->GetID());
  SDL_GL_SwapWindow(window_);
  ShowGLErrors();
  return true;
}

std::shared_ptr<SDLSurface> SDLGraphicsBackend::RenderToSurface(
    const RenderFrameConfig& config,
    const DrawCallback& draw_scene) {
  auto canvas = std::make_shared<glCanvas>(
      config.screen_size, config.display_size, config.screen_origin);
  canvas->Use();

  const std::shared_ptr<glFrameBuffer> original_screen = SDLSurface::screen_;
  SDLSurface::screen_ = canvas->GetBuffer();
  if (draw_scene)
    draw_scene();
  SDLSurface::screen_ = original_screen;

  std::shared_ptr<glTexture> texture = canvas->GetBuffer()->GetTexture();
  const int width = config.screen_size.width();
  const int height = config.screen_size.height();

  std::vector<GLubyte> buf(width * height * 4);
  glGetTextureSubImage(texture->GetID(), 0, 0, 0, 0, width, height, 1, GL_RGBA,
                       GL_UNSIGNED_BYTE, buf.size(), buf.data());

  SDL_Surface* surface =
      SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
  if (!surface)
    throw std::runtime_error("SDL_CreateSurface failed");

  for (int y = 0; y < height; ++y) {
    void* dst = static_cast<uint8_t*>(surface->pixels) + y * surface->pitch;
    const void* src = buf.data() + (height - y - 1) * width * 4;
    std::memcpy(dst, src, width * 4);
  }

  return std::make_shared<SDLSurface>(surface);
}
