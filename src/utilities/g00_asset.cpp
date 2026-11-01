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

#include "utilities/g00_asset.hpp"

#include "core/avdec/image_decoder.hpp"

#include <png.h>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace g00 {
namespace {

constexpr std::size_t kMaxBytes = 256 * 1024 * 1024;
constexpr int kMaxShort = 32767;

void Require(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

std::uint32_t Read(std::string_view bytes, std::size_t offset, int size = 4) {
  Require(offset <= bytes.size() &&
              bytes.size() - offset >= static_cast<std::size_t>(size),
          "Truncated G00 data");
  std::uint32_t value = 0;
  for (int i = 0; i < size; ++i)
    value |= std::uint32_t(static_cast<unsigned char>(bytes[offset + i]))
             << (8 * i);
  return value;
}

int Signed(std::string_view bytes, std::size_t offset, int size = 4) {
  const auto value = Read(bytes, offset, size);
  return size == 2 ? static_cast<std::int16_t>(value)
                   : static_cast<std::int32_t>(value);
}

void Put(std::vector<char>& bytes, std::int64_t value, int size = 4) {
  for (int i = 0; i < size; ++i)
    bytes.push_back(
        static_cast<char>(static_cast<std::uint64_t>(value) >> (8 * i)));
}

std::size_t PixelBytes(int width, int height) {
  Require(width > 0 && height > 0 && width <= kMaxShort && height <= kMaxShort,
          "Image dimensions must be between 1 and 32767");
  const auto bytes = static_cast<std::size_t>(width) * height * 4;
  Require(bytes <= kMaxBytes, "Image exceeds the 256 MiB pixel limit");
  return bytes;
}

bool Inside(const Rect& inner, const Rect& outer) {
  return inner.width() >= 0 && inner.height() >= 0 && inner.x() >= outer.x() &&
         inner.y() >= outer.y() &&
         static_cast<std::int64_t>(inner.x()) + inner.width() <=
             static_cast<std::int64_t>(outer.x()) + outer.width() &&
         static_cast<std::int64_t>(inner.y()) + inner.height() <=
             static_cast<std::int64_t>(outer.y()) + outer.height();
}

void Validate(const Asset& asset) {
  Require(asset.bgra.size() == PixelBytes(asset.width, asset.height),
          "Pixel buffer does not match image dimensions");
  Require(asset.regions.size() <= kMaxShort,
          "Too many regions (maximum 32767)");
  const Rect canvas = Rect::REC(0, 0, asset.width, asset.height);
  for (const auto& region : asset.regions) {
    Require(region.rect.width() > 0 && region.rect.height() > 0 &&
                Inside(region.rect, canvas),
            "Region must be a nonempty rectangle inside the image");
    if (region.pixel_region) {
      const auto& pixel = *region.pixel_region;
      Require(Inside(pixel.rect, region.rect),
              "Pixel crop must be inside its rendering region");
      const auto x = static_cast<std::int64_t>(pixel.rect.x()) -
                     region.rect.x() + pixel.origin.x();
      const auto y = static_cast<std::int64_t>(pixel.rect.y()) -
                     region.rect.y() + pixel.origin.y();
      Require(x >= std::numeric_limits<int>::min() &&
                  x <= std::numeric_limits<int>::max() &&
                  y >= std::numeric_limits<int>::min() &&
                  y <= std::numeric_limits<int>::max(),
              "Pixel origin exceeds the G00 integer range");
      if (!pixel.rect.width() || !pixel.rect.height())
        Require(
            pixel.rect == Rect::REC(region.rect.x(), region.rect.y(), 0, 0) &&
                pixel.origin == Point(),
            "Empty cuts must have a zero origin and zero size");
    }
  }
}

std::vector<char> Unpack(std::string_view bytes, std::size_t offset, int unit) {
  const auto packed = Read(bytes, offset);
  const auto expected = Read(bytes, offset + 4);
  Require(packed >= 8 && packed == bytes.size() - offset,
          "G00 compressed size does not match file size");
  Require(expected > 0 && expected <= kMaxBytes,
          "Invalid G00 decompressed size (maximum 256 MiB)");
  std::vector<char> result;
  result.reserve(expected);
  std::size_t cursor = offset + 8;
  while (result.size() < expected && cursor < bytes.size()) {
    const auto flags = Read(bytes, cursor++, 1);
    for (int bit = 0;
         bit < 8 && result.size() < expected && cursor < bytes.size(); ++bit) {
      if (flags & (1 << bit)) {
        Require(bytes.size() - cursor >= static_cast<std::size_t>(unit) &&
                    expected - result.size() >= static_cast<std::size_t>(unit),
                "Truncated G00 literal");
        result.insert(result.end(), bytes.begin() + cursor,
                      bytes.begin() + cursor + unit);
        cursor += unit;
      } else {
        const auto token = Read(bytes, cursor, 2);
        cursor += 2;
        const auto distance = (token >> 4) * unit;
        const auto length = ((token & 15) + (unit == 3 ? 1 : 2)) * unit;
        Require(distance > 0 && distance <= result.size(),
                "Invalid G00 back-reference distance");
        Require(length <= expected - result.size(),
                "G00 back-reference exceeds decompressed size");
        for (std::uint32_t i = 0; i < length; ++i)
          result.push_back(result[result.size() - distance]);
      }
    }
  }
  // Some type 0 assets advertise a larger buffer than their RGB data needs.
  Require(unit == 3 || result.size() == expected, "Truncated G00 payload");
  Require(cursor == bytes.size(),
          "Trailing bytes after G00 compressed payload");
  return result;
}

// Validate pointers, LZ references and chip destinations before calling the
// legacy decoder, whose optimized loops assume trusted input.
void CheckG00(std::string_view bytes) {
  const auto type = Read(bytes, 0, 1);
  Require(type <= 3, "Unsupported G00 type (expected 0, 1, 2 or 3)");
  const int width = Signed(bytes, 1, 2), height = Signed(bytes, 3, 2);
  PixelBytes(width, height);
  Require(bytes.size() >= 10 && bytes.size() <= kMaxBytes,
          "Invalid G00 file size (maximum 256 MiB)");
  if (type == 3)
    return;
  std::size_t offset = 5;
  std::vector<std::array<int, 6>> regions;
  int atlas_height = height;
  if (type == 2) {
    const auto count = Read(bytes, 5);
    Require(count > 0 && count <= kMaxShort, "Invalid G00 region count");
    offset = 9 + 24 * count;
    Require(offset <= bytes.size() && bytes.size() - offset >= 8,
            "Truncated G00 region table");
    for (std::uint32_t i = 0; i < count; ++i) {
      std::array<int, 6> region;
      for (int j = 0; j < 6; ++j)
        region[j] = Signed(bytes, 9 + 24 * i + 4 * j);
      region[0] = std::clamp(region[0], 0, width - 1);
      region[1] = std::clamp(region[1], 0, height - 1);
      region[2] = std::clamp(region[2], region[0], width - 1);
      region[3] = std::clamp(region[3], region[1], height - 1);
      regions.push_back(region);
    }
    std::set<std::array<int, 6>> unique;
    int nonempty = 0;
    for (const auto& region : regions) {
      if (region[0] < region[2] && region[1] < region[3]) {
        ++nonempty;
        unique.insert(region);
      }
    }
    if (nonempty > 1 && unique.size() == 1) {
      atlas_height *= count;
      PixelBytes(width, atlas_height);
      for (std::size_t i = 0; i < regions.size(); ++i) {
        regions[i][1] += i * height;
        regions[i][3] += i * height;
      }
    }
  }
  const auto storage = Unpack(bytes, offset, type == 0 ? 3 : 1);
  const std::string_view payload(storage.data(), storage.size());
  if (type == 0) {
    Require(storage.size() >= static_cast<std::size_t>(width) * height * 3,
            "G00 RGB payload is too short");
    return;
  }
  if (type == 1) {
    const auto colors = Read(payload, 0, 2);
    Require(colors <= 256 &&
                payload.size() >=
                    2 + colors * 4 + static_cast<std::size_t>(width) * height,
            "Invalid G00 palette or index data");
    return;
  }
  const auto count = Read(payload, 0);
  Require(count == regions.size() && 4 + 8 * count <= payload.size(),
          "G00 cut table does not match region count");
  for (std::size_t i = 0; i < count; ++i) {
    const auto start = Read(payload, 4 + 8 * i);
    const int length = Signed(payload, 8 + 8 * i);
    if (!start || !length)
      continue;
    Require(start <= payload.size() && payload.size() - start >= 0x74,
            "G00 cut offset is outside the payload");
    Require(length < 0 || (length >= 0x74 && static_cast<std::size_t>(length) <=
                                                 payload.size() - start),
            "G00 cut length is outside the payload");
    const auto end = length < 0 ? payload.size() : start + length;
    const int chips = Signed(payload, start + 2, 2);
    Require(chips >= 0 && Signed(payload, start + 12) >= 0 &&
                Signed(payload, start + 16) >= 0,
            "Invalid G00 cut dimensions or chip count");
    std::size_t cursor = start + 0x74;
    for (int j = 0; j < chips; ++j) {
      Require(cursor <= end && end - cursor >= 0x5c,
              "Truncated G00 chip header");
      const auto x =
          static_cast<std::int64_t>(regions[i][0]) + Signed(payload, cursor, 2);
      const auto y = static_cast<std::int64_t>(regions[i][1]) +
                     Signed(payload, cursor + 2, 2);
      const int w = Signed(payload, cursor + 6, 2),
                h = Signed(payload, cursor + 8, 2);
      cursor += 0x5c;
      Require(x >= 0 && y >= 0 && w >= 0 && h >= 0 && x + w <= width &&
                  y + h <= atlas_height,
              "G00 chip writes outside the image");
      const auto size = static_cast<std::size_t>(w) * h * 4;
      Require(size <= end - cursor, "Truncated G00 chip pixels");
      cursor += size;
    }
  }
}

std::vector<char> Pack(const std::vector<char>& input) {
  std::array<int, 65536> heads;
  std::array<int, 4096> previous;
  heads.fill(-1);
  previous.fill(-1);
  const int size = input.size();
  auto hash = [&](int at) {
    const auto a = static_cast<unsigned char>(input[at]);
    const auto b = static_cast<unsigned char>(input[at + 1]);
    const auto c = static_cast<unsigned char>(input[at + 2]);
    return ((a * 251 + b) * 251 + c) & 65535;
  };
  std::vector<char> packed;
  for (int pos = 0; pos < size;) {
    const auto flag_at = packed.size();
    packed.push_back(0);
    unsigned char flags = 0;
    for (int bit = 0; bit < 8 && pos < size; ++bit) {
      int best = 0, distance = 0;
      if (pos + 2 < size) {
        int candidate = heads[hash(pos)];
        for (int tries = 0;
             candidate >= 0 && pos - candidate <= 4095 && tries < 64; ++tries) {
          int length = 0;
          while (length < 17 && pos + length < size &&
                 input[candidate + length] == input[pos + length])
            ++length;
          if (length > best) {
            best = length;
            distance = pos - candidate;
            if (best == 17)
              break;
          }
          candidate = previous[candidate & 4095];
        }
      }
      const int consumed = best >= 3 ? best : 1;
      if (best >= 3) {
        Put(packed, (distance << 4) | (best - 2), 2);
      } else {
        flags |= 1 << bit;
        packed.push_back(input[pos]);
      }
      for (int end = pos + consumed; pos < end; ++pos) {
        if (pos + 2 < size) {
          const int key = hash(pos);
          previous[pos & 4095] = heads[key];
          heads[key] = pos;
        }
      }
    }
    packed[flag_at] = static_cast<char>(flags);
  }
  std::vector<char> result;
  Put(result, packed.size() + 8);
  Put(result, input.size());
  result.insert(result.end(), packed.begin(), packed.end());
  return result;
}

Asset Decoded(std::string_view bytes) {
  // The type 0 decoder uses four-byte loads when copying three-byte pixels.
  std::string padded(bytes);
  padded.append(8, '\0');
  ImageDecoder decoder(std::string_view(padded.data(), bytes.size()));
  Asset result{decoder.width, decoder.height, std::move(decoder.mem),
               std::move(decoder.region_table)};
  Validate(result);
  return result;
}

}  // namespace

Asset DecodeG00(std::string_view bytes) {
  CheckG00(bytes);
  return Decoded(bytes);
}

Asset DecodePNG(std::string_view bytes) {
  Require(bytes.size() >= 24 &&
              png_sig_cmp(reinterpret_cast<const png_byte*>(bytes.data()), 0,
                          8) == 0,
          "Input must be a PNG image");
  // Check dimensions before the decoder allocates its pixel buffer.
  auto big_endian = [&](int offset) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
      value = (value << 8) | static_cast<unsigned char>(bytes[offset + i]);
    return value;
  };
  PixelBytes(big_endian(16), big_endian(20));
  return Decoded(bytes);
}

