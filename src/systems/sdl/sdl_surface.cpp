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

#include "systems/sdl/sdl_surface.hpp"

#include <SDL3/SDL.h>

#include "core/colour.hpp"
#include "core/localrect.hpp"
#include "pygame/alphablit.h"
#include "core/object.hpp"
#include "systems/sdl/gl_frame_buffer.hpp"
#include "systems/sdl/gl_utils.hpp"
#include "systems/sdl/glrenderer.hpp"
#include "systems/sdl/gltexture.hpp"
#include "systems/sdl/screen_canvas.hpp"

#include "GL/glew.h"
#include "utilities/graphics.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <format>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <vector>

std::shared_ptr<glFrameBuffer> SDLSurface::screen_ = nullptr;

// -----------------------------------------------------------------------

static inline SDL_Rect ToSDLRect(const Rect rect) {
  SDL_Rect out;
  out.x = rect.x();
  out.y = rect.y();
  out.w = rect.width();
  out.h = rect.height();
  return out;
}

[[noreturn]] static inline void ThrowSDLError(std::string_view sdl_fn_name,
                                              std::string_view fn_name) {
  throw std::runtime_error(
      std::format("Error while calling SDL function '{}' in {}: {}",
                  sdl_fn_name, fn_name, SDL_GetError()));
}

// -----------------------------------------------------------------------

SDL_Surface* buildNewSurface(const Size& size) {
  // Create an empty surface. The ARGB8888 format has the same byte order as
  // the raw G00 data.
  SDL_Surface* tmp =
      SDL_CreateSurface(size.width(), size.height(), SDL_PIXELFORMAT_ARGB8888);

  if (tmp == NULL) {
    std::ostringstream ss;
    ss << "Couldn't allocate surface in build_new_surface"
       << ": " << SDL_GetError();
    throw std::runtime_error(ss.str());
  }

  return tmp;
}

// -----------------------------------------------------------------------
// SDLSurface::TextureRecord
// -----------------------------------------------------------------------
SDLSurface::TextureRecord::TextureRecord(SDLSurface const* me,
                                         int x,
                                         int y,
                                         int w,
                                         int h,
                                         unsigned int bytes_per_pixel,
                                         int byte_order,
                                         int byte_type)
    : x_(x),
      y_(y),
      w_(w),
      h_(h),
      bytes_per_pixel_(bytes_per_pixel),
      byte_order_(byte_order),
      byte_type_(byte_type) {
  reupload(me, Rect::REC(x, y, w, h));
}

// -----------------------------------------------------------------------

void SDLSurface::TextureRecord::reupload(SDLSurface const* me, Rect dirty) {
  if (gltexture) {
    Rect intersect = Rect::REC(x_, y_, w_, h_).Intersection(dirty);
    auto data = me->Dump(intersect);
    gltexture->Write(
        Rect(Point(intersect.x() - x_, intersect.y() - y_), intersect.size()),
        byte_order_, byte_type_, data.data());
  } else {
    auto data = me->Dump(Rect::REC(x_, y_, w_, h_));
    gltexture = std::make_shared<glTexture>(Size(w_, h_));
    gltexture->Write(Rect(Point(0, 0), Size(w_, h_)), byte_order_, byte_type_,
                     data.data());
  }
}

// -----------------------------------------------------------------------

void SDLSurface::TextureRecord::forceUnload() { gltexture.reset(); }

// -----------------------------------------------------------------------
// SDLSurface
// -----------------------------------------------------------------------

SDLSurface::SDLSurface()
    : surface_(NULL), texture_is_valid_(false), is_mask_(false) {}

// -----------------------------------------------------------------------

// SDLSurface that takes ownership of an externally created surface.
SDLSurface::SDLSurface(SDL_Surface* surf, std::vector<GrpRect> region_table)
    : surface_(surf),
      region_table_(std::move(region_table)),
      texture_is_valid_(false),
      is_mask_(false) {
  if (region_table_.empty())
    buildRegionTable(Size(surf->w, surf->h));
}

