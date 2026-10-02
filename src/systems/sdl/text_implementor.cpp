// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
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
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
//
// -----------------------------------------------------------------------

#include "systems/sdl/text_implementor.hpp"

#include "log/domain_logger.hpp"
#include "systems/sdl/sdl_surface.hpp"

#include <SDL3_ttf/SDL_ttf.h>

#include <cassert>
#include <string>

using std::string_literals::operator""s;

namespace {
static SDL_Color ToSDLColor(const RGBColour& in) {
  SDL_Color out;
  out.r = in.r();
  out.g = in.g();
  out.b = in.b();
  out.a = 255;
  return out;
}
}  // namespace

static DomainLogger logger("SDLText");

struct TTFFont final : public IFont {
  TTFFont(std::shared_ptr<TTF_Font> f, int s)
      : is_monospace(false), ttf_font(f), size(s) {
    is_monospace = GetCharWidth('i') == GetCharWidth('m');
  }

  bool IsMonospace() const override { return is_monospace; }
  int GetCharWidth(uint16_t codepoint) override {
    TTF_Font* f = ttf_font.get();
    int minx, maxx, miny, maxy, advance = 0;
    if (!TTF_GetGlyphMetrics(f, codepoint, &minx, &maxx, &miny, &maxy,
                             &advance))
      return 0;
    return advance;
  }
  int GetSize() const override { return size; }

  bool is_monospace;
  std::shared_ptr<TTF_Font> ttf_font;
  int size;
};

// -----------------------------------------------------------------------
// class SDLTextImpl
void SDLTextImpl::InitSystem() {
  if (!TTF_Init())
    throw std::runtime_error("Error initializing SDL_ttf: "s + SDL_GetError());
}

void SDLTextImpl::QuitSystem() { TTF_Quit(); }

std::shared_ptr<IFont> SDLTextImpl::GetFont(
    const std::filesystem::path& font_file,
    int font_size) {
  TTF_Font* f =
      TTF_OpenFont(font_file.native().c_str(), static_cast<float>(font_size));
  if (f == nullptr)
    throw std::runtime_error("Error loading font: "s + SDL_GetError());

  TTF_SetFontStyle(f, TTF_STYLE_NORMAL);

  std::shared_ptr<TTF_Font> font(f, TTF_CloseFont);

  return std::make_shared<TTFFont>(font, font_size);
}

Size SDLTextImpl::RenderGlyphOnto(const std::string& text,
                                  FontFace font,
                                  RGBColour font_color,
                                  std::optional<RGBColour> shadow_color,
                                  Point insertion,
                                  std::shared_ptr<SDLSurface> dst) {
  SDLSurface* sdl_surface = dst.get();
  std::shared_ptr<TTFFont> ttf_font =
      std::dynamic_pointer_cast<TTFFont>(font.font);
  if (!ttf_font || !ttf_font->ttf_font)
    throw std::runtime_error("RenderGlyph: ttf font is nullptr");
  TTF_Font* f = ttf_font->ttf_font.get();

  if (font.is_italic) {
    TTF_SetFontStyle(f, TTF_STYLE_ITALIC);
  }

  std::shared_ptr<SDL_Surface> character(
      TTF_RenderText_Blended(f, text.c_str(), 0, ToSDLColor(font_color)),
      SDL_DestroySurface);

  if (character == nullptr) {
    // Bug during Kyou's path. The string is printed "". Regression in parser?
    logger(Severity::Warn)
        << "TTF_RenderText_Blended didn't render the character '" << text
        << "'. Hopefully continuing...";
    return Size(0, 0);
  }

  std::shared_ptr<SDL_Surface> shadow = nullptr;
  if (shadow_color) {
    SDL_Color sdl_shadow_color = ToSDLColor(*shadow_color);
    shadow.reset(TTF_RenderText_Blended(f, text.c_str(), 0, sdl_shadow_color),
                 SDL_DestroySurface);
  }

  // reset the font back to normal
  if (font.is_italic) {
    TTF_SetFontStyle(f, TTF_STYLE_NORMAL);
  }

  constexpr int alpha = 255;
  if (shadow) {
    Size offset(shadow->w, shadow->h);
    sdl_surface->blitFROMSurface(shadow.get(), Rect(Point(0, 0), offset),
                                 Rect(insertion + Point(2, 2), offset), alpha);
  }

  Size size(character->w, character->h);
  sdl_surface->blitFROMSurface(character.get(), Rect(Point(0, 0), size),
                               Rect(insertion, size), alpha);

  return size;
}

std::shared_ptr<SDLSurface> SDLTextImpl::RenderText(
    const std::string& text /* utf8 */,
    FontFace font,
    RGBColour c) {
  SDL_Color color = ToSDLColor(c);
  TTF_Font* f = static_cast<TTF_Font*>(get_ttf(font.font));
  assert(f != nullptr);
  SDL_Surface* tmp = TTF_RenderText_Blended(f, text.c_str(), 0, color);
  return std::make_shared<SDLSurface>(tmp);
}

// ------------------------------------------------------------------------------

void* get_ttf(std::shared_ptr<IFont> font) {
  if (!font)
    return nullptr;
  std::shared_ptr<TTFFont> ttf_font = std::dynamic_pointer_cast<TTFFont>(font);
  if (!ttf_font)
    return nullptr;
  return ttf_font->ttf_font.get();
}