std::vector<char> EncodeG00(const Asset& asset) {
  Validate(asset);
  auto regions = asset.regions;
  if (regions.empty())
    regions = MakeGrid(asset.width, asset.height, asset.width, asset.height);
  // Repeated rendering rectangles would trigger the decoder's stack heuristic.
  Require(regions.size() == 1 ||
              !std::all_of(regions.begin(), regions.end(),
                           [&](const auto& r) {
                             return r.rect == regions.front().rect &&
                                    r.originX == regions.front().originX &&
                                    r.originY == regions.front().originY;
                           }),
          "Identical regions must be arranged as separate atlas cells");
  std::vector<std::vector<char>> cuts;
  std::size_t payload_size = 4 + 8 * regions.size();
  for (const auto& region : regions) {
    const PixelRegion pixel =
        region.pixel_region.value_or(PixelRegion{region.rect, Point()});
    std::vector<char> cut;
    if (pixel.rect.width() && pixel.rect.height()) {
      const int x = pixel.rect.x() - region.rect.x();
      const int y = pixel.rect.y() - region.rect.y();
      Put(cut, 1, 2);
      Put(cut, 1, 2);
      for (int value :
           {x, y, pixel.rect.width(), pixel.rect.height(),
            static_cast<int>(static_cast<std::int64_t>(x) + pixel.origin.x()),
            static_cast<int>(static_cast<std::int64_t>(y) + pixel.origin.y()),
            region.rect.width(), region.rect.height()})
        Put(cut, value);
      cut.resize(0x74, 0);
      Put(cut, x, 2);
      Put(cut, y, 2);
      Put(cut, 1, 2);
      Put(cut, pixel.rect.width(), 2);
      Put(cut, pixel.rect.height(), 2);
      cut.resize(0x74 + 0x5c, 0);
      for (int row = 0; row < pixel.rect.height(); ++row) {
        const auto at =
            (static_cast<std::size_t>(pixel.rect.y() + row) * asset.width +
             pixel.rect.x()) *
            4;
        cut.insert(cut.end(), asset.bgra.begin() + at,
                   asset.bgra.begin() + at + pixel.rect.width() * 4);
      }
    }
    payload_size += cut.size();
    Require(payload_size <= kMaxBytes, "G00 cut data exceeds 256 MiB");
    cuts.push_back(std::move(cut));
  }
  std::vector<char> payload;
  payload.reserve(payload_size);
  Put(payload, regions.size());
  std::size_t offset = 4 + 8 * regions.size();
  for (const auto& cut : cuts) {
    Put(payload, cut.empty() ? 0 : offset);
    Put(payload, cut.size());
    offset += cut.size();
  }
  for (const auto& cut : cuts)
    payload.insert(payload.end(), cut.begin(), cut.end());
  std::vector<char> file;
  Put(file, 2, 1);
  Put(file, asset.width, 2);
  Put(file, asset.height, 2);
  Put(file, regions.size());
  for (const auto& region : regions)
    for (int value : {region.rect.x(), region.rect.y(),
                      region.rect.x() + region.rect.width() - 1,
                      region.rect.y() + region.rect.height() - 1,
                      region.originX, region.originY})
      Put(file, value);
  const auto packed = Pack(payload);
  file.insert(file.end(), packed.begin(), packed.end());
  Require(file.size() <= kMaxBytes, "Encoded G00 exceeds 256 MiB");
  return file;
}

