// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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
//
// -----------------------------------------------------------------------

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "core/asset_scanner.hpp"
#include "core/image_resources.hpp"
#include "systems/igraphics_backend.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "utilities/bytestream.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::Throw;

const std::set<std::string> kImageExtensions = {"g00", "pdt"};

class MockAssetScanner : public AssetScanner {
 public:
  MOCK_METHOD((expected<fs::path, std::runtime_error>),
              FindFile,
              (std::string, const std::set<std::string>&),
              (override));
};

class MockGraphicsBackend : public IGraphicsBackend {
 public:
  MOCK_METHOD(void, InitSystem, (Size, bool), (override));
  MOCK_METHOD(void, QuitSystem, (), (override));
  MOCK_METHOD(Size, Resize, (Size, bool), (override));
  MOCK_METHOD(std::shared_ptr<SDLSurface>, CreateSurface, (Size), (override));
  MOCK_METHOD(std::shared_ptr<SDLSurface>,
              CreateSurfaceBGRA,
              (Size, std::span<char>, bool),
              (override));
  MOCK_METHOD(std::shared_ptr<SDLSurface>,
              LoadSurface,
              (const fs::path&),
              (override));
  MOCK_METHOD(std::shared_ptr<Album>, LoadAlbum, (const fs::path&), (override));
  MOCK_METHOD(void, SetWindowTitle, (const std::string&), (override));
  MOCK_METHOD(void, ShowSystemCursor, (bool), (override));
  MOCK_METHOD(void, WarpMouse, (Point), (override));
  MOCK_METHOD(void,
              RenderFrame,
              (const RenderFrameConfig&,
               const DrawCallback&,
               const DrawCallback&),
              (override));
  MOCK_METHOD(bool,
              RedrawLastFrame,
              (const RenderFrameConfig&, const DrawCallback&),
              (override));
  MOCK_METHOD(std::shared_ptr<SDLSurface>,
              RenderToSurface,
              (const RenderFrameConfig&, const DrawCallback&),
              (override));
};

ToneCurve MakeToneCurves() {
  std::array<ToneCurveRGBMap, 2> effects{};
  for (int i = 0; i < 256; ++i) {
    effects[0][0][i] = 255 - i;
    effects[0][1][i] = i / 2;
    effects[0][2][i] = (i + 17) % 256;
    effects[1][0][i] = i / 2;
    effects[1][1][i] = (i + 29) % 256;
    effects[1][2][i] = 255 - i;
  }

  oBytestream data;
  data << 1000 << static_cast<uint32_t>(effects.size());
  for (int i = 0; i < 1000; ++i)
    data << (i < effects.size() ? 4008 + i * 832 : 0);
  for (const auto& effect : effects) {
    data << 0 << 768;
    for (int i = 0; i < 14; ++i)
      data << 0;
    data << effect[0] << effect[1] << effect[2];
  }
  const auto bytes = data.Get();
  return ToneCurve(std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                    bytes.size()));
}

class ImageResourcesUnitTest : public ::testing::Test {
 protected:
  void ExpectLoad(const std::string& name) {
    const fs::path path =
        fs::path("assets") / (name.substr(0, name.find('?')) + ".g00");
    EXPECT_CALL(*scanner_, FindFile(name, kImageExtensions))
        .WillOnce(Return(path));
    // Construct on invocation so the mock does not retain a surface reference.
    EXPECT_CALL(*backend_, LoadSurface(path)).WillOnce([](const fs::path&) {
      return std::make_shared<SDLSurface>(Size(1, 1));
    });
  }

  std::shared_ptr<StrictMock<MockAssetScanner>> scanner_ =
      std::make_shared<StrictMock<MockAssetScanner>>();
  std::shared_ptr<StrictMock<MockGraphicsBackend>> backend_ =
      std::make_shared<StrictMock<MockGraphicsBackend>>();
  ImageResources images_{scanner_, backend_, ToneCurve()};
};

