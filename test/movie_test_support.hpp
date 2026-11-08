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

#include "core/object.hpp"
#include "core/object_internal/drawer/movie.hpp"
#include "mock_clock.hpp"
#include "mock_graphics_backend.hpp"
#include "test_utils.hpp"

#include <SDL3/SDL.h>

class MovieTestHost {
 public:
  MovieTestHost() {
    ON_CALL(*backend, CreateSurfaceBGRA(testing::_, testing::_, testing::_))
        .WillByDefault([](Size size, std::span<char> pixels, bool alpha) {
          auto image = std::make_shared<SDLSurface>(size);
          image->UpdateBGRA(pixels, alpha);
          return image;
        });
  }
  std::unique_ptr<GraphicsObjectData> Make(bool loop = true,
                                           bool auto_free = true,
                                           bool paused = false) {
    return std::make_unique<ObjectMovieData>(path, loop, auto_free, true,
                                             paused, backend, clock);
  }
  SerializationContext Context() {
    return {.clock = clock, .backend = backend};
  }
  std::filesystem::path path = PathToTestCase("Gameroot/movie/two_frames.omv");
  std::shared_ptr<MockClock> clock = std::make_shared<MockClock>();
  std::shared_ptr<testing::NiceMock<MockGraphicsBackend>> backend =
      std::make_shared<testing::NiceMock<MockGraphicsBackend>>();
};
