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

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

class Clock;
class CaptureManager;
class System;
class ImageResources;
class IGraphicsBackend;
class SDLSurface;
struct LocalConfig;
struct GlobalConfig;
struct ObjectParameter;
struct StringObjectRenderContext;

// Archive-local reconstruction services. This contains no saved game state;
// each resource owns its Boost serialization and the archive tracks pointers.
struct SerializationContext {
  using Commit = std::function<void()>;
  using DeferCommit = std::function<void(Commit)>;
  using ObjectSetter = std::function<void(ObjectParameter&, int)>;
  System* system = nullptr;
  ImageResources* images = nullptr;
  CaptureManager* captures = nullptr;
  std::shared_ptr<Clock> clock;
  std::shared_ptr<IGraphicsBackend> backend;
  std::filesystem::path game_root;
  const LocalConfig* local_config = nullptr;
  const GlobalConfig* global_config = nullptr;
  std::function<ObjectSetter(const std::string&, int)> object_setter;

  // A staged loader retains installation until its surrounding load succeeds.
  DeferCommit defer_commit;

  // Reconstructed images are shared within an archive, outside the live cache.
  mutable std::map<std::string, std::shared_ptr<SDLSurface>> surface_cache;

  std::shared_ptr<Clock> GetClock() const;
  std::shared_ptr<SDLSurface> LoadSurface(const std::string& name) const;
  std::shared_ptr<SDLSurface> LoadThumbnail(int number, bool local) const;
  StringObjectRenderContext StringRenderer() const;
  std::string SavePath(const std::filesystem::path& path, bool& relative) const;
  std::filesystem::path LoadPath(const std::string& path, bool relative) const;

  // A nonzero key keeps this helper separate from Boost's shared_ptr helper.
  static void* Key();
  inline static SerializationContext& Get(auto& archive) {
    return archive.template get_helper<SerializationContext>(Key());
  }
  inline static void Init(auto& archive, SerializationContext ctx) {
    ctx.surface_cache.clear();
    Get(archive) = std::move(ctx);
  }
};
