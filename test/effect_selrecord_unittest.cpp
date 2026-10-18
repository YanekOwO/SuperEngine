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

#include "core/gameexe.hpp"
#include "long_operations/effects/sel_record.hpp"

class SelRecordTest : public ::testing::Test {
 protected:
  void SetUp() override {
    gexe.parseLine(
        "#SEL.000=100,50,1380, 1010,000,000, 500,050,  0,  0,  0,  0,  0,  "
        "0,255,  0,");
    gexe.parseLine(
        "#SELR.000=000,000,1280, 960,000,000, 500,050,  0,  0,  0,  0,  0,  "
        "0,255,  0,");
    gexe.parseLine(
        "#SELR.012=100,050,1280, 960,010,050,2000,194,  0,  2,  2,500,  0,  "
        "0,255,  0,");
  }

  Gameexe gexe;
};

TEST_F(SelRecordTest, GetSEL) {
  auto record = lop::GetSelRecord(gexe, 0);
  EXPECT_EQ(record.ToString(),
            "(100,50,1380,1010)(0,0) 500 50 0 0 0 0 0 0 255 0");
}

TEST_F(SelRecordTest, GetSELR) {
  auto record = lop::GetSelRecord(gexe, 12);
  EXPECT_EQ(record.ToString(),
            "(100,50,1380,1010)(10,50) 2000 194 0 2 2 500 0 0 255 0");
}

TEST_F(SelRecordTest, Fallback) {
  Gameexe empty;
  empty.parseLine("#SCREENSIZE_MOD=0");
  auto record = lop::GetSelRecord(empty, 77);
  EXPECT_EQ(record.ToString(), "(0,0,640,480)(0,0) 1000 0 0 0 0 0 0 0 255 0");
}
