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

#include "movie_test_support.hpp"
#include "serialization_test_helpers.hpp"
#include "utilities/file.hpp"

namespace {

class MovieDrawerArchiveTest : public ::testing::Test {};

TEST_F(MovieDrawerArchiveTest, LoopingMovieRestartsAtBeginning) {
  // A looping movie saved on its second frame restores the first frame and
  // loading clock.
  serialization_test::ForEachFormat([&](auto format) {
    using namespace std::chrono_literals;
    MovieTestHost saving, loading;
    auto source = saving.Make();
    const auto first = source->GetPixelColor(Point(), 0);
    dynamic_cast<ObjectMovieData&>(*source).Seek(150);
    ASSERT_NE(source->GetPixelColor(Point(), 0), first);
    loading.clock->SetElapsed(9000ms);
    std::unique_ptr<GraphicsObjectData> target;
    serialization_test::RoundTrip(source, target, format, {},
                                  loading.Context());
    auto* restored = dynamic_cast<ObjectMovieData*>(target.get());
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->GetSeekTime(), 0);
    EXPECT_EQ(restored->GetPixelColor(Point(), 0), first);
    loading.clock->AdvanceTime(150ms);
    restored->Execute();
    EXPECT_EQ(restored->GetSeekTime(), 150);
    EXPECT_NE(restored->GetPixelColor(Point(), 0), first);
  });
}

TEST_F(MovieDrawerArchiveTest, RetainedMovieRestartsAtBeginning) {
  // A non-auto-free movie retains a visible frame and restarts its playback
  // position.
  serialization_test::ForEachFormat([&](auto format) {
    MovieTestHost host;
    auto source = host.Make(false, false);
    dynamic_cast<ObjectMovieData&>(*source).Seek(150);
    std::unique_ptr<GraphicsObjectData> target;
    serialization_test::RoundTrip(source, target, format, {}, host.Context());
    ASSERT_NE(dynamic_cast<ObjectMovieData*>(target.get()), nullptr);
    GraphicsObject object;
    EXPECT_EQ(target->PixelWidth(object), 16);
    EXPECT_EQ(dynamic_cast<ObjectMovieData&>(*target).GetSeekTime(), 0);
  });
}

TEST_F(MovieDrawerArchiveTest, PreparedMovieStaysPaused) {
  // A prepared movie restores its first frame without advancing until resumed.
  serialization_test::ForEachFormat([&](auto format) {
    using namespace std::chrono_literals;
    MovieTestHost host;
    auto source = host.Make(false, true, true);
    std::unique_ptr<GraphicsObjectData> target;
    serialization_test::RoundTrip(source, target, format, {}, host.Context());
    auto* restored = dynamic_cast<ObjectMovieData*>(target.get());
    ASSERT_NE(restored, nullptr);
    host.clock->AdvanceTime(50ms);
    restored->Execute();
    EXPECT_EQ(restored->GetSeekTime(), 0);
    restored->Resume();
    host.clock->AdvanceTime(25ms);
    restored->Execute();
    EXPECT_EQ(restored->GetSeekTime(), 25);
  });
}

TEST_F(MovieDrawerArchiveTest, AutoFreeOneShotDoesNotReopen) {
  // An ordinary one-shot movie loads without requiring the source asset or
  // backend.
  serialization_test::ForEachFormat([&](auto format) {
    MovieTestHost host;
    auto source = host.Make(false, true);
    std::unique_ptr<GraphicsObjectData> target;
    serialization_test::RoundTrip(source, target, format);
    GraphicsObject object;
    ASSERT_TRUE(target);
    EXPECT_EQ(target->PixelWidth(object), 0);
    EXPECT_FALSE(dynamic_cast<ObjectMovieData&>(*target).CheckMovie());
  });
}

TEST_F(MovieDrawerArchiveTest, FinishedMovieRemainsEmpty) {
  // An auto-freed movie remains empty through serialization and cloning.
  serialization_test::ForEachFormat([&](auto format) {
    using namespace std::chrono_literals;
    MovieTestHost host;
    auto source = host.Make(false, true);
    host.clock->AdvanceTime(250ms);
    source->Execute();
    GraphicsObject object;
    ASSERT_EQ(source->PixelWidth(object), 0);
    std::unique_ptr<GraphicsObjectData> target;
    serialization_test::RoundTrip(source, target, format);
    EXPECT_EQ(target->PixelWidth(object), 0);
    EXPECT_EQ(target->Clone()->PixelWidth(object), 0);
  });
}

TEST_F(MovieDrawerArchiveTest, RelocatesGameRelativeMovie) {
  // A saved game-relative movie reloads from a different root after the
  // original is removed.
  serialization_test::ForEachFormat([&](auto format) {
    ScopedTemporary original, relocated;
    MovieTestHost host;
    std::filesystem::copy_file(host.path, original.dir() / "movie.omv");
    std::filesystem::copy_file(host.path, relocated.dir() / "movie.omv");
    host.path = original.dir() / "movie.omv";
    auto source = host.Make();
    auto saving = host.Context();
    saving.game_root = original.dir();
    auto bytes = serialization_test::Save(source, format, saving);
    std::filesystem::remove(host.path);
    auto loading = host.Context();
    loading.game_root = relocated.dir();
    std::unique_ptr<GraphicsObjectData> target;
    serialization_test::Load(bytes, target, format, loading);
    GraphicsObject object;
    ASSERT_TRUE(target);
    EXPECT_EQ(target->PixelWidth(object), 16);
  });
}

TEST_F(MovieDrawerArchiveTest, MissingBackendRejectsRequiredMovie) {
  // A movie that must reopen fails without graphics services.
  serialization_test::ForEachFormat([&](auto format) {
    MovieTestHost host;
    auto source = host.Make();
    auto bytes = serialization_test::Save(
        dynamic_cast<ObjectMovieData&>(*source), format);
    auto target = host.Make();
    EXPECT_THROW(serialization_test::Load(
                     bytes, dynamic_cast<ObjectMovieData&>(*target), format),
                 std::runtime_error);
  });
}

TEST_F(MovieDrawerArchiveTest, MissingFileRejectsRequiredMovie) {
  // An unavailable required movie fails loading rather than silently
  // disappearing.
  serialization_test::ForEachFormat([&](auto format) {
    ScopedTemporary directory;
    MovieTestHost host;
    std::filesystem::copy_file(host.path, directory.dir() / "movie.omv");
    host.path = directory.dir() / "movie.omv";
    auto source = host.Make();
    auto bytes = serialization_test::Save(
        dynamic_cast<ObjectMovieData&>(*source), format);
    auto target = host.Make();
    std::filesystem::remove(host.path);
    EXPECT_THROW(
        serialization_test::Load(bytes, dynamic_cast<ObjectMovieData&>(*target),
                                 format, host.Context()),
        std::runtime_error);
  });
}

}  // namespace
