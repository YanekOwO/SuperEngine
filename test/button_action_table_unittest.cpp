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
//
// -----------------------------------------------------------------------

#include "core/button_action_table.hpp"
#include "core/gameexe.hpp"
#include "serialization_test_helpers.hpp"

#include <gtest/gtest.h>

namespace {

using State = ButtonActionTable::State;
static State NeutralState() { return State{}; }

TEST(ButtonActionTableTest, SiglusUsesLegacyDefaults) {
  Gameexe gameexe;

  const auto table = ButtonActionTable::ParseSiglus(gameexe);

  EXPECT_EQ(table.GetCount(), 16);
  const auto first = table.GetEntry(0);
  EXPECT_EQ(first.normal, NeutralState());
  EXPECT_EQ(first.hit, (State{0, Point(0, 0), 255, 32, 0}));
  EXPECT_EQ(first.push, (State{0, Point(1, 1), 255, 32, 0}));
  EXPECT_EQ(first.select, NeutralState());
  EXPECT_EQ(first.disable, NeutralState());

  EXPECT_EQ(table.GetEntry(15).push, (State{0, Point(1, 1), 255, 32, 0}));
}

TEST(ButtonActionTableTest, ParsesSparseSiglusEntriesAndClampsColorValues) {
  Gameexe gameexe;
  gameexe.parseLine("BUTTON.ACTION.CNT = 300");
  gameexe.parseLine("BUTTON.ACTION.299.NORMAL = 7,-2,3,300,-4,999");
  gameexe.parseLine("BUTTON.ACTION.299.PUSH = 8,4,5,128,64,32");

  const auto table = ButtonActionTable::ParseSiglus(gameexe);

  EXPECT_EQ(table.GetCount(), 300);
  const auto entry = table.GetEntry(299);
  EXPECT_EQ(entry.normal, (State{7, Point(-2, 3), 255, 0, 255}));
  EXPECT_EQ(entry.hit, (State{0, Point(0, 0), 255, 32, 0}));
  EXPECT_EQ(entry.push, (State{8, Point(4, 5), 128, 64, 32}));
}

TEST(ButtonActionTableTest, EmptyRealliveConfigCreatesEmptyTable) {
  Gameexe gameexe;

  const auto table = ButtonActionTable::ParseReallive(gameexe);

  EXPECT_EQ(table.GetCount(), 0);
  EXPECT_TRUE(table.GetAllEntry().empty());
}

TEST(ButtonActionTableTest, ParsesSparseRealliveEntriesAndFillsDefaults) {
  Gameexe gameexe;
  gameexe.parseLine("BTNOBJ.ACTION.300.NORMAL = 4,99,-3,6");
  gameexe.parseLine("BTNOBJ.ACTION.300.PUSH = 5,42,7,8,100,200");

  const auto table = ButtonActionTable::ParseReallive(gameexe);

  EXPECT_EQ(table.GetCount(), 301);
  EXPECT_EQ(table.GetEntry(0), ButtonActionTable::Entry{});

  const auto entry = table.GetEntry(300);
  EXPECT_EQ(entry.normal, (State{4, Point(-3, 6), 255, 0, 0}));
  EXPECT_EQ(entry.hit, NeutralState());
  EXPECT_EQ(entry.push, (State{5, Point(7, 8), 255, 0, 0}));
  EXPECT_EQ(entry.disable, NeutralState());
}

// ------------------------------------------------------------------------------

class ButtonActionArchiveTest : public ::testing::Test {};

TEST_F(ButtonActionArchiveTest, RetainsAllButtonStates) {
  // Distinct corrections for every button state survive an archive round trip.
  serialization_test::ForEachFormat([&](auto format) {
    ButtonActionTable::Entry source, target;
    source.normal = {1, Point(2, 3), 4, 5, 6};
    source.hit = {7, Point(8, 9), 10, 11, 12};
    source.push = {13, Point(14, 15), 16, 17, 18};
    source.select = {19, Point(20, 21), 22, 23, 24};
    source.disable = {25, Point(26, 27), 28, 29, 30};
    serialization_test::RoundTrip(source, target, format);
    EXPECT_EQ(target, source);
  });
}

}  // namespace
