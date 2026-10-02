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
// -----------------------------------------------------------------------

#include "test_system/sdl_env.hpp"

#include "core/rect.hpp"
#include "systems/sdl/gl_utils.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>

sdlEnv::sdlEnv(Size screen) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    throw std::runtime_error(std::string("Failed to setup sdl video: ") +
                             SDL_GetError());
  }

  window_ = SDL_CreateWindow("rlvm test", screen.width(), screen.height(),
                             SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  if (!window_) {
    std::string error = std::string("Failed to create window: ") +
                        SDL_GetError();
    SDL_Quit();
    throw std::runtime_error(error);
  }

  SDL_GLContext context = SDL_GL_CreateContext(window_);
  if (!context) {
    std::string error = std::string("Failed to create GL context: ") +
                        SDL_GetError();
    SDL_DestroyWindow(window_);
    SDL_Quit();
    throw std::runtime_error(error);
  }
  gl_context_ = context;

  try {
    InitGLEW();
  } catch (...) {
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window_);
    SDL_Quit();
    throw;
  }
}

sdlEnv::~sdlEnv() {
  SDL_GL_DestroyContext(static_cast<SDL_GLContext>(gl_context_));
  SDL_DestroyWindow(window_);
  SDL_Quit();
}

std::shared_ptr<sdlEnv> SetupSDL(Size screen) {
  static std::weak_ptr<sdlEnv> cached;
  std::shared_ptr<sdlEnv> env = cached.lock();
  if (env)
    return env;

  cached = env = std::make_shared<sdlEnv>(screen);
  return env;
}
