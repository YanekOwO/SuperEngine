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

#include "utilities/g00_asset.hpp"

#include "core/avdec/image_decoder.hpp"
#include "test_utils.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

class G00AssetTest : public ::testing::Test {
 protected:
  void SetUp() override {
    asset_ = {6, 4, std::vector<char>(6 * 4 * 4), g00::MakeGrid(6, 4, 3, 2)};
    for (std::size_t i = 0; i < asset_.bgra.size(); ++i)
      asset_.bgra[i] = static_cast<char>(i * 37);
  }

  std::string Bytes(const std::vector<char>& bytes) {
    return {bytes.begin(), bytes.end()};
  }

  g00::Asset RoundTrip(const g00::Asset& asset) {
    return g00::DecodeG00(Bytes(g00::EncodeG00(asset)));
  }

  std::string Fixture(const std::string& name) {
    std::ifstream input(PathToTestCase("Gameroot/g00/" + name),
                        std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
  }

  void WriteInt(std::string& bytes, std::size_t at, int value, int size = 4) {
    bytes.resize(std::max(bytes.size(), at + size));
    for (int i = 0; i < size; ++i)
      bytes[at + i] =
          static_cast<char>(static_cast<std::uint32_t>(value) >> (8 * i));
  }

  std::string LiteralFile(std::string header, std::string_view payload) {
    std::string packed;
    for (std::size_t i = 0; i < payload.size(); ++i) {
      if (i % 8 == 0)
        packed.push_back('\xff');
      packed.push_back(payload[i]);
    }
    const auto at = header.size();
    WriteInt(header, at, packed.size() + 8);
    WriteInt(header, at + 4, payload.size());
    return header + packed;
  }

  g00::Asset asset_;
};

TEST_F(G00AssetTest, EncoderPreservesEveryBgraByte) {
  // Four colored cells include non-opaque pixels; G00 decoding preserves all
  // bytes.
  EXPECT_EQ(RoundTrip(asset_).bgra, asset_.bgra);
}

TEST_F(G00AssetTest, EngineReadsEncodedPixels) {
  // Decode generated cuts directly through the runtime, independently of
  // preflight.
  ImageDecoder decoder(Bytes(g00::EncodeG00(asset_)));
  EXPECT_EQ(decoder.mem, asset_.bgra);
}

TEST_F(G00AssetTest, GridOrdersRegionsAcrossRowsThenDown) {
  // A two-by-two grid assigns indexes left to right before moving down.
  const auto result = RoundTrip(asset_);
  ASSERT_EQ(result.regions.size(), 4);
  EXPECT_EQ(result.regions[0].rect, Rect::REC(0, 0, 3, 2));
  EXPECT_EQ(result.regions[1].rect, Rect::REC(3, 0, 3, 2));
  EXPECT_EQ(result.regions[2].rect, Rect::REC(0, 2, 3, 2));
  EXPECT_EQ(result.regions[3].rect, Rect::REC(3, 2, 3, 2));
}

TEST_F(G00AssetTest, EncoderPreservesRenderingOrigins) {
  // Give a region signed rendering offsets; encoding retains both coordinates.
  asset_.regions[2].originX = -5;
  asset_.regions[2].originY = 17;
  const auto result = RoundTrip(asset_);
  EXPECT_EQ(result.regions[2].originX, -5);
  EXPECT_EQ(result.regions[2].originY, 17);
}

TEST_F(G00AssetTest, EncoderPreservesCroppedSamplingMetadata) {
  // Crop the bottom-right cell and offset its sampling origin; both survive.
  asset_.regions[3].pixel_region =
      PixelRegion{Rect::REC(4, 3, 2, 1), Point(-2, 7)};
  const auto result = RoundTrip(asset_);
  ASSERT_TRUE(result.regions[3].pixel_region);
  EXPECT_EQ(result.regions[3].pixel_region->rect, Rect::REC(4, 3, 2, 1));
  EXPECT_EQ(result.regions[3].pixel_region->origin, Point(-2, 7));
}

TEST_F(G00AssetTest, EncoderPreservesEmptyCuts) {
  // An empty final slot remains empty instead of inheriting neighboring pixels.
  asset_.regions[3].pixel_region = PixelRegion{Rect::REC(3, 2, 0, 0), Point()};
  const auto result = RoundTrip(asset_);
  ASSERT_TRUE(result.regions[3].pixel_region);
  EXPECT_EQ(result.regions[3].pixel_region->rect, Rect::REC(3, 2, 0, 0));
}

TEST_F(G00AssetTest, EncoderDefaultsToOneFullImageCut) {
  // PNGs have no region table; encoding supplies a single full-canvas pattern.
  asset_.regions.clear();
  const auto result = RoundTrip(asset_);
  ASSERT_EQ(result.regions.size(), 1);
  EXPECT_EQ(result.regions[0].rect, Rect::REC(0, 0, 6, 4));
}

TEST_F(G00AssetTest, CompressionReducesRepetitivePixels) {
  // A solid image offers repeated runs; the encoder emits references, not
  // literals only.
  asset_ = {64, 64, std::vector<char>(64 * 64 * 4, '\x7f'), {}};
  const auto encoded = g00::EncodeG00(asset_);
  EXPECT_LT(encoded.size(), asset_.bgra.size() / 2);
  EXPECT_EQ(g00::DecodeG00(Bytes(encoded)).bgra, asset_.bgra);
}

TEST_F(G00AssetTest, CompressionHandlesDataBeyondTheSlidingWindow) {
  // Random pixels cross many dictionary wraps; compressed decoding stays
  // lossless.
  asset_ = {128, 128, std::vector<char>(128 * 128 * 4), {}};
  std::mt19937 random(42);
  for (auto& byte : asset_.bgra)
    byte = static_cast<char>(random());
  EXPECT_EQ(RoundTrip(asset_).bgra, asset_.bgra);
}

TEST_F(G00AssetTest, PngRoundTripPreservesStraightAlphaAndColors) {
  // Non-opaque BGRA pixels pass through PNG without alpha or channel changes.
  EXPECT_EQ(g00::DecodePNG(Bytes(g00::EncodePNG(asset_))).bgra, asset_.bgra);
}

TEST_F(G00AssetTest, JsonRoundTripPreservesRenderingAndSamplingData) {
  // Distinct origins and a crop are exported to JSON and reapplied exactly.
  asset_.regions[3].originX = 11;
  asset_.regions[3].pixel_region =
      PixelRegion{Rect::REC(4, 3, 2, 1), Point(3, -1)};
  auto restored = asset_;
  restored.regions.clear();
  g00::ApplyMetadata(restored, g00::Metadata(asset_));
  EXPECT_EQ(g00::Metadata(restored), g00::Metadata(asset_));
}

TEST_F(G00AssetTest, JsonRejectsDifferentImageDimensions) {
  // Metadata from a differently sized canvas cannot silently alter PNG
  // geometry.
  auto different = asset_;
  different.width = 3;
  different.height = 8;
  different.regions.clear();
  EXPECT_THROW(g00::ApplyMetadata(asset_, g00::Metadata(different)),
               std::runtime_error);
}

TEST_F(G00AssetTest, JsonFailureLeavesTheOriginalAssetIntact) {
  // An invalid region must fail validation without losing the caller's pixel
  // buffer.
  const auto before = asset_.bgra;
  const std::string json = R"({"version":1,"width":6,"height":4,
      "regions":[{"rect":[0,0,7,4],"origin":[0,0]}]})";
  EXPECT_THROW(g00::ApplyMetadata(asset_, json), std::runtime_error);
  EXPECT_EQ(asset_.bgra, before);
  EXPECT_EQ(asset_.regions.size(), 4);
}

