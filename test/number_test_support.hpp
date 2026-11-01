// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
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

#include <SDL3/SDL.h>

#include "systems/sdl/sdl_surface.hpp"

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

inline std::shared_ptr<SDLSurface> MakeNumberTestAtlas(int count = 26) {
  std::vector<GrpRect> cuts;
  for (int i = 0; i < count; ++i) {
    cuts.push_back({.rect = Rect::REC(i * 16, 0, 10 + i % 3, 14 + i % 2),
                    .originX = 2,
                    .originY = 3});
  }
  auto* raw = SDL_CreateSurface(count * 16, 16, SDL_PIXELFORMAT_ARGB8888);
  if (!raw)
    throw std::runtime_error(SDL_GetError());
  auto atlas = std::make_shared<SDLSurface>(raw, std::move(cuts));
  atlas->Fill(RGBAColour::Clear());
  for (int i = 0; i < count; ++i)
    atlas->Fill(RGBAColour(i + 1, 100, 200, 255), atlas->GetPattern(i).rect);
  return atlas;
}