std::vector<char> EncodePNG(const Asset& asset) {
  Validate(asset);
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  struct Cleanup {
    png_image& image;
    ~Cleanup() { png_image_free(&image); }
  } cleanup{image};
  image.width = asset.width;
  image.height = asset.height;
  image.format = PNG_FORMAT_BGRA;
  png_alloc_size_t size = 0;
  Require(png_image_write_to_memory(&image, nullptr, &size, 0,
                                    asset.bgra.data(), 0, nullptr),
          "Unable to size PNG output");
  std::vector<char> output(size);
  const bool ok = png_image_write_to_memory(&image, output.data(), &size, 0,
                                            asset.bgra.data(), 0, nullptr);
  const std::string error = image.message;
  if (!ok)
    throw std::runtime_error("PNG encode: " + error);
  output.resize(size);
  return output;
}

std::vector<GrpRect> MakeGrid(int width,
                              int height,
                              int cell_width,
                              int cell_height) {
  PixelBytes(width, height);
  Require(cell_width > 0 && cell_height > 0 && width % cell_width == 0 &&
              height % cell_height == 0,
          "Grid cell size must divide the image dimensions exactly");
  Require((width / cell_width) * (height / cell_height) <= kMaxShort,
          "Grid has too many cells (maximum 32767)");
  std::vector<GrpRect> regions;
  for (int y = 0; y < height; y += cell_height)
    for (int x = 0; x < width; x += cell_width) {
      const auto rect = Rect::REC(x, y, cell_width, cell_height);
      regions.push_back({rect, 0, 0, PixelRegion{rect, Point()}});
    }
  return regions;
}

