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

#include "core/capture.hpp"

#include "core/compression.hpp"
#include "core/gameexe.hpp"
#include "encodings/utf16.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "utilities/byte_reader.hpp"
#include "utilities/file.hpp"

#include <SDL3/SDL.h>
#include <png.h>
#include <algorithm>
#include <array>
#include <boost/archive/archive_exception.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace fs = std::filesystem;

static void CheckSize(Size size) {
  if (size.width() <= 0 || size.height() <= 0 ||
      static_cast<std::uint64_t>(size.width()) * size.height() >
          std::numeric_limits<int>::max() / 4)
    throw std::runtime_error("Capture dimensions are invalid: " +
                             size.DebugString());
}

static std::string Lower(std::string str) {
  std::transform(str.begin(), str.end(), str.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return str;
}

static std::string ImageName(std::string name) {
  std::replace(name.begin(), name.end(), '\\', '/');
  return Lower(fs::path(name).stem().string());
}

static std::shared_ptr<SDLSurface> Resize(std::shared_ptr<SDLSurface> source,
                                          Size size) {
  CheckSize(size);
  auto result = std::make_shared<SDLSurface>(size);
  if (!SDL_BlitSurfaceScaled(source->RawSurface(), nullptr,
                             result->RawSurface(), nullptr,
                             SDL_SCALEMODE_LINEAR))
    throw std::runtime_error(SDL_GetError());
  // These snapshots model the legacy X8R8G8B8 render targets.
  result->Apply(
      [](RGBAColour c) { return RGBAColour(c.r(), c.g(), c.b(), 255); });
  return result;
}

static bool WritePNG(const fs::path& path, const SDLSurface& surface) {
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(
      SDL_ConvertSurface(surface.RawSurface(), SDL_PIXELFORMAT_RGBA32),
      SDL_DestroySurface);
  if (!rgba)
    return false;
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  image.width = rgba->w;
  image.height = rgba->h;
  image.format = PNG_FORMAT_RGBA;
  const bool ok = png_image_write_to_file(&image, path.string().c_str(), 0,
                                          rgba->pixels, rgba->pitch, nullptr);
  png_image_free(&image);
  return ok;
}

// ------------------------------------------------------------------------------

CaptureConfig CaptureConfig::Parse(Gameexe& gameexe, Size screen_size) {
  return {
      .save_thumbnail = gameexe("SAVE_THUMB.USE").Int().value_or(0) != 0,
      .thumbnail_size = Size(gameexe("SAVE_THUMB.SIZE").IntAt(0).value_or(200),
                             gameexe("SAVE_THUMB.SIZE").IntAt(1).value_or(150)),
      .png_thumbnail = gameexe("SAVE_THUMB.TYPE").Int().value_or(0) == 1,
      .screen_size = screen_size};
}

CaptureManager::CaptureManager(CaptureConfig config,
                               fs::path save_path,
                               Render render,
                               Load load,
                               LiveImages live_images,
                               Clock clock)
    : config_(config),
      save_path_(std::move(save_path)),
      render_(std::move(render)),
      load_(std::move(load)),
      live_images_(std::move(live_images)),
      clock_(std::move(clock)) {
  if (!clock_) {
    clock_ = +[] {
      return static_cast<int>(std::chrono::system_clock::to_time_t(
          std::chrono::system_clock::now()));
    };
  }
}

std::optional<CaptureRequest> CaptureManager::PrepareRequest(
    CaptureRequest request) const {
  if (request.kind == CaptureKind::Save &&
      (!config_.save_thumbnail || (save_ && request.priority < save_priority_)))
    return std::nullopt;

  if (request.kind == CaptureKind::Buffer) {
    if (!buffer_)
      throw std::runtime_error("Capture buffer has not been created");

    request.size = buffer_->GetSize();
    request.display = false;
  } else {
    if (!request.size.width())
      request.size.set_width(config_.screen_size.width());
    if (!request.size.height())
      request.size.set_height(config_.screen_size.height());
  }
  CheckSize(request.size);
  return request;
}

std::optional<int> CaptureManager::Capture(const CaptureRequest& request) {
  if (request.kind == CaptureKind::Save && save_ &&
      request.priority < save_priority_)
    return std::nullopt;

  std::shared_ptr<SDLSurface> surface;
  if (request.kind == CaptureKind::Save && live_images_) {
    for (const auto& image : live_images_()) {
      if (auto it = thumbnail_table_.find(ImageName(image));
          it != thumbnail_table_.end()) {
        surface = load_(it->second);
        break;
      }
    }
  }

  const bool predefined = static_cast<bool>(surface);
  if (!surface)
    surface = render_(request);
  if (!surface)
    throw std::runtime_error("Capture renderer returned no image");

  switch (request.kind) {
    case CaptureKind::Save:
      save_ = predefined ? surface : Resize(surface, config_.thumbnail_size);
      save_priority_ = request.priority;
      break;
    case CaptureKind::Object:
      object_ = std::move(surface);
      break;
    case CaptureKind::Tweet:
      tweet_ = std::move(surface);
      break;
    case CaptureKind::Local: {
      surface = Resize(surface, request.size);
      const int timestamp = clock_();
      fs::create_directories(save_path_);
      if (!WritePNG(FilePath(std::format("{:010}", timestamp), "png"),
                    *surface))
        throw std::runtime_error("Could not write local capture thumbnail");
      return timestamp;
    }
    case CaptureKind::Buffer:
      buffer_ = std::move(surface);
      if (!request.png_name.empty() &&
          !Export(FilePath(request.png_name, "png"), "png"))
        throw std::runtime_error("Could not export capture PNG");
      break;
  }
  return std::nullopt;
}

void CaptureManager::Reset() {
  save_.reset();
  object_.reset();
  tweet_.reset();
  buffer_.reset();
  save_priority_ = 0;
}

void CaptureManager::Release(CaptureKind kind, int priority) {
  switch (kind) {
    case CaptureKind::Save:
      if (priority >= save_priority_) {
        save_.reset();
        save_priority_ = 0;
      }
      break;
    case CaptureKind::Object:
      object_.reset();
      break;
    case CaptureKind::Tweet:
      tweet_.reset();
      break;
    default:
      throw std::runtime_error("Invalid capture cache");
  }
}

std::shared_ptr<SDLSurface> CaptureManager::Snapshot(CaptureKind kind) const {
  switch (kind) {
    case CaptureKind::Save:
      return save_;
    case CaptureKind::Object:
      return object_;
    case CaptureKind::Tweet:
      return tweet_;
    case CaptureKind::Buffer:
      return buffer_;
    default:
      return {};
  }
}

void CaptureManager::FromFile(const fs::path& filename) {
  if (!config_.save_thumbnail)
    return;
  auto surface = load_(filename);
  if (!surface)
    throw std::runtime_error("Could not load capture image: " +
                             filename.string());
  if (surface->GetNumPatterns() > 0) {
    const auto& pattern = surface->GetPattern(0);
    auto first = std::make_shared<SDLSurface>(pattern.rect.size());
    surface->BlitToSurface(*first, pattern.rect, first->GetRect(), 255, false);
    surface = std::move(first);
  }
  save_ = std::move(surface);
}

void CaptureManager::CreateBuffer(Size size) {
  CheckSize(size);
  auto buffer = std::make_shared<SDLSurface>(size);
  buffer->Fill(RGBAColour(0, 0, 0, 0));
  buffer_ = std::move(buffer);
}
void CaptureManager::DestroyBuffer() { buffer_.reset(); }

std::shared_ptr<SDLSurface> CaptureManager::LoadThumbnail(int number,
                                                          bool local) {
  std::string filename =
      local ? std::format("{:010}", number) : std::format("{:04}", number);
  for (const char* extension : config_.png_thumbnail
                                   ? std::array{"png", "bmp"}
                                   : std::array{"bmp", "png"}) {
    auto path = CorrectPath(save_path_, filename + '.' + extension);
    if (!path.empty())
      return load_(path);
  }
  return {};
}

void CaptureManager::LoadThumbnailTable(const fs::path& path) {
  std::string bytes = LoadFileStr(path);
  if (bytes.size() < 12)
    throw std::runtime_error("Truncated thumbnail table");
  ByteReader header(bytes);
  const auto header_size = header.PopAs<std::uint32_t>(4);
  header.Proceed(4);
  const auto count = header.PopAs<std::uint32_t>(4);
  if (header_size < 12 || header_size > bytes.size())
    throw std::runtime_error("Invalid thumbnail table header");

  auto data = Decompress_lzss(std::string_view(bytes).substr(header_size));
  auto PopUTF16 = [](std::string_view bytes,
                     std::size_t& position) -> std::string {
    const auto begin = position;
    while (position + 1 < bytes.size()) {
      position += 2;
      if (bytes[position - 2] == 0 && bytes[position - 1] == 0)
        return utf16le::Decode(bytes.substr(begin, position - begin - 2));
    }
    throw std::runtime_error("Unterminated capture string");
  };

  std::map<std::string, std::string> table;
  std::size_t position = 0;
  for (std::uint32_t i = 0; i < count; ++i) {
    auto image = PopUTF16(data, position);
    auto thumbnail = PopUTF16(data, position);
    table.emplace(ImageName(image), std::move(thumbnail));
  }
  thumbnail_table_ = std::move(table);
}

fs::path CaptureManager::FilePath(std::string name,
                                  std::string extension) const {
  std::replace(name.begin(), name.end(), '\\', '/');
  return save_path_ / (name + '.' + Lower(extension));
}

bool CaptureManager::Export(const fs::path& path,
                            std::string extension,
                            const CaptureFlags& flags) const {
  if (!buffer_)
    throw std::runtime_error("Capture buffer has not been created");
  extension = Lower(extension);
  if (extension != "bmp" && extension != "png")
    throw std::runtime_error("Capture format must be bmp or png");
  if (extension == "png" && !flags.empty())
    throw std::runtime_error("PNG captures cannot contain flag data");
  std::error_code error;
  if (!path.parent_path().empty())
    fs::create_directories(path.parent_path(), error);
  if (error)
    return false;
  if (extension == "png")
    return WritePNG(path, *buffer_);

  // Write a 24-bit BMP for the image decoder. bfSize excludes the appended
  // capture flags, which use an RLVM-specific Boost text archive. Backward
  // compatibility with the original Siglus serialization format is out of
  // scope and will not be supported.
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgb(
      SDL_ConvertSurface(buffer_->RawSurface(), SDL_PIXELFORMAT_BGR24),
      SDL_DestroySurface);
  if (!rgb || !SDL_SaveBMP(rgb.get(), path.string().c_str()))
    return false;
  std::ofstream file(path, std::ios::binary | std::ios::app);
  if (!file)
    return false;
  try {
    boost::archive::text_oarchive archive(file);
    archive << flags.integers << flags.strings;
  } catch (const boost::archive::archive_exception&) {
    return false;
  }
  return static_cast<bool>(file);
}

CaptureFlags CaptureManager::ImportFlags(const fs::path& path) {
  auto bytes = LoadFileStr(path);
  if (bytes.size() < 14 || bytes.substr(0, 2) != "BM")
    throw std::runtime_error("Capture flag import requires a BMP file");
  const auto image_size =
      ByteReader(std::string_view(bytes).substr(2)).PopAs<std::uint32_t>(4);
  if (image_size < 14 || image_size > bytes.size())
    throw std::runtime_error("Invalid capture BMP size");
  if (image_size == bytes.size())
    return {};

  CaptureFlags result;
  std::istringstream file(bytes.substr(image_size));
  boost::archive::text_iarchive archive(file);
  archive >> result.integers >> result.strings;
  if (!file)
    throw std::runtime_error("Truncated capture flag archive");
  return result;
}
