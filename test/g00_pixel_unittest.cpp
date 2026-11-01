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

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/avdec/image_decoder.hpp"
#include "core/object_internal/drawer/file.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "utilities/bytestream.hpp"

class G00PixelTest : public ::testing::Test {
 protected:
  // Literal-only compression keeps the generated cut headers easy to inspect.
  std::string Pack(std::string_view bytes) {
    std::string literals;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      if (i % 8 == 0)
        literals.push_back(static_cast<char>(0xff));
      literals.push_back(bytes[i]);
    }
    oBytestream packed;
    packed << static_cast<std::uint32_t>(literals.size() + 8)
           << static_cast<std::uint32_t>(bytes.size()) << literals;
    return String(packed);
  }

  std::string String(const oBytestream& stream) {
    const auto bytes = stream.Get();
    return std::string(reinterpret_cast<const char*>(bytes.data()),
                       bytes.size());
  }

  std::string Image(std::vector<std::optional<RGBAColour>> colors =
                        {RGBAColour(11, 37, 83, 139)},
                    bool linked_second_cut = false) {
    std::vector<std::string> cuts;
    for (const auto& color : colors) {
      if (!color) {
        cuts.emplace_back();
        continue;
      }
      oBytestream cut;
      cut << std::uint8_t{1} << std::uint8_t{0} << std::uint16_t{1};
      // Stored crop (2,1,2,2), center (3,2), and logical size (6,4).
      for (int value : {2, 1, 2, 2, 3, 2, 6, 4})
        cut << value;
      for (int i = 0; i < 20; ++i)
        cut << 0;
      cut << std::uint16_t{2} << std::uint16_t{1} << std::uint8_t{1}
          << std::uint8_t{0} << std::uint16_t{2} << std::uint16_t{2};
      for (int i = 0; i < 82; ++i)
        cut << std::uint8_t{0};
      for (int i = 0; i < 4; ++i)
        cut << static_cast<std::uint8_t>(color->b())
            << static_cast<std::uint8_t>(color->g())
            << static_cast<std::uint8_t>(color->r())
            << static_cast<std::uint8_t>(color->a());
      cuts.push_back(String(cut));
    }

    oBytestream payload;
    payload << static_cast<std::uint32_t>(cuts.size());
    std::uint32_t offset = 4 + 8 * cuts.size();
    for (std::size_t i = 0; i < cuts.size(); ++i) {
      const auto& cut = cuts[i];
      if (i == 1 && linked_second_cut) {
        payload << static_cast<std::uint32_t>(4 + 8 * cuts.size())
                << -static_cast<std::int32_t>(cuts.front().size());
      } else {
        payload << (cut.empty() ? 0u : offset)
                << static_cast<std::uint32_t>(cut.size());
      }
      offset += cut.size();
    }
    for (const auto& cut : cuts)
      payload << cut;

    oBytestream file;
    file << std::uint8_t{2} << std::uint16_t{6} << std::uint16_t{4}
         << static_cast<std::uint32_t>(cuts.size());
    for (std::size_t i = 0; i < cuts.size(); ++i) {
      // Rendering metadata deliberately has a different origin from the cut.
      for (int value : {0, 0, 5, 3, 4, 3})
        file << value;
    }
    file << Pack(String(payload));
    return String(file);
  }

  std::shared_ptr<SDLSurface> Surface(const std::string& image) {
    ImageDecoder decoder(image);
    SDL_Surface* raw = SDL_CreateSurface(decoder.width, decoder.height,
                                         SDL_PIXELFORMAT_ARGB8888);
    if (!raw)
      throw std::runtime_error(SDL_GetError());
    auto result = std::make_shared<SDLSurface>(raw, decoder.region_table);
    result->UpdateBGRA(decoder.mem, true);
    return result;
  }
};

TEST_F(G00PixelTest, PreservesStoredCropBounds) {
  // Decode a cropped cut; sampling metadata must retain its stored rectangle.
  ImageDecoder decoder(Image());
  ASSERT_EQ(decoder.region_table.size(), 1);
  const auto& region = decoder.region_table[0].pixel_region;
  ASSERT_TRUE(region);
  EXPECT_EQ(region->rect, Rect::REC(2, 1, 2, 2));
}

