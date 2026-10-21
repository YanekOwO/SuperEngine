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

#include "core/image_resources.hpp"

#include "core/asset_scanner.hpp"
#include "systems/igraphics_backend.hpp"
#include "systems/sdl/sdl_surface.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <set>
#include <stdexcept>
#include <utility>

ImageResources::ImageResources(std::shared_ptr<AssetScanner> scanner,
                               std::shared_ptr<IGraphicsBackend> backend,
                               ToneCurve tone_curves)
    : asset_scanner_(std::move(scanner)),
      impl_(std::move(backend)),
      tone_curves_(std::move(tone_curves)) {
  if (!asset_scanner_ || !impl_)
    throw std::invalid_argument(
        "ImageResources requires an asset scanner and backend");
}

std::shared_ptr<SDLSurface> ImageResources::FindCached(
    const std::string& name) {
  return image_cache_.fetch(name);
}

std::shared_ptr<SDLSurface> ImageResources::LoadSurfaceFromFile(
    const std::string& short_filename) {
  static const std::set<std::string> IMAGE_FILETYPES = {"g00", "pdt"};
  auto pth = asset_scanner_->FindFile(short_filename, IMAGE_FILETYPES);

  if (!pth.has_value())
    throw pth.error();

  if (pth->empty())
    throw std::runtime_error(
        std::format("Could not load image file '{}'", short_filename));

  std::shared_ptr<SDLSurface> result = impl_->LoadSurface(*pth);
  std::erase_if(loaded_images_,
                [](const auto& entry) { return entry.second.expired(); });
  loaded_images_.emplace_back(short_filename, result);

  // handle tone curve effect loading
  if (auto pos = short_filename.find("?"); pos != short_filename.npos) {
    auto effect_str = std::string_view(short_filename).substr(pos + 1);
    int effect_no = 0;
    if (auto [ptr, ec] =
            std::from_chars(effect_str.begin(), effect_str.end(), effect_no);
        ec != std::errc{} || ptr != effect_str.end()) {
      throw std::runtime_error(
          std::format("Invalid tone curve query '{}'", effect_str));
    }

    // the effect number is an index that goes from 10 to GetEffectCount() * 10,
    // so keep that in mind here
    if ((effect_no / 10) > tone_curves_.GetEffectCount() || effect_no < 10)
      throw std::runtime_error(
          std::format("Tone curve index {} is invalid.", effect_no));

    result->Apply(
        [effect = tone_curves_.GetEffect(effect_no / 10 - 1)](RGBAColour c) {
          c.set_red(effect[0][c.r()]);
          c.set_green(effect[1][c.g()]);
          c.set_blue(effect[2][c.b()]);
          return c;
        });
  }

  return result;
}

std::shared_ptr<SDLSurface> ImageResources::GetSurfaceNamed(
    const std::string& name) {
  if (auto image = FindPreloaded(name))
    return image;
  if (auto image = FindCached(name))
    return image;
  auto image = LoadSurfaceFromFile(name);
  image_cache_.insert(name, image);
  return image;
}

void ImageResources::PreloadImage(std::size_t slot, const std::string& name) {
  if (slot >= preloaded_images_.max_size())
    throw std::out_of_range("Image preload slot out of range");
  auto image = FindCached(name);
  if (!image)
    image = LoadSurfaceFromFile(name);
  if (slot >= preloaded_images_.size())
    preloaded_images_.resize(slot + 1);
  preloaded_images_[slot] = {name, std::move(image)};
}

void ImageResources::ClearPreloadedImage(std::size_t slot) {
  if (slot < preloaded_images_.size())
    preloaded_images_[slot] = {};
}

void ImageResources::ClearPreloadedImages() { preloaded_images_.clear(); }

std::shared_ptr<SDLSurface> ImageResources::FindPreloaded(
    const std::string& name) const {
  for (const auto& [filename, image] : preloaded_images_)
    if (image && filename == name)
      return image;
  return nullptr;
}
