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

#include "core/memory.hpp"

#include <memory>
#include <random>

class MemoryStressTest : public ::testing::Test {
 protected:
  static constexpr std::size_t kStressBankSize = 4096;

  MemoryStressTest()
      : gen(rd()),
        index_dist(0, kStressBankSize - 1),
        int_dist(std::numeric_limits<int>::min(),
                 std::numeric_limits<int>::max()),
        bankid_dist(0, static_cast<uint8_t>(IntBank::CNT) - 1),
        memory_(backing_memory_.CreateHandle()) {
    memory_.PushStackFrame(kStressBankSize, kStressBankSize);
  }

  std::random_device rd;
  std::mt19937 gen;
  std::uniform_int_distribution<size_t> index_dist;
  std::uniform_int_distribution<int> int_dist;
  std::uniform_int_distribution<uint8_t> bankid_dist;

  IntMemoryLocation RandomIntLocation() {
    return IntMemoryLocation(static_cast<IntBank>(bankid_dist(gen)),
                             index_dist(gen));
  }

  Memory backing_memory_;
  MemoryHandle memory_;
};

TEST_F(MemoryStressTest, DynamicAllocation) {
  auto fake_memory = [&]() -> std::map<IntMemoryLocation, int> {
    std::map<IntMemoryLocation, int> result;
    for (int i = 0; i < 1000; ++i)
      result.emplace(RandomIntLocation(), int_dist(gen));
    return result;
  }();

  EXPECT_NO_THROW({
    for (uint8_t i = 0; i < static_cast<uint8_t>(IntBank::CNT); ++i)
      memory_.Resize(static_cast<IntBank>(i), kStressBankSize);
  }) << "Memory class should dynamically allocate memory on demand";

  for (const auto& [loc, val] : fake_memory)
    memory_.Write(loc, val);

  // Repeatedly grow and update memory across every integer bank.
  for (int ver = 1; ver < 10; ++ver) {
    for (int j = 0; j < 30; ++j) {
      auto loc = RandomIntLocation();
      auto val = int_dist(gen);
      memory_.Write(loc, val);
      fake_memory[loc] = val;
    }
  }

  for (const auto& [loc, val] : fake_memory) {
    EXPECT_EQ(memory_.Read(loc), val) << "location " << loc;
  }
}
