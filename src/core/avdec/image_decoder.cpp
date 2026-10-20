// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
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

#include "core/avdec/image_decoder.hpp"

#include <png.h>
#include "xclannad/file.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>

void saveBGRAasPPM(std::ostream& os,
                   int width,
                   int height,
                   const std::vector<char>& bgra) {
  // Check that we have exactly width*height*4 bytes
  if (bgra.size() != static_cast<size_t>(width) * height * 4) {
    throw std::runtime_error("RGBA buffer has wrong size");
  }

  if (!os) {
    throw std::runtime_error("Failed to open output file");
  }

  // PPM header for binary P6 format
  os << "P6\n"
     << width << " " << height << "\n"
     << "255\n";

  // Write pixel data: for each pixel, output R, G, B (skip A)
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const size_t idx = (static_cast<size_t>(y) * width + x) * 4;
      char b = bgra[idx + 0];
      char g = bgra[idx + 1];
      char r = bgra[idx + 2];
      [[maybe_unused]] char a = bgra[idx + 3];

      os.put(r);
      os.put(g);
      os.put(b);
    }
  }

  if (!os) {
    throw std::runtime_error("Error occurred while writing pixel data");
  }
}

// ------------------------------------------------------------------------------

ImageDecoder::ImageDecoder(std::string_view sv) {
  if (sv.size() >= 8 &&
      png_sig_cmp(reinterpret_cast<const png_byte*>(sv.data()), 0, 8) == 0) {
    // Decode PNG
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    struct Cleanup {
      png_image& image;
      ~Cleanup() { png_image_free(&image); }
    } cleanup{image};
    if (!png_image_begin_read_from_memory(&image, sv.data(), sv.size()))
      throw std::runtime_error(std::string("PNG header: ") + image.message);
    if (!image.width || !image.height ||
        static_cast<std::uint64_t>(image.width) * image.height >
            std::numeric_limits<int>::max() / 4)
      throw std::runtime_error("PNG dimensions are invalid or too large");
    image.format = PNG_FORMAT_BGRA;
    mem.resize(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, mem.data(), 0, nullptr))
      throw std::runtime_error(std::string("PNG decode: ") + image.message);
    width = image.width;
    height = image.height;
    ismask = false;
    for (std::size_t i = 3; i < mem.size(); i += 4)
      ismask |= static_cast<unsigned char>(mem[i]) != 255;
    return;
  }

  // Decode PDT/G00
  std::unique_ptr<IConverter> conv =
      IConverter::CreateConverter(sv.data(), sv.size());
  if (!conv)
    throw std::runtime_error("Failure at creating GRPCONV.");

  ismask = conv->IsMask();
  height = conv->Height();
  width = conv->Width();

  if (!conv->region_table.empty()) {
    region_table.reserve(conv->region_table.size());
    auto xclannadRegionConverter = [](const IConverter::Region& region) {
      return GrpRect{.rect = Rect(Point(region.x1, region.y1),
                                  Point(region.x2 + 1, region.y2 + 1)),
                     .originX = region.origin_x,
                     .originY = region.origin_y};
    };

    std::transform(conv->region_table.cbegin(), conv->region_table.cend(),
                   std::back_inserter(region_table), xclannadRegionConverter);
  }

  mem.resize(width * height * 4);
  if (!conv->Read(mem.data()))
    throw std::runtime_error("Xclannad converter failed.");
}
