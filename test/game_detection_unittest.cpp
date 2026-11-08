// Copyright (C) 2026 RLVM contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "utilities/game_detection.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>

#include "reallive/scenario_test_support.hpp"
#include "utilities/file.hpp"

namespace {
namespace fs = std::filesystem;
using reallive_test::MakeArchive;
using reallive_test::MakeScenario;
using reallive_test::PutU32;

class GameDetectionTest : public ::testing::Test {
 protected:
  fs::path Path(std::string_view name = "") const {
    return temporary_.dir() / name;
  }

  void Write(std::string_view name, std::string_view data = "") {
    WriteFile(Path(name), data);
  }

  GameLayout Detect() const { return DetectGameRoot(temporary_.dir()); }

  ScopedTemporary temporary_;
};

TEST_F(GameDetectionTest, ScenePackIdentifiesSiglusWithoutConfiguration) {
  // A scene pack alone identifies Siglus even before configuration loading.
  Write("Scene.pck");
  EXPECT_EQ(Detect().engine, EngineKind::Siglus);
}

TEST_F(GameDetectionTest, GameexeDatIdentifiesSiglusWithoutScenePack) {
  // A binary configuration alone identifies Siglus even without scenes.
  Write("Gameexe.dat");
  EXPECT_EQ(Detect().engine, EngineKind::Siglus);
}

TEST_F(GameDetectionTest, SiglusMarkersIgnoreCase) {
  // Mixed-case game filenames still identify the Siglus engine.
  Write("sCeNe.PcK");
  EXPECT_EQ(Detect().engine, EngineKind::Siglus);
}

TEST_F(GameDetectionTest, SiglusTakesPriorityOverRealLive) {
  // Both engines' markers are present; Siglus wins within the selected root.
  Write("Scene.pck");
  Write("Gameexe.ini");
  Write("Seen.txt", MakeArchive());
  EXPECT_EQ(Detect().engine, EngineKind::Siglus);
}

TEST_F(GameDetectionTest, DirectoriesAreNotGameMarkers) {
  // Directories named after Siglus files cannot identify a game.
  fs::create_directory(Path("Scene.pck"));
  fs::create_directory(Path("Gameexe.dat"));
  EXPECT_EQ(Detect().engine, EngineKind::Unknown);
}

TEST_F(GameDetectionTest, EmptyRootIsUnknown) {
  // No engine files are installed; detection returns an explanatory unknown.
  const auto game = Detect();
  EXPECT_EQ(game.engine, EngineKind::Unknown);
  EXPECT_FALSE(game.evidence.empty());
}

TEST_F(GameDetectionTest, ConfigurationAloneDoesNotIdentifyRealLive) {
  // An ini without scenarios is insufficient to identify RealLive.
  Write("Gameexe.ini");
  EXPECT_EQ(Detect().engine, EngineKind::Unknown);
}

TEST_F(GameDetectionTest, ArchiveRequiresConfiguration) {
  // Even a recognizable archive needs a RealLive configuration beside it.
  Write("Seen.txt", MakeArchive());
  EXPECT_EQ(Detect().engine, EngineKind::Unknown);
}

TEST_F(GameDetectionTest, DetectsRootArchiveIgnoringCase) {
  // Mixed-case configuration and archive names resolve to their native paths.
  Write("gAmEeXe.InI");
  Write("sEeN.TxT", MakeArchive());
  const auto game = Detect();
  EXPECT_EQ(game.engine, EngineKind::RealLive);
  EXPECT_EQ(game.gameexe_ini, Path("gAmEeXe.InI"));
  EXPECT_EQ(game.seen_archive, Path("sEeN.TxT"));
}

TEST_F(GameDetectionTest, DetectsDatArchiveIgnoringDirectoryCase) {
  // A scenario archive in a mixed-case DAT directory identifies RealLive.
  Write("Gameexe.ini");
  Write("dAt/SeEn.TxT", MakeArchive());
  EXPECT_EQ(Detect().seen_archive, Path("dAt/SeEn.TxT"));
}

TEST_F(GameDetectionTest, DetectsConfiguredFolderAndArchiveName) {
  // A case-insensitive directive resolves nested Windows-style folder names.
  Write("Gameexe.ini",
        "  #foldname.txt = \"DATA\\SCN\" = 0 : \"STORY.PAK\"\r\n");
  Write("Data/Scn/Story.pak", MakeArchive());
  EXPECT_EQ(Detect().seen_archive, Path("Data/Scn/Story.pak"));
}

TEST_F(GameDetectionTest, EmptyConfiguredArchiveNameDefaultsToSeen) {
  // An empty archive name in FOLDNAME uses SEEN.TXT in the configured folder.
  Write("Gameexe.ini", "#FOLDNAME.TXT = \"SCN\" = 0 : \"\"\n");
  Write("SCN/Seen.txt", MakeArchive());
  EXPECT_EQ(Detect().seen_archive, Path("SCN/Seen.txt"));
}

TEST_F(GameDetectionTest, MissingConfiguredArchiveNameDefaultsToSeen) {
  // A directive containing only a folder also defaults to SEEN.TXT.
  Write("Gameexe.ini", "#FOLDNAME.TXT = \"SCN\"\n");
  Write("SCN/Seen.txt", MakeArchive());
  EXPECT_EQ(Detect().seen_archive, Path("SCN/Seen.txt"));
}

TEST_F(GameDetectionTest, ConfiguredArchiveNameFallsBackToRoot) {
  // A missing configured folder still permits its archive name in the root.
  Write("Gameexe.ini", "#FOLDNAME.TXT = \"MISSING\" = 0 : \"STORY.PAK\"\n");
  Write("Story.pak", MakeArchive());
  EXPECT_EQ(Detect().seen_archive, Path("Story.pak"));
}

TEST_F(GameDetectionTest, ConfiguredLocationTakesPriorityOverFallbacks) {
  // Multiple valid archives exist; the configured location wins.
  Write("Gameexe.ini", "#FOLDNAME.TXT = \"SCN\" = 0 : \"STORY.PAK\"\n");
  Write("SCN/Story.pak", MakeArchive());
  Write("STORY.PAK", MakeArchive());
  Write("DAT/Seen.txt", MakeArchive());
  Write("Seen.txt", MakeArchive());
  EXPECT_EQ(Detect().seen_archive, Path("SCN/Story.pak"));
}

TEST_F(GameDetectionTest, DatArchiveTakesPriorityOverRootArchive) {
  // Without a directive, DAT is searched before the root archive.
  Write("Gameexe.ini");
  Write("DAT/Seen.txt", MakeArchive());
  Write("Seen.txt", MakeArchive());
  EXPECT_EQ(Detect().seen_archive, Path("DAT/Seen.txt"));
}

TEST_F(GameDetectionTest, InvalidConfiguredArchiveAllowsFallback) {
  // An unrecognizable configured archive does not block a valid fallback.
  Write("Gameexe.ini", "#FOLDNAME.TXT = \"SCN\"\n");
  Write("SCN/Seen.txt", "not an archive");
  Write("Seen.txt", MakeArchive());
  EXPECT_EQ(Detect().seen_archive, Path("Seen.txt"));
}

TEST_F(GameDetectionTest, RecognizesAvg2000HeaderAsRealLiveFamily) {
  // The predecessor's header identifies the family without adding VM support.
  Write("Gameexe.ini");
  Write("Seen.txt", MakeArchive(MakeScenario(0x1cc)));
  EXPECT_EQ(Detect().engine, EngineKind::RealLive);
}

TEST_F(GameDetectionTest, TruncatedArchiveIsUnknown) {
  // An archive shorter than the full table cannot identify RealLive.
  Write("Gameexe.ini");
  Write("Seen.txt", std::string(79999, '\0'));
  EXPECT_EQ(Detect().engine, EngineKind::Unknown);
}

TEST_F(GameDetectionTest, EmptyArchiveTableIsUnknown) {
  // A full table with no scenarios supplies no RealLive evidence.
  Write("Gameexe.ini");
  Write("Seen.txt", std::string(80000, '\0'));
  EXPECT_EQ(Detect().engine, EngineKind::Unknown);
}

TEST_F(GameDetectionTest, OutOfBoundsScenarioIsUnknown) {
  // A scenario range extending beyond the file is ignored safely.
  auto archive = MakeArchive();
  PutU32(archive, 8 + 4, 0xffffffff);
  Write("Gameexe.ini");
  Write("Seen.txt", archive);
  EXPECT_EQ(Detect().engine, EngineKind::Unknown);
}

TEST_F(GameDetectionTest, TruncatedScenarioHeaderIsUnknown) {
  // A table entry shorter than four bytes cannot carry a scenario header.
  auto archive = MakeArchive();
  PutU32(archive, 8 + 4, 3);
  Write("Gameexe.ini");
  Write("Seen.txt", archive);
  EXPECT_EQ(Detect().engine, EngineKind::Unknown);
}

TEST_F(GameDetectionTest, ConflictingSampledHeaderRejectsArchive) {
  // A second bounded scenario with the wrong header invalidates the sample.
  auto archive = MakeArchive();
  PutU32(archive, 16, archive.size());
  PutU32(archive, 20, 4);
  archive += "bad!";
  Write("Gameexe.ini");
  Write("Seen.txt", archive);
  EXPECT_EQ(Detect().engine, EngineKind::Unknown);
}

TEST_F(GameDetectionTest, ArchiveSamplingStopsAfterFourValidHeaders) {
  // Four recognizable headers supply enough evidence; later entries are
  // deliberately outside the detector's sample, as in the reference detector.
  auto archive = MakeArchive();
  for (int id = 2; id <= 5; ++id) {
    PutU32(archive, id * 8, archive.size());
    PutU32(archive, id * 8 + 4, 4);
    archive += id == 5 ? "bad!" : std::string("\xd0\x01\0\0", 4);
  }
  Write("Gameexe.ini");
  Write("Seen.txt", archive);
  EXPECT_EQ(Detect().engine, EngineKind::RealLive);
}

TEST_F(GameDetectionTest, InvalidTableEntryDoesNotHideValidScenario) {
  // An out-of-range entry is skipped so a later bounded scenario can identify
  // the archive, matching the reference detector's evidence sampling.
  auto archive = MakeArchive(MakeScenario(), 2);
  PutU32(archive, 8, 0xffffffff);
  PutU32(archive, 12, 8);
  Write("Gameexe.ini");
  Write("Seen.txt", archive);
  EXPECT_EQ(Detect().engine, EngineKind::RealLive);
}

TEST_F(GameDetectionTest, PaclArchiveIsNotRealLive) {
  // An AVG32 archive signature is rejected despite the configuration file.
  Write("Gameexe.ini");
  Write("Seen.txt", "PACL");
  EXPECT_EQ(Detect().engine, EngineKind::Unknown);
}

TEST_F(GameDetectionTest, PaclArchivePreventsLooseNamingFallback) {
  // A known AVG32 archive takes priority over a loose four-digit filename.
  Write("Gameexe.ini");
  Write("Seen.txt", "PACL");
  Write("SEEN0001.TXT", "compressed?");
  EXPECT_EQ(Detect().engine, EngineKind::Unknown);
}

TEST_F(GameDetectionTest, DetectsLooseScenesByHeader) {
  // Loose RealLive headers identify a scene collection without an archive.
  Write("Gameexe.ini");
  Write("DAT/sEeN0001.TxT", MakeScenario());
  const auto game = Detect();
  EXPECT_EQ(game.engine, EngineKind::RealLive);
  EXPECT_TRUE(game.seen_archive.empty());
  EXPECT_EQ(game.seen_directory, Path("DAT"));
}

TEST_F(GameDetectionTest, DetectsLooseScenesByFourDigitName) {
  // When the header is unrecognizable, four digits supply the naming fallback.
  Write("Gameexe.ini");
  Write("SEEN0001.TXT", "compressed?");
  EXPECT_EQ(Detect().engine, EngineKind::RealLive);
}

TEST_F(GameDetectionTest, LooseHeaderTakesPriorityOverDigitWidth) {
  // A RealLive header identifies a loose scene even with a three-digit name.
  Write("Gameexe.ini");
  Write("SEEN001.TXT", MakeScenario());
  EXPECT_EQ(Detect().engine, EngineKind::RealLive);
}

TEST_F(GameDetectionTest, Tpc32HeaderOverridesRealLiveNamingFallback) {
  // A known AVG32 scene signature is rejected even with a four-digit name.
  Write("Gameexe.ini");
  Write("SEEN0001.TXT", "TPC32");
  EXPECT_EQ(Detect().engine, EngineKind::Unknown);
}

TEST_F(GameDetectionTest, ThreeDigitNamingFallbackIsNotRealLive) {
  // Three-digit scene names belong to AVG32 when no header identifies them.
  Write("Gameexe.ini");
  Write("SEEN001.TXT", "compressed?");
  EXPECT_EQ(Detect().engine, EngineKind::Unknown);
}

TEST_F(GameDetectionTest, DirectoriesAreNotLooseScenes) {
  // A directory with a plausible scene name cannot identify RealLive.
  Write("Gameexe.ini");
  fs::create_directory(Path("SEEN0001.TXT"));
  EXPECT_EQ(Detect().engine, EngineKind::Unknown);
}

TEST_F(GameDetectionTest, FindsKineticDataIgnoringCase) {
  // A game below the established KINETICDATA folder resolves to that root.
  Write("kineticData/Gameexe.ini");
  Write("kineticData/Seen.txt", MakeArchive());
  EXPECT_EQ(Detect().root, Path("kineticData"));
}

TEST_F(GameDetectionTest, FindsRealLiveDataIgnoringCase) {
  // A game below the established REALLIVEDATA folder resolves to that root.
  Write("realliveData/Gameexe.ini");
  Write("realliveData/Seen.txt", MakeArchive());
  EXPECT_EQ(Detect().root, Path("realliveData"));
}

TEST_F(GameDetectionTest, RecognizedRootTakesPriorityOverNestedGame) {
  // A root RealLive game wins over a Siglus game in a fallback directory.
  Write("Gameexe.ini");
  Write("Seen.txt", MakeArchive());
  Write("KINETICDATA/Scene.pck");
  const auto game = Detect();
  EXPECT_EQ(game.engine, EngineKind::RealLive);
  EXPECT_EQ(game.root, temporary_.dir());
}

TEST_F(GameDetectionTest, KineticDataTakesPriorityOverRealLiveData) {
  // Both fallback folders contain games; KINETICDATA is searched first.
  Write("KINETICDATA/Scene.pck");
  Write("REALLIVEDATA/Gameexe.dat");
  EXPECT_EQ(Detect().root, Path("KINETICDATA"));
}

TEST_F(GameDetectionTest, DoesNotSearchArbitrarySubdirectories) {
  // Discovery stays within the two established fallback directories.
  Write("other/Scene.pck");
  EXPECT_EQ(Detect().engine, EngineKind::Unknown);
}

TEST_F(GameDetectionTest, MissingRootReportsFilesystemError) {
  // A nonexistent selected root produces a filesystem error rather than a game.
  EXPECT_THROW(DetectGameRoot(Path("missing")), fs::filesystem_error);
}

TEST_F(GameDetectionTest, FileRootReportsFilesystemError) {
  // A file supplied as the game root produces a directory validation error.
  Write("file");
  EXPECT_THROW(DetectGameRoot(Path("file")), fs::filesystem_error);
}

}  // namespace