TEST_F(ImageResourcesUnitTest, RejectsNullDependencies) {
  EXPECT_THROW(ImageResources(nullptr, backend_, ToneCurve()),
               std::invalid_argument);
  EXPECT_THROW(ImageResources(scanner_, nullptr, ToneCurve()),
               std::invalid_argument);
  EXPECT_THROW(ImageResources(nullptr, nullptr, ToneCurve()),
               std::invalid_argument);
}

TEST_F(ImageResourcesUnitTest, LoadsSurfaceFromResolvedPath) {
  const fs::path path = fs::path("assets") / "background.pdt";
  const auto surface = std::make_shared<SDLSurface>(Size(1, 1));
  InSequence sequence;
  EXPECT_CALL(*scanner_, FindFile("background", kImageExtensions))
      .WillOnce(Return(path));
  EXPECT_CALL(*backend_, LoadSurface(path)).WillOnce(Return(surface));

  EXPECT_EQ(images_.LoadSurfaceFromFile("background"), surface);
}

TEST_F(ImageResourcesUnitTest, PropagatesScannerError) {
  const std::string message = "Scanner could not find background";
  EXPECT_CALL(*scanner_, FindFile("background", kImageExtensions))
      .WillOnce(Return(make_unexpected(std::runtime_error(message))));

  EXPECT_THAT([&] { images_.GetSurfaceNamed("background"); },
              ::testing::ThrowsMessage<std::runtime_error>(message));
  EXPECT_EQ(images_.FindCached("background"), nullptr);
  EXPECT_TRUE(images_.loaded_images().empty());
}

TEST_F(ImageResourcesUnitTest, RejectsEmptyResolvedPath) {
  EXPECT_CALL(*scanner_, FindFile("background", kImageExtensions))
      .WillOnce(Return(fs::path()));

  EXPECT_THROW(images_.GetSurfaceNamed("background"), std::runtime_error);
  EXPECT_EQ(images_.FindCached("background"), nullptr);
  EXPECT_TRUE(images_.loaded_images().empty());
}

TEST_F(ImageResourcesUnitTest, FirstNamedLookupLoadsAndCachesSurface) {
  ExpectLoad("background");

  const auto surface = images_.GetSurfaceNamed("background");

  ASSERT_NE(surface, nullptr);
  EXPECT_EQ(images_.FindCached("background"), surface);
}

TEST_F(ImageResourcesUnitTest, SecondNamedLookupReturnsIdenticalCachedSurface) {
  ExpectLoad("background");
  const auto first = images_.GetSurfaceNamed("background");

  EXPECT_EQ(images_.GetSurfaceNamed("background"), first);
  EXPECT_EQ(images_.loaded_images().size(), 1);
}

TEST_F(ImageResourcesUnitTest, MissingCacheLookupDoesNotLoad) {
  EXPECT_EQ(images_.FindCached("missing"), nullptr);
  EXPECT_TRUE(images_.loaded_images().empty());
}

TEST_F(ImageResourcesUnitTest, DirectLoadsReturnNewSurfacesWithoutCaching) {
  ExpectLoad("background");
  const auto first = images_.LoadSurfaceFromFile("background");
  ExpectLoad("background");
  const auto second = images_.LoadSurfaceFromFile("background");

  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_NE(first, second);
  EXPECT_EQ(images_.FindCached("background"), nullptr);
}

TEST_F(ImageResourcesUnitTest, DirectLoadDoesNotReuseOrReplaceCachedSurface) {
  ExpectLoad("background");
  const auto cached = images_.GetSurfaceNamed("background");
  ExpectLoad("background");

  const auto direct = images_.LoadSurfaceFromFile("background");

  EXPECT_NE(direct, cached);
  EXPECT_EQ(images_.FindCached("background"), cached);
}

TEST_F(ImageResourcesUnitTest, PreloadPopulatesEmptySlotAndServesNamedLookups) {
  EXPECT_EQ(images_.FindPreloaded("background"), nullptr);
  ExpectLoad("background");

  images_.PreloadImage(4, "background");

  const auto preloaded = images_.FindPreloaded("background");
  ASSERT_NE(preloaded, nullptr);
  EXPECT_EQ(images_.GetSurfaceNamed("background"), preloaded);
  EXPECT_EQ(images_.FindCached("background"), nullptr);
}

