// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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

#include "core/bgm_table.hpp"
#include "core/gameexe.hpp"
#include "serialization_test_helpers.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

TEST(BgmTableTest, ConstructorRequiresMatchingStateSize) {
  EXPECT_THROW(BgmTable({"track"}, {}), std::invalid_argument);
}

TEST(BgmTableTest, LookupIsCaseInsensitive) {
  BgmTable table({"TrackOne", "", "TrackTwo"}, {false, true, true});

  EXPECT_EQ(table.Count(), 3);
  EXPECT_EQ(table.GetListen("trackone"), 0);
  EXPECT_EQ(table.GetListen("TRACKTWO"), 1);
  EXPECT_EQ(table.GetListen(""), -1);
  EXPECT_EQ(table.GetListen("unknown"), -1);
}

TEST(BgmTableTest, MutatesIndividualAndAllEntries) {
  BgmTable table({"one", "two", "three"}, {false, true, false});

  table.SetListen("ONE", true);
  EXPECT_EQ(table.GetListen("one"), 1);

  table.SetListen("missing", true, false);
  EXPECT_EQ(table.GetListen("missing"), -1);

  table.SetListenAll(false);
  EXPECT_EQ(table.GetListen("one"), 0);
  EXPECT_EQ(table.GetListen("two"), 0);
  EXPECT_EQ(table.GetListen("three"), 0);

  table.SetListenAll(true);
  EXPECT_EQ(table.GetListen("one"), 1);
  EXPECT_EQ(table.GetListen("two"), 1);
  EXPECT_EQ(table.GetListen("three"), 1);
}

TEST(BgmTableTest, CreatesDefaultSizedTableFromEmptyGameexe) {
  Gameexe gameexe;
  BgmTable table = BgmTable::CreateFromSiglus(gameexe);

  EXPECT_EQ(table.Count(), 32);
  EXPECT_EQ(table.GetListen("unknown"), -1);
}

TEST(BgmTableTest, CreatesTableFromSiglusEntries) {
  Gameexe gameexe;
  gameexe.parseLine("BGM.000 = BGM01,BGM01,82286,5184000,905143");
  gameexe.parseLine("BGM.030 = SONG01,SONG01,0,-1,0");
  gameexe.parseLine("BGM.CNT = 50");
  gameexe.SetStringAt("BGM.invalid", "ignored");
  gameexe.SetStringAt("BGM.050", "out-of-range");

  BgmTable table = BgmTable::CreateFromSiglus(gameexe);

  EXPECT_EQ(table.Count(), 50);
  EXPECT_EQ(table.GetListen("bgm01"), 0);
  EXPECT_EQ(table.GetListen("song01"), 0);
  EXPECT_EQ(table.GetListen("ignored"), -1);
  EXPECT_EQ(table.GetListen("out-of-range"), -1);
}

TEST(BgmTableTest, RejectsInvalidSiglusCount) {
  Gameexe gameexe;

  gameexe.SetIntAt("BGM.CNT", -1);
  EXPECT_THROW(BgmTable::CreateFromSiglus(gameexe), std::runtime_error);

  gameexe.SetIntAt("BGM.CNT", 257);
  EXPECT_THROW(BgmTable::CreateFromSiglus(gameexe), std::runtime_error);
}

// ------------------------------------------------------------------------------

class BgmTableArchiveTest : public ::testing::Test {};

TEST_F(BgmTableArchiveTest, RestoresListenState) {
  // Saved listen flags replace later changes without changing the game table.
  serialization_test::ForEachFormat([&](auto format) {
    BgmTable table({"one", "two"}, {true, false});
    std::vector<bool> saved;
    serialization_test::RoundTrip(table.ListenState(), saved, format);
    table.SetListenAll(false);
    table.RestoreListenState(std::move(saved));
    EXPECT_EQ(table.GetListen("one"), 1);
    EXPECT_EQ(table.GetListen("two"), 0);
  });
}

TEST_F(BgmTableArchiveTest, SizeMismatchPreservesListenState) {
  // A saved table of the wrong size fails without replacing live listen flags.
  serialization_test::ForEachFormat([&](auto format) {
    BgmTable table({"one", "two"}, {true, false});
    std::vector<bool> source{false}, target;
    serialization_test::RoundTrip(source, target, format);
    EXPECT_THROW(table.RestoreListenState(target), std::invalid_argument);
    EXPECT_EQ(table.ListenState(), (std::vector<bool>{true, false}));
  });
}

}  // namespace
