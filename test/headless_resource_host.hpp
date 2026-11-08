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

#include "core/gameexe.hpp"
#include "core/mwnd_config.hpp"
#include "core/selection.hpp"
#include "core/serialization_context.hpp"
#include "core/stage.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"

#include <SDL3/SDL.h>
#include <optional>
#include <string>

class ScopedDummyDrivers {
 public:
  ScopedDummyDrivers() {
    if (const auto* value = SDL_GetHint(SDL_HINT_VIDEO_DRIVER))
      video_ = value;
    if (const auto* value = SDL_GetHint(SDL_HINT_AUDIO_DRIVER))
      audio_ = value;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
  }
  ~ScopedDummyDrivers() {
    Restore(SDL_HINT_VIDEO_DRIVER, video_);
    Restore(SDL_HINT_AUDIO_DRIVER, audio_);
  }

 private:
  static void Restore(const char* key,
                      const std::optional<std::string>& value) {
    if (value)
      SDL_SetHint(key, value->c_str());
    else
      SDL_ResetHint(key);
  }
  std::optional<std::string> video_, audio_;
};

class HeadlessResourceHost {
 public:
  HeadlessResourceHost() {
    for (const char* line :
         {"SCREEN_SIZE = 640,480", "MWND.CNT = 1", "MWND.0.MOJI_CNT = 42,4",
          "MWND.0.MOJI_SIZE = 20", "WAKU.CNT = 0", "WAKU.BTN.CNT = 0"})
      gameexe.parseLine(line);
    system = std::make_unique<System>(gameexe, stage, local, global, scanner,
                                      MwndConfig::ParseSiglus(gameexe));
    system->text().BindGraphics({.request_refresh = [] {}});
  }
  ~HeadlessResourceHost() {
    // Pending glyphs must release their fonts before the text backend shuts
    // down.
    stage.Reset();
    system->text().Reset();
  }
  SerializationContext Context() {
    return {.system = system.get(),
            .local_config = &local,
            .global_config = &global};
  }
  ScopedDummyDrivers drivers;
  Gameexe gameexe;
  Stage stage;
  LocalConfig local;
  GlobalConfig global;
  std::shared_ptr<AssetScanner> scanner = std::make_shared<AssetScanner>();
  std::unique_ptr<System> system;
};