// -----------------------------------------------------------------------

SDLSurface::SDLSurface(const Size& size)
    : surface_(NULL), texture_is_valid_(false), is_mask_(false) {
  Allocate(size);
  buildRegionTable(size);
}

// -----------------------------------------------------------------------

// Constructor helper function
void SDLSurface::buildRegionTable(const Size& size) {
  // Build a region table with one entry the size of the surface (This
  // should never need to be used with objects created with this
  // constructor, but let's make sure everything is initialized since
  // it'll happen somehow.)
  GrpRect rect;
  rect.rect = Rect(Point(0, 0), size);
  rect.originX = 0;
  rect.originY = 0;
  region_table_.push_back(rect);
}

// -----------------------------------------------------------------------

SDLSurface::~SDLSurface() { Deallocate(); }

// -----------------------------------------------------------------------

SDL_Surface* SDLSurface::Release() { return std::exchange(surface_, nullptr); }

// -----------------------------------------------------------------------

Size SDLSurface::GetSize() const { return Size(surface_->w, surface_->h); }

Rect SDLSurface::GetRect() const { return Rect(Point(0, 0), GetSize()); }

// -----------------------------------------------------------------------

void SDLSurface::Allocate(const Size& size) {
  Deallocate();

  surface_ = buildNewSurface(size);

  Fill(RGBAColour::Black());
}

// -----------------------------------------------------------------------

void SDLSurface::Deallocate() {
  textures_.clear();
  if (surface_) {
    SDL_DestroySurface(surface_);
    surface_ = NULL;
  }
}