Asset ExtractRegion(const Asset& asset, std::size_t index) {
  Validate(asset);
  if (asset.regions.empty()) {
    Require(index == 0, "Region index is outside the region table");
    return asset;
  }
  Require(index < asset.regions.size(),
          "Region index is outside the region table");
  const auto& region = asset.regions[index];
  Asset result{region.rect.width(), region.rect.height(), {}, {}};
  result.bgra.resize(PixelBytes(result.width, result.height));
  const auto rect =
      region.pixel_region ? region.pixel_region->rect : region.rect;
  for (int row = 0; row < rect.height(); ++row) {
    const auto source =
        (static_cast<std::size_t>(rect.y() + row) * asset.width + rect.x()) * 4;
    const auto dest =
        (static_cast<std::size_t>(rect.y() - region.rect.y() + row) *
             result.width +
         rect.x() - region.rect.x()) *
        4;
    std::copy_n(asset.bgra.begin() + source, rect.width() * 4,
                result.bgra.begin() + dest);
  }
  return result;
}

std::string Metadata(const Asset& asset) {
  Validate(asset);
  std::ostringstream out;
  auto rect = [&](const Rect& r) {
    out << '[' << r.x() << ", " << r.y() << ", " << r.width() << ", "
        << r.height() << ']';
  };
  out << "{\n  \"version\": 1,\n  \"width\": " << asset.width
      << ",\n  \"height\": " << asset.height << ",\n  \"regions\": [";
  for (std::size_t i = 0; i < asset.regions.size(); ++i) {
    const auto& r = asset.regions[i];
    out << (i ? ",\n" : "\n") << "    {\"rect\": ";
    rect(r.rect);
    out << ", \"origin\": [" << r.originX << ", " << r.originY << ']';
    if (r.pixel_region) {
      out << ", \"pixels\": {\"rect\": ";
      rect(r.pixel_region->rect);
      out << ", \"origin\": [" << r.pixel_region->origin.x() << ", "
          << r.pixel_region->origin.y() << "]}";
    }
    out << '}';
  }
  out << (asset.regions.empty() ? "" : "\n  ") << "]\n}\n";
  return out.str();
}

