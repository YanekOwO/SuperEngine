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
#include "core/object.hpp"
#include "core/object_internal/drawer/file.hpp"
#include "image_archive_test_support.hpp"
#include "log_capture.hpp"
#include "mock_clock.hpp"
#include "serialization_test_helpers.hpp"
#include "utilities/file.hpp"

#include <format>

namespace {

class FileDrawerArchiveTest : public ::testing::Test {};

TEST_F(FileDrawerArchiveTest, SharedFilenamesRetainSurfaceIdentity) {
  // Two polymorphic drawers naming one asset share a single reloaded surface.
  serialization_test::ForEachFormat([&](auto format) {
    ImageArchiveHost loading;
    auto image = std::make_shared<SDLSurface>(Size(1, 1));
    using Drawers = std::pair<std::unique_ptr<GraphicsObjectData>,
                              std::unique_ptr<GraphicsObjectData>>;
    Drawers source{std::make_unique<GraphicsObjectOfFile>("picture", image),
                   std::make_unique<GraphicsObjectOfFile>("picture", image)},
        target;
    serialization_test::RoundTrip(source, target, format, {},
                                  {.images = &loading.images});
    ASSERT_NE(dynamic_cast<GraphicsObjectOfFile*>(target.first.get()), nullptr);
    ASSERT_NE(dynamic_cast<GraphicsObjectOfFile*>(target.second.get()),
              nullptr);
    GraphicsObject properties;
    EXPECT_EQ(target.first->CurrentSurface(properties),
              target.second->CurrentSurface(properties));
    EXPECT_EQ(loading.loads, 1);
  });
}

TEST_F(FileDrawerArchiveTest, NamedAssetUsesDestinationPixels) {
  // A modified source image is replaced by the destination asset's pixels.
  serialization_test::ForEachFormat([&](auto format) {
    ImageArchiveHost loading;
    loading.color = RGBAColour(90, 80, 70, 255);
    auto image = std::make_shared<SDLSurface>(Size(1, 1));
    image->Fill(RGBAColour(1, 2, 3, 4));
    GraphicsObjectOfFile source("picture", image), target("old", image);
    serialization_test::RoundTrip(source, target, format, {},
                                  {.images = &loading.images});
    EXPECT_EQ(target.GetPixelColor(Point(), 0), loading.color);
  });
}

TEST_F(FileDrawerArchiveTest, NamedAssetUsesDestinationRegions) {
  // Reloaded pattern origins and pixel regions come from the destination file.
  serialization_test::ForEachFormat([&](auto format) {
    ImageArchiveHost loading;
    GrpRect region{
        .rect = Rect::REC(0, 0, 2, 2),
        .originX = 7,
        .originY = -3,
        .pixel_region = PixelRegion{Rect::REC(1, 0, 1, 2), Point(4, 5)}};
    loading.regions = {region};
    auto image = std::make_shared<SDLSurface>(Size(1, 1));
    GraphicsObjectOfFile source("picture", image), target("old", image);
    serialization_test::RoundTrip(source, target, format, {},
                                  {.images = &loading.images});
    GraphicsObject object;
    const auto& restored = static_cast<GraphicsObjectData&>(target)
                               .CurrentSurface(object)
                               ->GetPattern(0);
    EXPECT_EQ(restored.rect, region.rect);
    EXPECT_EQ(restored.originX, 7);
    EXPECT_EQ(restored.originY, -3);
    ASSERT_TRUE(restored.pixel_region);
    EXPECT_EQ(restored.pixel_region->rect, region.pixel_region->rect);
    EXPECT_EQ(restored.pixel_region->origin, Point(4, 5));
  });
}

TEST_F(FileDrawerArchiveTest, ResolvesFilenameUsingDestinationScanner) {
  // A logical asset name resolves under the loading host's asset directory.
  serialization_test::ForEachFormat([&](auto format) {
    ImageArchiveHost loading;
    loading.root = "relocated/images";
    auto image = std::make_shared<SDLSurface>(Size(1, 1));
    GraphicsObjectOfFile source("picture", image), target("old", image);
    serialization_test::RoundTrip(source, target, format, {},
                                  {.images = &loading.images});
    EXPECT_EQ(loading.loaded_path, loading.root / "picture.g00");
  });
}

TEST_F(FileDrawerArchiveTest, SavingOmitsPixels) {
  // Changing pixels and surface size leaves an asset owner's archive unchanged.
  serialization_test::ForEachFormat([&](auto format) {
    auto image = std::make_shared<SDLSurface>(Size(1, 1));
    GraphicsObjectOfFile source("picture", image);
    const auto before = serialization_test::Save(source, format);
    image->Allocate(Size(32, 32));
    image->Fill(RGBAColour(90, 80, 70, 60));
    EXPECT_EQ(serialization_test::Save(source, format), before);
  });
}

TEST_F(FileDrawerArchiveTest, LoadingDoesNotReplaceLiveCachedImage) {
  // Reconstruction reloads a fresh image while preserving the live cache entry.
  serialization_test::ForEachFormat([&](auto format) {
    ImageArchiveHost loading;
    auto live = loading.images.GetSurfaceNamed("picture");
    loading.color = RGBAColour(90, 80, 70, 255);
    GraphicsObjectOfFile source("picture", live), target("old", live);
    serialization_test::RoundTrip(source, target, format, {},
                                  {.images = &loading.images});
    GraphicsObject object;
    EXPECT_NE(static_cast<GraphicsObjectData&>(target).CurrentSurface(object),
              live);
    EXPECT_EQ(loading.images.FindCached("picture"), live);
    EXPECT_EQ(live->GetPixelAt(Point()), RGBAColour(12, 34, 56, 78));
  });
}

TEST_F(FileDrawerArchiveTest, SeparateArchivesReloadIndependently) {
  // Reusing reconstruction services across archives cannot reuse old pixels.
  serialization_test::ForEachFormat([&](auto format) {
    ImageArchiveHost loading;
    auto image = std::make_shared<SDLSurface>(Size(1, 1));
    GraphicsObjectOfFile source("picture", image), first("old", image),
        second("old", image);
    auto bytes = serialization_test::Save(source, format);
    SerializationContext context{.images = &loading.images};
    context.LoadSurface("picture");
    serialization_test::Load(bytes, first, format, context);
    loading.color = RGBAColour(90, 80, 70, 255);
    serialization_test::Load(bytes, second, format, context);
    EXPECT_EQ(second.GetPixelColor(Point(), 0), loading.color);
    EXPECT_EQ(loading.loads, 3);
  });
}

TEST_F(FileDrawerArchiveTest, NamedAssetRequiresImageServices) {
  // A named owner fails reconstruction when no image loader is supplied.
  serialization_test::ForEachFormat([&](auto format) {
    auto image = std::make_shared<SDLSurface>(Size(1, 1));
    GraphicsObjectOfFile source("picture", image), target("old", image);
    auto bytes = serialization_test::Save(source, format);
    EXPECT_THROW(serialization_test::Load(bytes, target, format),
                 std::runtime_error);
  });
}

TEST_F(FileDrawerArchiveTest, MissingNamedAssetPropagatesFailure) {
  // An unavailable destination asset rejects reconstruction instead of
  // embedding.
  serialization_test::ForEachFormat([&](auto format) {
    ImageArchiveHost loading;
    loading.fail = true;
    auto image = std::make_shared<SDLSurface>(Size(1, 1));
    GraphicsObjectOfFile source("missing", image), target("old", image);
    auto bytes = serialization_test::Save(source, format);
    EXPECT_THROW(serialization_test::Load(bytes, target, format,
                                          {.images = &loading.images}),
                 std::runtime_error);
  });
}

TEST_F(FileDrawerArchiveTest, RejectsUnknownImageSource) {
  // An unknown source discriminator is rejected before resource reconstruction.
  serialization_test::ForEachFormat([&](auto format) {
    GraphicsObjectOfFile::Source invalid{
        .kind = static_cast<GraphicsObjectOfFile::SourceKind>(99)};
    GraphicsObjectOfFile source(invalid, nullptr), target("old", nullptr);
    auto bytes = serialization_test::Save(source, format);
    EXPECT_THROW(serialization_test::Load(bytes, target, format),
                 std::runtime_error);
  });
}

TEST_F(FileDrawerArchiveTest, CaptureRestoresMetadataWithoutPixels) {
  // A capture object retains parameters but reloads as a safe empty drawer.
  serialization_test::ForEachFormat([&](auto format) {
    auto image = std::make_shared<SDLSurface>(Size(2, 1));
    GraphicsObject source, target;
    source.Param().SetX(17);
    source.SetDrawer(std::make_unique<GraphicsObjectOfFile>(
        GraphicsObjectOfFile::Source{}, image));
    serialization_test::RoundTrip(source, target, format);
    EXPECT_EQ(target.Param().x(), 17);
    EXPECT_TRUE(target.FilePath().empty());
    ASSERT_TRUE(target.HasDrawer());
    auto& drawer = target.GetDrawer();
    EXPECT_FALSE(drawer.CurrentSurface(target));
    EXPECT_EQ(drawer.PixelWidth(target), 0);
    EXPECT_EQ(drawer.PixelHeight(target), 0);
    EXPECT_EQ(drawer.SrcRect(target), Rect());
    EXPECT_EQ(drawer.DstOrigin(target), Point());
    EXPECT_EQ(drawer.GetPixelColor(Point(), 0), RGBAColour::Clear());
    EXPECT_FALSE(drawer.GetAnimator());
    drawer.PlaySet(100);
    drawer.Execute();
    EXPECT_FALSE(std::as_const(drawer).GetAnimator());
  });
}

TEST_F(FileDrawerArchiveTest, CaptureSaveEmitsDiagnostic) {
  // Saving a capture warns about its unsupported pixels and still succeeds.
  serialization_test::ForEachFormat([&](auto format) {
    LogCapture log;
    SetupLogging(Severity::Warn);
    GraphicsObjectOfFile source(GraphicsObjectOfFile::Source{},
                                std::make_shared<SDLSurface>(Size(1, 1)));
    EXPECT_FALSE(serialization_test::Save(source, format).empty());
    EXPECT_THAT(log.Str(), testing::HasSubstr("Capture objects"));
  });
}

TEST_F(FileDrawerArchiveTest, AnimationRetainsElapsedTime) {
  // A playing file animation retains elapsed time on the loading clock.
  serialization_test::ForEachFormat([&](auto format) {
    using namespace std::chrono_literals;
    ImageArchiveHost loading;
    auto clock = std::make_shared<MockClock>();
    std::unique_ptr<GraphicsObjectData>
        source = std::make_unique<GraphicsObjectOfFile>(
            "picture", std::make_shared<SDLSurface>(Size(2, 1))),
        target;
    *source->GetAnimator() = Animator(clock);
    source->PlaySet(100);
    clock->AdvanceTime(25ms);
    auto target_clock = std::make_shared<MockClock>();
    serialization_test::RoundTrip(
        source, target, format, {},
        {.images = &loading.images, .clock = target_clock});
    ASSERT_TRUE(target);
    EXPECT_TRUE(target->GetAnimator()->IsPlaying());
    target_clock->AdvanceTime(15ms);
    EXPECT_EQ(target->GetAnimator()->GetAnimationTime(), 40ms);
  });
}

// ------------------------------------------------------------------------------

class ThumbnailDrawerArchiveTest : public ::testing::Test {
 protected:
  void WriteThumbnail(int number, bool local, RGBAColour color) {
    const auto name = local ? std::format("{:010}.bmp", number)
                            : std::format("{:04}.bmp", number);
    SDLSurface image(Size(1, 1));
    image.Fill(color);
    ASSERT_TRUE(
        SDL_SaveBMP(image.RawSurface(), (directory.dir() / name).c_str()));
  }

