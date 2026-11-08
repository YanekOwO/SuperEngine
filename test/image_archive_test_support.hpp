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

#include "core/image_resources.hpp"
#include "mock_asset_scanner.hpp"
#include "mock_graphics_backend.hpp"
#include "systems/sdl/sdl_surface.hpp"

#include <SDL3/SDL.h>
#include <gmock/gmock.h>
#include <functional>

class ImageArchiveHost {
 public:
  ImageArchiveHost() {
    ON_CALL(*scanner, FindFile(testing::_, testing::_))
        .WillByDefault([this](std::string name, const auto&) {
          return expected<std::filesystem::path, std::runtime_error>(
              root / (name + ".g00"));
        });
    ON_CALL(*backend, LoadSurface(testing::_))
        .WillByDefault([this](const std::filesystem::path& path) {
          ++loads;
          loaded_path = path;
          if (fail)
            throw std::runtime_error("Test image is unavailable");
          if (make_surface)
            return make_surface();
          auto* raw = SDL_CreateSurface(size.width(), size.height(),
                                        SDL_PIXELFORMAT_RGBA32);
          if (!raw)
            throw std::runtime_error(SDL_GetError());
          SDL_FillSurfaceRect(raw, nullptr,
                              SDL_MapSurfaceRGBA(raw, color.r(), color.g(),
                                                 color.b(), color.a()));
          return std::make_shared<SDLSurface>(raw, regions);
        });
  }
  std::shared_ptr<testing::NiceMock<MockAssetScanner>> scanner =
      std::make_shared<testing::NiceMock<MockAssetScanner>>();
  std::shared_ptr<testing::NiceMock<MockGraphicsBackend>> backend =
      std::make_shared<testing::NiceMock<MockGraphicsBackend>>();
  ImageResources images{scanner, backend, ToneCurve{}};
  RGBAColour color{12, 34, 56, 78};
  Size size{2, 2};
  std::vector<GrpRect> regions;
  std::filesystem::path root = "images", loaded_path;
  std::function<std::shared_ptr<SDLSurface>()> make_surface;
  int loads = 0;
  bool fail = false;
};