TEST_F(G00AssetTest, GridRejectsPartialCells) {
  // A cell width that leaves trailing columns must be rejected, not truncated.
  EXPECT_THROW(g00::MakeGrid(6, 4, 4, 2), std::runtime_error);
}

TEST_F(G00AssetTest, ExtractionRetainsLogicalPaddingAroundCrop) {
  // Extract a cropped cell; stored pixels retain their position in a
  // transparent canvas.
  asset_.regions[3].pixel_region = PixelRegion{Rect::REC(4, 3, 2, 1), Point()};
  const auto extracted = g00::ExtractRegion(asset_, 3);
  ASSERT_EQ(extracted.bgra.size(), 3 * 2 * 4);
  EXPECT_EQ(extracted.width, 3);
  EXPECT_EQ(extracted.height, 2);
  EXPECT_EQ(extracted.bgra[0], 0);
  EXPECT_EQ(extracted.bgra[16], asset_.bgra[(3 * 6 + 4) * 4]);
}

TEST_F(G00AssetTest, ExtractionRejectsMissingIndex) {
  // A four-pattern sheet has no fifth sprite; indexing it reports an error.
  EXPECT_THROW(g00::ExtractRegion(asset_, 4), std::runtime_error);
}

TEST_F(G00AssetTest, DecoderReadsLegacyRgbFixture) {
  // The legacy RGB fixture advertises extra decompression space; its pixels
  // still decode.
  const auto bytes = Fixture("rainbow.g00");
  ImageDecoder engine(bytes);
  EXPECT_EQ(g00::DecodeG00(bytes).bgra, engine.mem);
}

TEST_F(G00AssetTest, DecoderReadsLegacyCutFixture) {
  // Read an existing multi-cut fixture; preflight must accept engine-compatible
  // metadata.
  const auto bytes = Fixture("rainbow_cut.g00");
  ImageDecoder engine(bytes);
  EXPECT_EQ(g00::DecodeG00(bytes).bgra, engine.mem);
}

