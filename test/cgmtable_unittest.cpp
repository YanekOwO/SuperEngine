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

#include <gtest/gtest.h>

#include "core/cgm_table.hpp"
#include "core/gameexe.hpp"
#include "utilities/file.hpp"
#include "utilities/mapped_file.hpp"

#include "test_utils.hpp"

#include <filesystem>

namespace fs = std::filesystem;

TEST(cgmTableTest, DisabledCgm) {
  CGMTable table;
  EXPECT_EQ(table.GetTotal(), 0);
  EXPECT_EQ(table.GetViewed(), 0);
  EXPECT_EQ(table.GetPercent(), 0);
}

TEST(cgmTableTest, CreateFromCaseInsensitiveDataDirectory) {
  const auto data = LoadFileStr(PathToTestCase("Gameroot/data/clannad.cgm"));
  for (const char* directory : {"dat", "DAT", "Dat"}) {
    SCOPED_TRACE(directory);
    ScopedTemporary root;
    WriteFile(root.dir() / directory / "Clannad.CGM", data);

    Gameexe gameexe;
    gameexe("__GAMEPATH") = root.dir().string();
    gameexe("CGTABLE_FILENAME") = "clannad.cgm";

    const auto table = CreateCGMTable(gameexe);
    EXPECT_EQ(table.GetTotal(), 174);
    EXPECT_EQ(table.GetFlag("FGNG01A"), 0);
  }
}

TEST(cgmTableTest, ParseCgm) {
  {
    fs::path filepath = PathToTestCase("Gameroot/data/clannad.cgm");
    MappedFile file(filepath);

    CGMTable table(file.Read());
    EXPECT_EQ(table.GetTotal(), 174);

    EXPECT_EQ(table.GetFlag("FGNG01A"), 0);
    EXPECT_EQ(table.GetFlag("FGKY05C"), 100);
    EXPECT_EQ(table.GetFlag("BG051O"), 154);
    EXPECT_EQ(table.GetFlag("FGTM01B"), 173);
    EXPECT_EQ(table.GetFlag("FGTM08"), 189);
    EXPECT_EQ(table.GetFlag("ED4_01"), 252);
    EXPECT_EQ(table.GetFlag("ED4_02"), 253);
  }

  {
    fs::path filepath = PathToTestCase("Gameroot/data/tomoyoafter.cgm");
    MappedFile file(filepath);

    CGMTable table(file.Read());
    const std::vector<std::string> cgnames{
        "CG01",    "CG03A",   "CG03B",   "CG03C",   "CG03D",  "CG03E",
        "CG04A",   "CG04B",   "CG04C",   "CG04D",   "CG05A",  "CG05B",
        "CG06A",   "CG06B",   "CG07A",   "CG07B",   "CG08",   "CG09A",
        "CG09B",   "CG10A",   "CG10B",   "CG11A",   "CG11B",  "CG06",
        "CG12A",   "CG12B",   "CG13",    "CG14",    "CG15",   "CG37",
        "CG29",    "CG40",    "CG23",    "CG39",    "CG19",   "X_EV08",
        "X_EV01A", "X_EV01B", "X_EV01C", "X_EV01D", "X_EV02", "X_EV03",
        "X_EV04",  "X_EV05",  "X_EV06",  "CG64A",   "CG64B",  "CG64C",
        "CG64D",   "CG64E",   "CG64F",   "CG42",    "CG43",   "CG32B",
        "CG32",    "HCG4A",   "HCG4B",   "HCG4C",   "HCG4D",  "CG33",
        "CG33B",   "CG34A",   "CG34B",   "CG34C",   "CG34E",  "CG35",
        "CG28A",   "CG28B",   "CG28C"};
    EXPECT_EQ(table.GetTotal(), cgnames.size());
    for (size_t i = 0; i < cgnames.size(); ++i) {
      EXPECT_EQ(table.GetFlag(cgnames[i]), i) << cgnames[i];
    }
  }
}

TEST(cgmTableTest, SetViewed) {
  fs::path filepath = PathToTestCase("Gameroot/data/clannad.cgm");
  MappedFile file(filepath);

  CGMTable table(file.Read());

  EXPECT_EQ(table.GetViewed(), 0);
  EXPECT_EQ(table.GetPercent(), 0);
  table.SetViewed("FGSB03");
  EXPECT_EQ(table.GetPercent(), 1)
      << "Percentage should be at least 1% even if only 1 CG is viewed";
  table.SetViewed("nonexist");
  table.SetViewed("FGSB03");
  table.SetViewed("FGNG01A");
  table.SetViewed("FGNG01B");
  EXPECT_EQ(table.GetViewed(), 3);
}
