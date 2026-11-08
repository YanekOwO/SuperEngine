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

#include "libsiglus/bindings/memory.hpp"

namespace siglus_test {
using namespace libsiglus;

TEST(ScriptCallTest, FarcallMemoryCopiesItsSeeds) {
  // A host fiber owns copied L/K seeds independently of its request data.
  Memory memory;
  std::vector<int> integers{7, 11};
  std::vector<std::string> strings{"argument"};
  auto banks = binding::MakeCallMemory(memory, integers, strings);
  integers[0] = 99;
  strings[0] = "changed";
  EXPECT_EQ(banks.Read(IntBank::L, 0), 7);
  EXPECT_EQ(banks.Read(IntBank::L, 1), 11);
  EXPECT_EQ(banks.Read(StrBank::K, 0), "argument");
}

TEST(ScriptCallTest, EmptyHostCallHasMinimumBankSize) {
  // An empty native entry owns one zero-initialized bank pair of size eight.
  Memory memory;
  auto banks = binding::MakeCallMemory(memory);
  EXPECT_EQ(banks.StackDepth(), 1);
  EXPECT_EQ(banks.Size(IntBank::L), 8);
  EXPECT_EQ(banks.Size(StrBank::K), 8);
  EXPECT_EQ(banks.Read(IntBank::L, 0), 0);
  EXPECT_EQ(banks.Read(StrBank::K, 0), "");
}

TEST(ScriptCallTest, HostBanksGrowToFitTheirSeeds) {
  // Seeds larger than the minimum retain every element in the initial banks.
  Memory memory;
  auto banks = binding::MakeCallMemory(memory, std::vector<int>(11, 3),
                                       std::vector<std::string>(9, "text"));
  EXPECT_EQ(banks.Size(IntBank::L), 11);
  EXPECT_EQ(banks.Size(StrBank::K), 9);
  EXPECT_EQ(banks.Read(IntBank::L, 10), 3);
  EXPECT_EQ(banks.Read(StrBank::K, 8), "text");
}

}  // namespace siglus_test