  ScopedTemporary directory;
  CaptureManager captures{
      {},
      directory.dir(),
      {},
      [](const std::filesystem::path& path) {
        return std::make_shared<SDLSurface>(SDL_LoadBMP(path.c_str()));
      }};
};

TEST_F(ThumbnailDrawerArchiveTest, ReloadsSaveThumbnailByNumber) {
  // A save thumbnail reloads its current file instead of the saved object's
  // pixels.
  serialization_test::ForEachFormat([&](auto format) {
    WriteThumbnail(7, false, RGBAColour(90, 80, 70, 255));
    auto image = std::make_shared<SDLSurface>(Size(2, 2));
    GraphicsObjectOfFile source(
        {.kind = GraphicsObjectOfFile::SourceKind::SaveThumbnail,
         .thumbnail_number = 7},
        image),
        target("old", image);
    serialization_test::RoundTrip(source, target, format, {},
                                  {.captures = &captures});
    EXPECT_EQ(target.GetPixelColor(Point(), 0), RGBAColour(90, 80, 70, 255));
  });
}

TEST_F(ThumbnailDrawerArchiveTest, LocalThumbnailUsesLocalNamespace) {
  // Equal local and save identifiers resolve to their distinct thumbnail files.
  serialization_test::ForEachFormat([&](auto format) {
    WriteThumbnail(7, false, RGBAColour(1, 2, 3, 255));
    WriteThumbnail(7, true, RGBAColour(90, 80, 70, 255));
    auto image = std::make_shared<SDLSurface>(Size(2, 2));
    GraphicsObjectOfFile source(
        {.kind = GraphicsObjectOfFile::SourceKind::LocalThumbnail,
         .thumbnail_number = 7},
        image),
        target("old", image);
    serialization_test::RoundTrip(source, target, format, {},
                                  {.captures = &captures});
    EXPECT_EQ(target.GetPixelColor(Point(), 0), RGBAColour(90, 80, 70, 255));
  });
}

TEST_F(ThumbnailDrawerArchiveTest, MissingThumbnailRestoresEmptyDrawer) {
  // A removed thumbnail clears the old surface without rejecting the owner.
  serialization_test::ForEachFormat([&](auto format) {
    auto image = std::make_shared<SDLSurface>(Size(2, 2));
    GraphicsObjectOfFile source(
        {.kind = GraphicsObjectOfFile::SourceKind::SaveThumbnail,
         .thumbnail_number = 99},
        image),
        target("old", image);
    serialization_test::RoundTrip(source, target, format, {},
                                  {.captures = &captures});
    GraphicsObject object;
    EXPECT_FALSE(
        static_cast<GraphicsObjectData&>(target).CurrentSurface(object));
    EXPECT_EQ(target.PixelWidth(object), 0);
  });
}

TEST_F(ThumbnailDrawerArchiveTest, ThumbnailRequiresCaptureServices) {
  // Thumbnail reconstruction fails clearly when its destination service is
  // absent.
  serialization_test::ForEachFormat([&](auto format) {
    auto image = std::make_shared<SDLSurface>(Size(2, 2));
    GraphicsObjectOfFile source(
        {.kind = GraphicsObjectOfFile::SourceKind::SaveThumbnail,
         .thumbnail_number = 7},
        image),
        target("old", image);
    auto bytes = serialization_test::Save(source, format);
    EXPECT_THROW(serialization_test::Load(bytes, target, format),
                 std::runtime_error);
  });
}

}  // namespace
