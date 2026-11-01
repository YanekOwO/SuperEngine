// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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
//
// -----------------------------------------------------------------------

#pragma once

#include "systems/igraphics_backend.hpp"
#include "systems/sdl/sdl_surface.hpp"

#include <gmock/gmock.h>

#include <filesystem>
#include <memory>

class MockGraphicsBackend : public IGraphicsBackend {
 public:
  MOCK_METHOD(void, InitSystem, (Size, bool), (override));
  MOCK_METHOD(void, QuitSystem, (), (override));
  MOCK_METHOD(Size, Resize, (Size, bool), (override));
  MOCK_METHOD(std::shared_ptr<SDLSurface>, CreateSurface, (Size), (override));
  MOCK_METHOD(std::shared_ptr<SDLSurface>,
              CreateSurfaceBGRA,
              (Size, std::span<char>, bool),
              (override));
  MOCK_METHOD(std::shared_ptr<SDLSurface>,
              LoadSurface,
              (const std::filesystem::path&),
              (override));
  MOCK_METHOD(std::shared_ptr<Album>,
              LoadAlbum,
              (const std::filesystem::path&),
              (override));
  MOCK_METHOD(void, SetWindowTitle, (const std::string&), (override));
  MOCK_METHOD(void, ShowSystemCursor, (bool), (override));
  MOCK_METHOD(void, WarpMouse, (Point), (override));
  MOCK_METHOD(void,
              RenderFrame,
              (const RenderFrameConfig&,
               const DrawCallback&,
               const DrawCallback&),
              (override));
  MOCK_METHOD(bool,
              RedrawLastFrame,
              (const RenderFrameConfig&, const DrawCallback&),
              (override));
  MOCK_METHOD(std::shared_ptr<SDLSurface>,
              RenderToSurface,
              (const RenderFrameConfig&, const DrawCallback&),
              (override));
};
