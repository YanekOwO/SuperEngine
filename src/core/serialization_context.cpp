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

#include "core/serialization_context.hpp"

#include "core/capture.hpp"
#include "core/game_config.hpp"
#include "core/gameexe.hpp"
#include "core/image_resources.hpp"
#include "core/object_internal/drawer/string.hpp"
#include "systems/itext_system.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"
#include "utilities/clock.hpp"

#include <stdexcept>

void* SerializationContext::Key() {
  static char key;
  return &key;
}

std::shared_ptr<Clock> SerializationContext::GetClock() const {
  return clock ? clock : std::make_shared<Clock>();
}

std::shared_ptr<SDLSurface> SerializationContext::LoadSurface(
    const std::string& name) const {
  if (name.empty())
    throw std::runtime_error("Saved image requires a filename");
  if (!images)
    throw std::runtime_error("Saved image requires image resources");
  if (auto it = surface_cache.find(name); it != surface_cache.end())
    return it->second;
  auto surface = images->LoadSurfaceFromFile(name);
  if (!surface)
    throw std::runtime_error("Could not reconstruct saved image: " + name);
  surface_cache.emplace(name, surface);
  return surface;
}

std::shared_ptr<SDLSurface> SerializationContext::LoadThumbnail(
    int number,
    bool local) const {
  if (!captures)
    throw std::runtime_error("Saved thumbnail requires capture resources");
  return captures->LoadThumbnail(number, local);
}

StringObjectRenderContext SerializationContext::StringRenderer() const {
  const auto services = *this;
  return {
      .rasterize =
          [services](const std::string& text, int size, RGBColour color) {
            if (!services.system)
              throw std::runtime_error("Text rendering requires System");
            auto& renderer = services.system->text();
            return renderer.impl().RenderText(
                text,
                FontFace{.font = renderer.LoadFont(size), .is_shadow = false},
                color);
          },
      .color =
          [services](int index) {
            if (services.system) {
              auto color =
                  services.system->gameexe()("COLOR_TABLE", index).IntVec();
              if (color && color->size() >= 3)
                return RGBColour((*color)[0], (*color)[1], (*color)[2]);
            }
            return index == 1 ? RGBColour::Black() : RGBColour::White();
          },
      .font_settings =
          [services] {
            const auto* local = services.system
                                    ? &services.system->text().FontConfig()
                                    : services.local_config;
            if (!local || !services.global_config)
              return StringObjectFontSettings{};
            return StringObjectFontSettings{
                EffectiveFontName(*local, *services.global_config),
                EffectiveFontBold(*local, *services.global_config),
                EffectiveFontShadow(*local, *services.global_config)};
          }};
}

std::string SerializationContext::SavePath(const std::filesystem::path& path,
                                           bool& relative) const {
  relative = false;
  if (!path.empty() && !game_root.empty()) {
    auto local = path.lexically_relative(game_root);
    if (!local.empty() && *local.begin() != "..") {
      relative = true;
      return local.generic_string();
    }
  }
  return path.string();
}

std::filesystem::path SerializationContext::LoadPath(const std::string& path,
                                                     bool relative) const {
  return relative ? game_root / path : std::filesystem::path(path);
}