TEST_F(G00AssetTest, DecoderReadsPalettePixels) {
  // A two-color indexed image expands its palette to the original BGRA bytes.
  std::string header(5, '\0');
  WriteInt(header, 0, 1, 1);
  WriteInt(header, 1, 2, 2);
  WriteInt(header, 3, 1, 2);
  std::string payload;
  WriteInt(payload, 0, 2, 2);
  WriteInt(payload, 2, 0x11223344);
  WriteInt(payload, 6, 0x55667788);
  payload += '\0';
  payload += '\1';
  const auto result = g00::DecodeG00(LiteralFile(header, payload));
  EXPECT_EQ(Bytes(result.bgra), payload.substr(2, 8));
}

TEST_F(G00AssetTest, DecoderReadsObfuscatedJpegFixture) {
  // A generated JPEG-backed G00 is decoded with the same colors as the engine.
  const auto bytes = Fixture("solid_jpeg.g00");
  ImageDecoder engine(bytes);
  const auto result = g00::DecodeG00(bytes);
  EXPECT_EQ(result.width, 8);
  EXPECT_EQ(result.height, 8);
  EXPECT_EQ(result.bgra, engine.mem);
}

TEST_F(G00AssetTest, DecoderStacksNonemptyRegionsAroundEmptySlots) {
  // Two equal regions and a degenerate slot trigger the engine's stack rule.
  std::string header(9 + 24 * 3, '\0');
  WriteInt(header, 0, 2, 1);
  WriteInt(header, 1, 6, 2);
  WriteInt(header, 3, 4, 2);
  WriteInt(header, 5, 3);
  for (int i = 0; i < 2; ++i) {
    WriteInt(header, 9 + 24 * i + 8, 5);
    WriteInt(header, 9 + 24 * i + 12, 3);
  }
  std::string payload(4 + 8 * 3, '\0');
  WriteInt(payload, 0, 3);
  const auto result = g00::DecodeG00(LiteralFile(header, payload));
  EXPECT_EQ(result.height, 12);
  EXPECT_EQ(result.regions[2].rect, Rect::REC(0, 8, 1, 1));
}

TEST_F(G00AssetTest, DecoderRejectsChipsOutsideCanvas) {
  // A one-pixel chip starts beyond the right edge; reject it before any copy.
  auto full = asset_;
  full.regions.clear();
  const auto header = Bytes(g00::EncodeG00(full)).substr(0, 33);
  std::string payload(12 + 0x74 + 0x5c + 4, '\0');
  WriteInt(payload, 0, 1);
  WriteInt(payload, 4, 12);
  WriteInt(payload, 8, payload.size() - 12);
  WriteInt(payload, 12 + 2, 1, 2);
  WriteInt(payload, 12 + 12, 1);
  WriteInt(payload, 12 + 16, 1);
  WriteInt(payload, 12 + 0x74, 6, 2);
  WriteInt(payload, 12 + 0x74 + 6, 1, 2);
  WriteInt(payload, 12 + 0x74 + 8, 1, 2);
  EXPECT_THROW(g00::DecodeG00(LiteralFile(header, payload)),
               std::runtime_error);
}

TEST_F(G00AssetTest, DecoderRejectsEveryTruncatedPrefix) {
  // Every proper prefix of a generated file is incomplete and fails before
  // legacy decoding.
  const auto bytes = Bytes(g00::EncodeG00(asset_));
  for (std::size_t size = 0; size < bytes.size(); ++size)
    EXPECT_THROW(g00::DecodeG00(std::string_view(bytes).substr(0, size)),
                 std::runtime_error);
}

TEST_F(G00AssetTest, DecoderRejectsInvalidBackReferences) {
  // A palette stream references bytes before its start; preflight reports the
  // bad distance.
  const char bytes[] = {1, 1, 0, 1, 0, 11, 0, 0, 0, 8, 0, 0, 0, 0, 0x10, 0};
  EXPECT_THROW(g00::DecodeG00(std::string_view(bytes, sizeof(bytes))),
               std::runtime_error);
}

TEST_F(G00AssetTest, EncoderRejectsCropsOutsideTheirRegion) {
  // A crop exceeds its cell bounds; encoding must not read another sprite's
  // pixels.
  asset_.regions[0].pixel_region = PixelRegion{Rect::REC(2, 0, 3, 2), Point()};
  EXPECT_THROW(g00::EncodeG00(asset_), std::runtime_error);
}

TEST_F(G00AssetTest, EncoderRejectsUnexpectedAutomaticStacking) {
  // Identical display regions would change decoded height; require distinct
  // atlas cells.
  asset_.regions = {asset_.regions[0], asset_.regions[0]};
  EXPECT_THROW(g00::EncodeG00(asset_), std::runtime_error);
}