TEST_F(G00PixelTest, PreservesOriginRelativeToStoredCrop) {
  // The cut center is (3,2) and crop starts at (2,1); local origin is (1,1).
  ImageDecoder decoder(Image());
  ASSERT_TRUE(decoder.region_table[0].pixel_region);
  EXPECT_EQ(decoder.region_table[0].pixel_region->origin, Point(1, 1));
}

TEST_F(G00PixelTest, RetainsExistingRenderingMetadata) {
  // Runtime sampling metadata differs; rendering still uses the database
  // region.
  ImageDecoder decoder(Image());
  const auto& region = decoder.region_table[0];
  EXPECT_EQ(region.rect, Rect::REC(0, 0, 6, 4));
  EXPECT_EQ(region.originX, 4);
  EXPECT_EQ(region.originY, 3);
}

TEST_F(G00PixelTest, SamplesUsingRuntimeCutOrigin) {
  // Read local zero from a decoded cut; use its runtime center, not display
  // data.
  auto surface = Surface(Image());
  EXPECT_EQ(surface->GetPixelColor(Point(), 0), RGBAColour(11, 37, 83, 139));
}

TEST_F(G00PixelTest, RejectsPixelsOutsideStoredCrop) {
  // A local point inside the logical canvas but beyond the crop reads zero.
  auto surface = Surface(Image());
  EXPECT_EQ(surface->GetPixelColor(Point(1, 0), 0), RGBAColour::Clear());
}

TEST_F(G00PixelTest, StackedCutsRetainTheirAtlasOffsets) {
  // Identical database regions stack vertically; cut 1 samples its own pixels.
  auto surface = Surface(
      Image({RGBAColour(11, 37, 83, 139), RGBAColour(151, 173, 191, 211)}));
  EXPECT_EQ(surface->GetPixelColor(Point(), 1), RGBAColour(151, 173, 191, 211));
}

TEST_F(G00PixelTest, MissingCutDataReadsZero) {
  // An empty album slot must remain empty even when another cut has pixels.
  auto surface = Surface(Image({RGBAColour(11, 37, 83, 139), std::nullopt}));
  EXPECT_EQ(surface->GetPixelColor(Point(), 1), RGBAColour::Clear());
}

TEST_F(G00PixelTest, LinkedCutReusesStoredPixels) {
  // A negative cut length references another cut's data; both read its pixels.
  auto surface = Surface(Image(
      {RGBAColour(11, 37, 83, 139), RGBAColour(151, 173, 191, 211)}, true));
  EXPECT_EQ(surface->GetPixelColor(Point(), 1), RGBAColour(11, 37, 83, 139));
}

TEST_F(G00PixelTest, CloneRetainsSamplingMetadata) {
  // Clone a decoded surface; runtime origin and crop remain available to reads.
  auto clone = Surface(Image())->Clone();
  EXPECT_EQ(clone->GetPixelColor(Point(), 0), RGBAColour(11, 37, 83, 139));
  EXPECT_EQ(clone->GetPixelColor(Point(1, 0), 0), RGBAColour::Clear());
}

TEST_F(G00PixelTest, CompositeClipsToStoredBaseCrop) {
  // An overlay reaches logical padding; the base's stored crop still clips it.
  auto base = Surface(Image());
  auto overlay = std::make_shared<SDLSurface>(Size(6, 4));
  overlay->Fill(RGBAColour::White());
  CompositeGraphicsObject drawer({{base}, {overlay}});
  EXPECT_EQ(drawer.GetPixelColor(Point(1, 0), 0), RGBAColour::Clear());
}

TEST_F(G00PixelTest, DeallocatedSurfaceReadsZero) {
  // Deallocating pixel storage leaves no image to sample, regardless of
  // metadata.
  auto surface = Surface(Image());
  surface->Deallocate();
  EXPECT_EQ(surface->GetPixelColor(Point(), 0), RGBAColour::Clear());
}