void ApplyMetadata(Asset& asset, std::string_view json) {
  boost::property_tree::ptree tree;
  std::istringstream input{std::string(json)};
  boost::property_tree::read_json(input, tree);
  Require(tree.get<int>("version") == 1, "Unsupported metadata version");
  Require(tree.get<int>("width") == asset.width &&
              tree.get<int>("height") == asset.height,
          "Metadata dimensions do not match the PNG image");
  auto values = [](const boost::property_tree::ptree& node, std::size_t count) {
    Require(node.data().empty() && node.size() == count,
            "Invalid metadata coordinate array");
    std::vector<int> result;
    for (const auto& [key, child] : node) {
      Require(key.empty() && child.empty(),
              "Expected a metadata integer array");
      result.push_back(child.get_value<int>());
    }
    return result;
  };
  auto rectangle = [&](const boost::property_tree::ptree& node) {
    const auto v = values(node, 4);
    return Rect::REC(v[0], v[1], v[2], v[3]);
  };
  auto origin = [&](const boost::property_tree::ptree& node) {
    const auto v = values(node, 2);
    return Point(v[0], v[1]);
  };
  std::vector<GrpRect> regions;
  const auto& array = tree.get_child("regions");
  Require(array.data().empty(), "Metadata regions must be an array");
  for (const auto& [key, node] : array) {
    Require(key.empty(), "Metadata regions must be an array");
    GrpRect r;
    r.rect = rectangle(node.get_child("rect"));
    const auto point = origin(node.get_child("origin"));
    r.originX = point.x();
    r.originY = point.y();
    if (const auto pixel = node.get_child_optional("pixels"))
      r.pixel_region = PixelRegion{rectangle(pixel->get_child("rect")),
                                   origin(pixel->get_child("origin"))};
    regions.push_back(r);
  }
  Asset candidate{asset.width, asset.height, {}, std::move(regions)};
  candidate.bgra.swap(asset.bgra);
  try {
    Validate(candidate);
  } catch (...) {
    candidate.bgra.swap(asset.bgra);
    throw;
  }
  asset = std::move(candidate);
}

}  // namespace g00
