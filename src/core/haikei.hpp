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

#include "core/hik.hpp"
#include "core/rect.hpp"
#include "utilities/clock.hpp"
#include "utilities/lazy_array.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>

class SDLSurface;
class ImageResources;

// Which type of mutually exclusive background should we display?
enum GraphicsBackgroundType { BACKGROUND_DC0, BACKGROUND_HIK };

// Owns the RealLive background state: display contexts, the haikei backing
// surface, and optional HIK animation state.
class Haikei {
 public:
  using SurfaceFactory =
      std::function<std::shared_ptr<SDLSurface>(Size screen_size)>;
  Haikei(Size screen_size,
         SurfaceFactory surface_factory,
         std::shared_ptr<ImageResources> images,
         std::shared_ptr<Clock> clock);
  ~Haikei();

  GraphicsBackgroundType background_type() const { return background_type_; }
  void set_graphics_background(GraphicsBackgroundType t) {
    background_type_ = t;
  }

  void Reset();

  std::shared_ptr<SDLSurface> GetHaikei();

  void AllocateDC(int dc, Size screen_size);
  void SetMinimumSizeForDC(int dc, Size size);
  void FreeDC(int dc);
  std::shared_ptr<SDLSurface> GetDC(int dc);
  void ClearAllDCs();

  inline HIKRenderer* hik_renderer() const { return hik_renderer_.get(); }
  void SetHikRenderer(HIKRenderer* renderer);
  void ClearHikRenderer();
  void LoadHikRenderer(const std::string& name,
                       const std::filesystem::path& file);

  std::shared_ptr<HIKScript> LoadHikFile(const std::filesystem::path& file);
  void PreloadHIKScript(int slot,
                        const std::string& name,
                        const std::filesystem::path& file);
  void ClearPreloadedHIKScript(int slot);
  void ClearAllPreloadedHIKScripts();
  std::shared_ptr<HIKScript> GetHIKScript(const std::string& name,
                                          const std::filesystem::path& file);

 private:
  using HIKArrayItem = std::pair<std::string, std::shared_ptr<HIKScript>>;
  using HIKScriptList = LazyArray<HIKArrayItem>;

  Size screen_size_;
  SurfaceFactory surface_factory_;
  std::shared_ptr<ImageResources> images_;
  std::shared_ptr<Clock> clock_;

  GraphicsBackgroundType background_type_;
  HIKScriptList preloaded_hik_scripts_;
  std::unique_ptr<HIKRenderer> hik_renderer_;
  std::shared_ptr<SDLSurface> haikei_;
  std::shared_ptr<SDLSurface> display_contexts_[16];
};