TEST_F(ImageResourcesUnitTest, PreloadReusesCachedSurfaceWithoutLoading) {
  ExpectLoad("background");
  const auto cached = images_.GetSurfaceNamed("background");

  images_.PreloadImage(0, "background");

  EXPECT_EQ(images_.FindPreloaded("background"), cached);
  EXPECT_EQ(images_.loaded_images().size(), 1);
}

TEST_F(ImageResourcesUnitTest, ClearingOnePreloadSlotPreservesOtherSlots) {
  ExpectLoad("first");
  images_.PreloadImage(0, "first");
  ExpectLoad("second");
  images_.PreloadImage(5, "second");
  const std::weak_ptr<SDLSurface> first = images_.FindPreloaded("first");
  const auto second = images_.FindPreloaded("second");

  images_.ClearPreloadedImage(0);

  EXPECT_EQ(images_.FindPreloaded("first"), nullptr);
  EXPECT_TRUE(first.expired());
  EXPECT_EQ(images_.FindPreloaded("second"), second);
}

TEST_F(ImageResourcesUnitTest, ClearingAllPreloadSlotsReleasesEverySurface) {
  std::vector<std::weak_ptr<SDLSurface>> surfaces;
  for (const auto* name : {"first", "second", "third"}) {
    ExpectLoad(name);
    images_.PreloadImage(surfaces.size(), name);
    surfaces.push_back(images_.FindPreloaded(name));
  }

  images_.ClearPreloadedImages();

  for (const auto* name : {"first", "second", "third"})
    EXPECT_EQ(images_.FindPreloaded(name), nullptr);
  for (const auto& surface : surfaces)
    EXPECT_TRUE(surface.expired());
}

TEST_F(ImageResourcesUnitTest, ReplacingPreloadSlotReleasesOldSurface) {
  ExpectLoad("first");
  images_.PreloadImage(2, "first");
  const std::weak_ptr<SDLSurface> first = images_.FindPreloaded("first");
  ExpectLoad("replacement");

  images_.PreloadImage(2, "replacement");

  EXPECT_EQ(images_.FindPreloaded("first"), nullptr);
  EXPECT_TRUE(first.expired());
  EXPECT_NE(images_.FindPreloaded("replacement"), nullptr);
}

TEST_F(ImageResourcesUnitTest, RejectsInvalidPreloadSlotBeforeLoading) {
  const auto invalid = std::numeric_limits<std::size_t>::max();

  EXPECT_THROW(images_.PreloadImage(invalid, "background"), std::out_of_range);
  EXPECT_THROW(images_.PreloadImage(invalid - 1, "background"),
               std::out_of_range);
  EXPECT_EQ(images_.FindPreloaded("background"), nullptr);
}

TEST_F(ImageResourcesUnitTest, LoadedImagesTracksWeakReferencesOnly) {
  ExpectLoad("background");
  auto surface = images_.LoadSurfaceFromFile("background");

  ASSERT_EQ(images_.loaded_images().size(), 1);
  EXPECT_EQ(images_.loaded_images()[0].first, "background");
  EXPECT_EQ(images_.loaded_images()[0].second.lock(), surface);
  EXPECT_EQ(surface.use_count(), 1);

  surface.reset();

  EXPECT_TRUE(images_.loaded_images()[0].second.expired());
}

TEST_F(ImageResourcesUnitTest,
       SuccessfulLoadPrunesExpiredEntriesAndKeepsLiveOnes) {
  ExpectLoad("expired");
  auto expired = images_.LoadSurfaceFromFile("expired");
  ExpectLoad("live");
  const auto live = images_.LoadSurfaceFromFile("live");
  expired.reset();
  ASSERT_EQ(images_.loaded_images().size(), 2);
  ASSERT_TRUE(images_.loaded_images()[0].second.expired());
  ExpectLoad("new");

  const auto newest = images_.LoadSurfaceFromFile("new");

  ASSERT_EQ(images_.loaded_images().size(), 2);
  EXPECT_EQ(images_.loaded_images()[0].first, "live");
  EXPECT_EQ(images_.loaded_images()[0].second.lock(), live);
  EXPECT_EQ(images_.loaded_images()[1].first, "new");
  EXPECT_EQ(images_.loaded_images()[1].second.lock(), newest);
}