// TODO(erg): This function doesn't ignore alpha blending when use_src_alpha is
// false; thus, grp_open and grp_mask_open are really grp_mask_open.
void SDLSurface::BlitToSurface(SDLSurface& dest_surface,
                               const Rect& src,
                               const Rect& dst,
                               int alpha,
                               bool use_src_alpha) const {
  SDL_Rect src_rect = ToSDLRect(src), dest_rect = ToSDLRect(dst);

  // SDL 1.2 ignored the per-surface alpha when the source had per-pixel
  // alpha. It also ignored the alpha value on raw copies. This code keeps
  // both behaviors. Thus masked images and copies blit the same as before.
  const auto set_blit_mode = [](SDL_Surface* s, bool blended, int a) {
    SDL_SetSurfaceBlendMode(s,
                            blended ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_SetSurfaceAlphaMod(
        s, (blended && !SDL_ISPIXELFORMAT_ALPHA(s->format)) ? a : 255);
  };

  if (src.size() != dst.size()) {
    // Blit the source rectangle into its own image.
    SDL_Surface* src_image = buildNewSurface(src.size());
    SDL_SetSurfaceBlendMode(surface_, SDL_ISPIXELFORMAT_ALPHA(surface_->format)
                                          ? SDL_BLENDMODE_BLEND
                                          : SDL_BLENDMODE_NONE);
    if (pygame_AlphaBlit(surface_, &src_rect, src_image, NULL))
      ThrowSDLError("pygame_AlphaBlit", "GraphicsSystem::blitSurfaceToDC()");

    SDL_Surface* tmp = buildNewSurface(dst.size());
    pygame_stretch(src_image, tmp);

    set_blit_mode(tmp, use_src_alpha, alpha);
    if (!SDL_BlitSurface(tmp, NULL, dest_surface.RawSurface(), &dest_rect))
      ThrowSDLError("SDL_BlitSurface", "GraphicsSystem::blitSurfaceToDC()");

    SDL_DestroySurface(tmp);
    SDL_DestroySurface(src_image);
  } else {
    set_blit_mode(surface_, use_src_alpha, alpha);
    if (!SDL_BlitSurface(surface_, &src_rect, dest_surface.RawSurface(),
                         &dest_rect))
      ThrowSDLError("SDL_BlitSurface", "GraphicsSystem::blitSurfaceToDC()");
  }
  dest_surface.markWrittenTo(dst);
}

// -----------------------------------------------------------------------

// Allows for tight coupling with SDL_ttf. Rethink the existence of
// this function later.
void SDLSurface::blitFROMSurface(SDL_Surface* src_surface,
                                 const Rect& src,
                                 const Rect& dst,
                                 int alpha,
                                 bool use_src_alpha) {
  SDL_Rect src_rect = ToSDLRect(src), dest_rect = ToSDLRect(dst);

  if (use_src_alpha) {
    if (pygame_AlphaBlit(src_surface, &src_rect, surface_, &dest_rect))
      ThrowSDLError("pygame_AlphaBlit", "GraphicsSystem::blitSurfaceToDC()");
  } else {
    if (!SDL_BlitSurface(src_surface, &src_rect, surface_, &dest_rect))
      ThrowSDLError("SDL_BlitSurface", "GraphicsSystem::blitSurfaceToDC()");
  }

  markWrittenTo(dst);
}
void SDLSurface::blitFROMSurface(SDLSurface& src_surface,
                                 const Rect& src,
                                 const Rect& dst,
                                 int alpha,
                                 bool use_src_alpha) {
  blitFROMSurface(src_surface.RawSurface(), src, dst, alpha, use_src_alpha);
}

// -----------------------------------------------------------------------

static void determineProperties(SDL_Surface* surface,
                                bool is_mask,
                                GLenum& bytes_per_pixel,
                                GLint& byte_order,
                                GLint& byte_type) {
  bytes_per_pixel = SDL_BYTESPERPIXEL(surface->format);
  byte_type = GL_UNSIGNED_BYTE;

  switch (surface->format) {
    case SDL_PIXELFORMAT_ARGB8888:
    case SDL_PIXELFORMAT_XRGB8888:
      // OpenGL has no GL_ARGB format. GL_BGRA with the reversed byte type
      // reads a packed ARGB word correctly on each byte order.
      byte_order = GL_BGRA;
      byte_type = GL_UNSIGNED_INT_8_8_8_8_REV;
      break;
    case SDL_PIXELFORMAT_RGBA32:
      byte_order = GL_RGBA;
      break;
    default:
      throw std::runtime_error(
          std::format("Error loading texture: unsupported surface format {}",
                      SDL_GetPixelFormatName(surface->format)));
  }

  if (is_mask) {
    // Compile shader for use:
    bytes_per_pixel = GL_ALPHA;
  }
}

// -----------------------------------------------------------------------

void SDLSurface::uploadTextureIfNeeded() const {
  if (!texture_is_valid_) {
    if (textures_.size() == 0) {
      GLenum bytes_per_pixel;
      GLint byte_order, byte_type;
      determineProperties(surface_, is_mask_, bytes_per_pixel, byte_order,
                          byte_type);

      // ---------------------------------------------------------------------

      // Figure out the optimal way of splitting up the image.
      std::vector<int> x_pieces, y_pieces;
      x_pieces = segmentPicture(surface_->w);
      y_pieces = segmentPicture(surface_->h);

      int x_offset = 0;
      for (std::vector<int>::const_iterator it = x_pieces.begin();
           it != x_pieces.end(); ++it) {
        int y_offset = 0;
        for (std::vector<int>::const_iterator jt = y_pieces.begin();
             jt != y_pieces.end(); ++jt) {
          textures_.emplace_back(this, x_offset, y_offset, *it, *jt,
                                 bytes_per_pixel, byte_order, byte_type);

          y_offset += *jt;
        }

        x_offset += *it;
      }
    } else {
      // Reupload the textures without reallocating them.
      for (auto& it : textures_)
        it.reupload(this, dirty_rectangle_);
    }

    dirty_rectangle_ = Rect();
    texture_is_valid_ = true;
  }
}

// -----------------------------------------------------------------------

void SDLSurface::RenderToScreen(const Rect& src_rect,
                                const Rect& dst_rect,
                                int alpha) const {
  int op[] = {alpha, alpha, alpha, alpha};
  this->RenderToScreen(src_rect, dst_rect, op);
}

// -----------------------------------------------------------------------

void SDLSurface::RenderToScreenAsColorMask(const Rect& src,
                                           const Rect& dst,
                                           const RGBAColour& rgba,
                                           bool filter) const {
  uploadTextureIfNeeded();

  for (const auto& it : textures_) {
    auto src_rect = src, dst_rect = dst;
    LocalRect coordinate_system(it.x_, it.y_, it.w_, it.h_);
    if (!coordinate_system.intersectAndTransform(src_rect, dst_rect))
      continue;

    if (!filter) {  // subtractive color mask
      glRenderer().RenderColormask({it.gltexture, src_rect},
                                   {screen_, dst_rect}, rgba);
    } else {
      RenderingConfig config;
      config.mask_color = rgba;
      glRenderer().Render({it.gltexture, src_rect}, config,
                          {screen_, dst_rect});
    }
  }
}

// -----------------------------------------------------------------------

void SDLSurface::RenderToScreen(const Rect& src_rect,
                                const Rect& dst_rect,
                                const int opacity[4]) const {
  uploadTextureIfNeeded();

  for (const auto& it : textures_) {
    auto src = src_rect, dst = dst_rect;
    LocalRect coordinate_system(it.x_, it.y_, it.w_, it.h_);
    if (coordinate_system.intersectAndTransform(src, dst)) {
      RenderingConfig config;
      config.vertex_alpha =
          std::array<float, 4>{opacity[0] / 255.0f, opacity[1] / 255.0f,
                               opacity[2] / 255.0f, opacity[3] / 255.0f};
      glRenderer().Render({it.gltexture, src}, config, {screen_, dst});
    }
  }
}

// -----------------------------------------------------------------------

void SDLSurface::Fill(const RGBAColour& colour, const Rect& area) {
  Uint32 sdl_colour = SDL_MapSurfaceRGBA(surface_, colour.r(), colour.g(),
                                         colour.b(), colour.a());

  SDL_Rect rect = ToSDLRect(area);
  if (!SDL_FillSurfaceRect(surface_, &rect, sdl_colour))
    ThrowSDLError("SDL_FillSurfaceRect", "GraphicsSystem::wipe()");

  // If we are the main screen, then we want to update the screen
  markWrittenTo(area);
}

void SDLSurface::UpdateBGRA(std::span<const char> bgra, bool is_alpha_mask) {
  if (!surface_)
    throw std::runtime_error("SDLSurface::UpdateBGRA called on null surface");

  const int width = surface_->w;
  const int height = surface_->h;
  const std::size_t expected_size =
      static_cast<std::size_t>(width) * height * 4;
  if (bgra.size() != expected_size)
    throw std::runtime_error("SDLSurface::UpdateBGRA buffer size mismatch");

  is_mask_ = is_alpha_mask;

  if (SDL_MUSTLOCK(surface_) && !SDL_LockSurface(surface_))
    ThrowSDLError("SDL_LockSurface", "SDLSurface::UpdateBGRA()");

  const bool direct_copy =
      surface_->format == SDL_PIXELFORMAT_ARGB8888 ||
      (!is_alpha_mask && surface_->format == SDL_PIXELFORMAT_XRGB8888);

  auto* dst = static_cast<unsigned char*>(surface_->pixels);
  const auto* src = reinterpret_cast<const unsigned char*>(bgra.data());
  if (direct_copy) {
    for (int y = 0; y < height; ++y) {
      std::memcpy(dst + y * surface_->pitch,
                  src + static_cast<std::size_t>(y) * width * 4,
                  static_cast<std::size_t>(width) * 4);
    }
  } else {
    const int bpp = SDL_BYTESPERPIXEL(surface_->format);
    for (int y = 0; y < height; ++y) {
      unsigned char* row = dst + y * surface_->pitch;
      for (int x = 0; x < width; ++x) {
        const std::size_t src_idx =
            (static_cast<std::size_t>(y) * width + x) * 4;
        const Uint32 pixel = SDL_MapSurfaceRGBA(
            surface_, src[src_idx + 2], src[src_idx + 1], src[src_idx + 0],
            is_alpha_mask ? src[src_idx + 3] : 255);
        std::memcpy(row + x * bpp, &pixel, bpp);
      }
    }
  }

  if (SDL_MUSTLOCK(surface_))
    SDL_UnlockSurface(surface_);

  markWrittenTo(GetRect());
}

// -----------------------------------------------------------------------

void SDLSurface::Apply(std::function<RGBAColour(RGBAColour)> transformer,
                       Rect area) {
  SDL_Surface* surface = RawSurface();

  const SDL_PixelFormatDetails* format =
      SDL_GetPixelFormatDetails(surface->format);
  const SDL_Palette* palette = SDL_GetSurfacePalette(surface);
  const int bpp = SDL_BYTESPERPIXEL(surface->format);
  const int row_advance = surface->pitch - bpp * area.width();

  // determine position
  char* p_position = static_cast<char*>(surface->pixels);
  // offset by y
  p_position += (surface->pitch * area.y());
  // offset by x
  p_position += bpp * area.x();

  if (SDL_MUSTLOCK(surface))
    SDL_LockSurface(surface);
  {
    for (int y = 0; y < area.height(); ++y) {
      for (int x = 0; x < area.width(); ++x) {
        // copy pixel data
        Uint32 col;
        memcpy(&col, p_position, bpp);

        // Before someone tries to simplify the following four lines,
        // remember that sizeof(int) != sizeof(Uint8).
        Uint8 r, g, b, a;
        SDL_GetRGBA(col, format, palette, &r, &g, &b, &a);
        RGBAColour out = transformer(RGBAColour(r, g, b, a));
        assert(out.is_within_u8());
        col = SDL_MapRGBA(format, palette, static_cast<Uint8>(out.r()),
                          static_cast<Uint8>(out.g()),
                          static_cast<Uint8>(out.b()),
                          static_cast<Uint8>(out.a()));

        memcpy(p_position, &col, bpp);

        p_position += bpp;
      }

      // advance forward to next row, area.x() column
      p_position += row_advance;
    }
  }
  if (SDL_MUSTLOCK(surface))
    SDL_UnlockSurface(surface);

  // If we are the main screen, then we want to update the screen
  markWrittenTo(area);
}

// -----------------------------------------------------------------------

int SDLSurface::GetNumPatterns() const { return region_table_.size(); }

// -----------------------------------------------------------------------

const GrpRect& SDLSurface::GetPattern(int patt_no) const {
  if (patt_no < region_table_.size())
    return region_table_[patt_no];
  else
    return region_table_[0];
}

// -----------------------------------------------------------------------

std::shared_ptr<SDLSurface> SDLSurface::Clone() const {
  SDL_Surface* tmp_surface =
      SDL_CreateSurface(surface_->w, surface_->h, surface_->format);
  if (!tmp_surface)
    ThrowSDLError("SDL_CreateSurface", "SDLSurface::clone()");

  // Disable alpha blending because we're copying onto a blank (and
  // blank alpha!) surface
  SDL_SetSurfaceBlendMode(surface_, SDL_BLENDMODE_NONE);
  const bool ok = SDL_BlitSurface(surface_, NULL, tmp_surface, NULL);
  SDL_SetSurfaceBlendMode(surface_, SDL_BLENDMODE_BLEND);
  if (!ok)
    ThrowSDLError("SDL_BlitSurface", "SDLSurface::clone()");

  return std::make_shared<SDLSurface>(tmp_surface, region_table_);
}

// -----------------------------------------------------------------------

std::vector<int> SDLSurface::segmentPicture(int size_remainging) {
  int max_texture_size = GetMaxTextureSize();

  std::vector<int> output;
  while (size_remainging > max_texture_size) {
    output.push_back(max_texture_size);
    size_remainging -= max_texture_size;
  }

  if (IsNPOTSafe()) {
    output.push_back(size_remainging);
  } else {
    while (size_remainging) {
      int ss = SafeSize(size_remainging);
      if (ss > 512) {
        output.push_back(512);
        size_remainging -= 512;
      } else {
        output.push_back(size_remainging);
        size_remainging = 0;
      }
    }
  }

  return output;
}

// -----------------------------------------------------------------------

RGBAColour SDLSurface::GetPixelAt(Point pos) const {
  Uint32 col = 0;

  const int bpp = SDL_BYTESPERPIXEL(surface_->format);
  char* p_position = (char*)surface_->pixels;
  p_position += (surface_->pitch * pos.y());
  p_position += (bpp * pos.x());

  // Copy pixel data
  std::memcpy(&col, p_position, bpp);

  // Use SDL_GetRGBA to extract RGBA components
  Uint8 r, g, b, a;
  SDL_GetRGBA(col, SDL_GetPixelFormatDetails(surface_->format),
              SDL_GetSurfacePalette(surface_), &r, &g, &b, &a);
  return RGBAColour(r, g, b, a);
}

std::vector<char> SDLSurface::Dump(Rect region) const {
  auto x = region.x(), y = region.y();
  auto w = region.width(), h = region.height();

  const int bpp = SDL_BYTESPERPIXEL(surface_->format);
  std::vector<char> buf(bpp * w * h);
  char* dst = buf.data();
  if (SDL_MUSTLOCK(surface_)) {
    if (!SDL_LockSurface(surface_)) {
      throw std::runtime_error("Failed to lock the SDL_Surface: " +
                               std::string(SDL_GetError()));
    }
  }
  char* src = static_cast<char*>(surface_->pixels);
  src += surface_->pitch * (y + h - 1);
  int col_offset = bpp * x;
  int col_size = bpp * w;
  for (int row = 0; row < h; ++row) {
    std::memcpy(dst, src + col_offset, col_size);
    dst += col_size;
    src -= surface_->pitch;
  }
  if (SDL_MUSTLOCK(surface_)) {
    SDL_UnlockSurface(surface_);
  }
  return buf;
}

// -----------------------------------------------------------------------

std::shared_ptr<SDLSurface> SDLSurface::ClipAsColorMask(const Rect& clip_rect,
                                                     int r,
                                                     int g,
                                                     int b) const {
  const char* function_name = "GraphicsSystem::ClipAsColorMask()";

  // TODO(erg): This needs to be made exception safe and so does the rest
  // of this file.
  SDL_Surface* tmp_surface =
      SDL_CreateSurface(surface_->w, surface_->h, SDL_PIXELFORMAT_XRGB8888);

  if (!tmp_surface)
    ThrowSDLError("SDL_CreateSurface", function_name);

  SDL_SetSurfaceBlendMode(surface_, SDL_BLENDMODE_BLEND);
  if (!SDL_BlitSurface(surface_, NULL, tmp_surface, NULL))
    ThrowSDLError("SDL_BlitSurface", function_name);

  Uint32 colour = SDL_MapSurfaceRGB(tmp_surface, r, g, b);
  if (!SDL_SetSurfaceColorKey(tmp_surface, true, colour))
    ThrowSDLError("SDL_SetSurfaceColorKey", function_name);

  // The blit into the color-keyed copy removes the alpha channel. A second
  // blit copies the result into a standard surface and clips it to the given
  // rectangle.
  SDL_Surface* surface = buildNewSurface(clip_rect.size());
  SDL_Rect srcrect = ToSDLRect(clip_rect);
  if (!SDL_BlitSurface(tmp_surface, &srcrect, surface, NULL))
    ThrowSDLError("SDL_BlitSurface", function_name);

  SDL_DestroySurface(tmp_surface);

  return std::make_shared<SDLSurface>(surface);
}

// -----------------------------------------------------------------------

void SDLSurface::markWrittenTo(const Rect& written_rect) {
  // Mark that the texture needs reuploading
  dirty_rectangle_ = dirty_rectangle_.Union(written_rect);
  texture_is_valid_ = false;
}

std::vector<SDLSurface::TextureRecord> SDLSurface::GetTextureArray() const {
  uploadTextureIfNeeded();

  return textures_;
}
