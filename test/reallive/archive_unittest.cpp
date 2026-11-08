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

#include "libreallive/archive.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string_view>

#include "reallive/scenario_test_support.hpp"
#include "utilities/file.hpp"
#include "utilities/game_detection.hpp"

namespace {
namespace fs = std::filesystem;
using namespace reallive_test;

class RealLiveArchiveTest : public ::testing::Test {
 protected:
  fs::path Path(std::string_view name = "") const {
    return temporary_.dir() / name;
  }

  ScopedTemporary temporary_;
};

TEST_F(RealLiveArchiveTest, LoadsDetectedConfiguredArchive) {
  // Detection selects a configured archive outside the working directory;
  // the archive reader loads its scenario successfully using that path.
  WriteFile(Path("Gameexe.ini"), "#FOLDNAME.TXT = \"SCN\" : \"STORY.PAK\"");
  WriteFile(Path("SCN/Story.pak"), MakeArchive());
  const auto game = DetectGameRoot(Path());
  libreallive::Archive archive(game.seen_archive, "");
  const auto* scene = archive.GetScenario(1);
  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(scene->scene_number(), 1);
}

TEST_F(RealLiveArchiveTest, LoadsDetectedLooseOnlyScenes) {
  // A detected loose-only directory loads scenes without a SEEN.TXT archive.
  WriteFile(Path("Gameexe.ini"), "");
  WriteFile(Path("DAT/sEeN0007.TxT"), MakeScenario());
  const auto game = DetectGameRoot(Path());
  libreallive::Archive archive(game.seen_directory, "");
  const auto* scene = archive.GetScenario(7);
  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(scene->scene_number(), 7);
}

TEST_F(RealLiveArchiveTest, LooseSceneOverridesAdjacentArchive) {
  // A loose file beside a data-folder archive replaces its archived scenario
  // even though the working directory is elsewhere.
  WriteFile(Path("DAT/Seen.txt"), MakeArchive());
  auto override_scene = MakeScenario();
  PutU32(override_scene, 0x1c4, 42);
  WriteFile(Path("DAT/SEEN0001.TXT"), override_scene);
  libreallive::Archive archive(Path("DAT/Seen.txt"), "");
  const auto* scene = archive.GetScenario(1);
  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(scene->header.savepoint_message_, 42);
}

TEST_F(RealLiveArchiveTest, LooseCollectionSelectsLowestScenarioId) {
  // Loose scenes are indexed by number, independently of discovery order.
  WriteFile(Path("SEEN0042.TXT"), MakeScenario());
  WriteFile(Path("SEEN0007.TXT"), MakeScenario());
  libreallive::Archive archive(Path(), "");
  EXPECT_EQ(archive.GetFirstScenarioID(), 7);
}

TEST_F(RealLiveArchiveTest, LoadsHeaderIdentifiedSceneWithThreeDigitName) {
  // Detection uses a RealLive header before filename width; loading accepts
  // the same scene number without requiring a four-digit spelling.
  WriteFile(Path("Gameexe.ini"), "");
  WriteFile(Path("SEEN007.TXT"), MakeScenario());
  const auto game = DetectGameRoot(Path());
  libreallive::Archive archive(game.seen_directory, "");
  EXPECT_NE(archive.GetScenario(7), nullptr);
}

TEST_F(RealLiveArchiveTest, EmptyCollectionReportsNoScenarios) {
  // An empty loose collection reports failure instead of dereferencing an end.
  libreallive::Archive archive(Path(), "");
  EXPECT_THROW(archive.GetFirstScenarioID(), std::runtime_error);
}

TEST_F(RealLiveArchiveTest, SceneNamedDirectoryIsIgnored) {
  // A directory with a scene filename cannot become a loose-file override.
  WriteFile(Path("Seen.txt"), MakeArchive());
  fs::create_directory(Path("SEEN0001.TXT"));
  libreallive::Archive archive(Path("Seen.txt"), "");
  EXPECT_NE(archive.GetScenario(1), nullptr);
}

TEST_F(RealLiveArchiveTest, LoadsArchiveByRelativeFilename) {
  // Existing callers can still load an archive named relative to the current
  // directory, including the adjacent loose-scene scan.
  WriteFile(Path("Seen.txt"), MakeArchive());
  ScopedCurrentPath current{Path()};
  libreallive::Archive archive(fs::path("Seen.txt"), "");
  EXPECT_NE(archive.GetScenario(1), nullptr);
}

TEST_F(RealLiveArchiveTest, UnrepresentableLooseSceneIdIsIgnored) {
  // An oversized numeric filename cannot overflow the scene index parser or
  // prevent loading the existing archived scene.
  WriteFile(Path("Seen.txt"), MakeArchive());
  WriteFile(Path("SEEN99999999999999999999.TXT"), MakeScenario());
  libreallive::Archive archive(Path("Seen.txt"), "");
  EXPECT_EQ(archive.GetFirstScenarioID(), 1);
}

}  // namespace