TEST_F(ImageResourcesUnitTest, FailedLoadsDoNotPruneTrackedImages) {
  ExpectLoad("expired");
  images_.LoadSurfaceFromFile("expired");
  EXPECT_CALL(*scanner_, FindFile("missing", kImageExtensions))
      .WillOnce(Return(make_unexpected(std::runtime_error("Missing image"))));

  EXPECT_THROW(images_.LoadSurfaceFromFile("missing"), std::runtime_error);
  ASSERT_EQ(images_.loaded_images().size(), 1);
  EXPECT_TRUE(images_.loaded_images()[0].second.expired());

  const fs::path path = fs::path("assets") / "broken.g00";
  EXPECT_CALL(*scanner_, FindFile("broken", kImageExtensions))
      .WillOnce(Return(path));
  EXPECT_CALL(*backend_, LoadSurface(path))
      .WillOnce(Throw(std::runtime_error("Decode failed")));

  EXPECT_THROW(images_.LoadSurfaceFromFile("broken"), std::runtime_error);
  ASSERT_EQ(images_.loaded_images().size(), 1);
  EXPECT_EQ(images_.loaded_images()[0].first, "expired");
}

TEST_F(ImageResourcesUnitTest, ToneCurveTransformsRgbAndPreservesAlpha) {
  for (const int index : {10, 20}) {
    SCOPED_TRACE(index);
    ImageResources images(scanner_, backend_, MakeToneCurves());
    const auto surface = std::make_shared<SDLSurface>(Size(256, 1));
    for (int i = 0; i < 256; ++i)
      surface->Fill(RGBAColour(i, 255 - i, (i + 37) % 256, (i + 73) % 256),
                    Rect(i, 0, Size(1, 1)));
    const auto name = "background?" + std::to_string(index);
    const fs::path path = fs::path("assets") / "background.g00";
    EXPECT_CALL(*scanner_, FindFile(name, kImageExtensions))
        .WillOnce(Return(path));
    EXPECT_CALL(*backend_, LoadSurface(path)).WillOnce(Return(surface));

    EXPECT_EQ(images.LoadSurfaceFromFile(name), surface);

    for (int i = 0; i < 256; ++i) {
      SCOPED_TRACE(i);
      const int r = i, g = 255 - i, b = (i + 37) % 256, a = (i + 73) % 256;
      const auto expected = index == 10
                                ? RGBAColour(255 - r, g / 2, (b + 17) % 256, a)
                                : RGBAColour(r / 2, (g + 29) % 256, 255 - b, a);
      EXPECT_EQ(surface->GetPixelAt(Point(i, 0)), expected);
    }
  }
}

TEST_F(ImageResourcesUnitTest, RejectsInvalidToneCurveIndicesAndQueries) {
  ImageResources images(scanner_, backend_, MakeToneCurves());
  for (const auto* query :
       {"", "invalid", "0", "9", "-10", "30", "10junk", "10?20", "10.5", "+10",
        " 10", "2147483647", "999999999999999999999999"}) {
    SCOPED_TRACE(query);
    const auto name = std::string("background?") + query;
    ExpectLoad(name);

    EXPECT_THROW(images.GetSurfaceNamed(name), std::runtime_error);
    EXPECT_EQ(images.FindCached(name), nullptr);
  }
}

TEST_F(ImageResourcesUnitTest, RejectsToneCurveIndexWhenNoEffectsAreAvailable) {
  ExpectLoad("background?10");

  EXPECT_THROW(images_.LoadSurfaceFromFile("background?10"),
               std::runtime_error);
}
