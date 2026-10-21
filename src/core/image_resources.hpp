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

#pragma once

#include "core/tone_curve.hpp"
#include "lru_cache.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class AssetScanner;
class IGraphicsBackend;
class SDLSurface;

// Shares decoded images and script preloads across an engine's consumers.
// loaded_images() observes resources without retaining them.
class ImageResources {
 public:
  ImageResources(std::shared_ptr<AssetScanner> scanner,
                 std::shared_ptr<IGraphicsBackend> backend,
                 ToneCurve tone_curves);
  std::shared_ptr<SDLSurface> GetSurfaceNamed(const std::string& name);
  std::shared_ptr<SDLSurface> FindCached(const std::string& name);
  std::shared_ptr<SDLSurface> LoadSurfaceFromFile(const std::string& name);
  void PreloadImage(std::size_t slot, const std::string& name);
  void ClearPreloadedImage(std::size_t slot);
  void ClearPreloadedImages();
  std::shared_ptr<SDLSurface> FindPreloaded(const std::string& name) const;
  inline const auto& loaded_images() const { return loaded_images_; }

 private:
  std::shared_ptr<AssetScanner> asset_scanner_;
  std::shared_ptr<IGraphicsBackend> impl_;
  ToneCurve tone_curves_;
  LRUCache<std::string, std::shared_ptr<SDLSurface>> image_cache_{64};
  std::vector<std::pair<std::string, std::shared_ptr<SDLSurface>>>
      preloaded_images_;
  std::vector<std::pair<std::string, std::weak_ptr<SDLSurface>>> loaded_images_;
};
