// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
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
//
// -----------------------------------------------------------------------

#include <gtest/gtest.h>

#include "core/memory.hpp"
#include "core/memory_internal/proxy.hpp"

#include <bitset>
#include <limits>
#include <string>

class MemoryTest : public ::testing::Test {
 protected:
  MemoryTest()
      : memory_(
            std::make_unique<MemoryHandle>(backing_memory_.CreateHandle())) {
    memory_->PushStackFrame(2000, 2000);
  }

  Memory backing_memory_;
  std::unique_ptr<MemoryHandle> memory_;
};

TEST_F(MemoryTest, Init) {
  for (size_t bank = 0; bank < static_cast<size_t>(IntBank::CNT); ++bank) {
    for (size_t index = 0; index < 2000; ++index) {
      IntMemoryLocation loc(static_cast<IntBank>(bank), index);
      EXPECT_EQ(memory_->Read(loc), 0) << "each IntBank should be initialized "
                                          "with size 2000 and default value 0";
    }
  }

  for (size_t bank = 0; bank < static_cast<size_t>(StrBank::CNT); ++bank) {
    for (size_t index = 0; index < 2000; ++index) {
      StrMemoryLocation loc(static_cast<StrBank>(bank), index);
      EXPECT_EQ(memory_->Read(loc), "")
          << "each StrBank should be initialized with "
             "size 2000 and default empty string";
    }
  }
}

TEST_F(MemoryTest, ReadWrite) {
  {
    IntMemoryLocation loc(IntBank::A, 100);

    memory_->Write(loc, 42);
    EXPECT_EQ(memory_->Read(loc), 42);

    IntMemoryLocation loc2(IntBank::Z, 101);
    EXPECT_EQ(memory_->Read(loc2), 0);

    IntMemoryLocation grown_loc(IntBank::F, 2000);
    EXPECT_EQ(memory_->Read(grown_loc), 0);
    EXPECT_EQ(memory_->Size(IntBank::F), 2001);
    memory_->Write(grown_loc, 10);
    EXPECT_EQ(memory_->Read(grown_loc), 10);
  }

  {
    StrMemoryLocation loc(StrBank::S, 150);

    memory_->Write(loc, "Hello World");
    EXPECT_EQ(memory_->Read(loc), "Hello World");

    StrMemoryLocation loc2(StrBank::M, 151);
    EXPECT_EQ(memory_->Read(loc2), "");

    StrMemoryLocation grown_loc(StrBank::S, 2000);
    EXPECT_EQ(memory_->Read(grown_loc), "");
    EXPECT_EQ(memory_->Size(StrBank::S), 2001);
    memory_->Write(grown_loc, "Test");
    EXPECT_EQ(memory_->Read(grown_loc), "Test");
  }
}

TEST_F(MemoryTest, WriteInt) {
  auto Write = [&](int bit, int index, int value) {
    auto loc = IntMemoryLocation(IntBank::B, index, bit);
    memory_->Write(loc, value);
  };

  Write(2, 16, 0b01);
  Write(1, 35, 0b1);
  Write(8, 5, 0b10000101);
  Write(4, 9, 0b101);
  Write(16, 3, 0b0100110110011100);

  EXPECT_EQ(memory_->Read(IntMemoryLocation(IntBank::B, 1)), 1302103385)
      << std::bitset<32>(memory_->Read(IntMemoryLocation(IntBank::B, 1)));
  EXPECT_EQ(memory_->Read(IntMemoryLocation(IntBank::B, 8, 4)), 0b1001)
      << std::bitset<4>(memory_->Read(IntMemoryLocation(IntBank::B, 8, 4)));

  EXPECT_THROW(Write(4, 0, 0b10000), std::overflow_error);
  EXPECT_EQ(memory_->Read(IntBank::B, 0), 0);
  EXPECT_THROW(Write(4, 0, -1), std::overflow_error);
  EXPECT_EQ(memory_->Read(IntBank::B, 0), 0);
  EXPECT_THROW(Write(5, 0, 0), std::invalid_argument);

  memory_->Write(IntBank::B, 2, std::numeric_limits<int>::min());
  EXPECT_EQ(memory_->Read(IntMemoryLocation(IntBank::B, 5, 16)), 0x8000);
}

TEST_F(MemoryTest, IntFill) {
  {
    memory_->Fill(IntBank::B, 50, 100, 7);

    for (size_t i = 50; i < 100; ++i) {
      IntMemoryLocation loc(IntBank::B, i);
      EXPECT_EQ(memory_->Read(loc), 7);
    }

    IntMemoryLocation loc_before(IntBank::B, 49);
    IntMemoryLocation loc_after(IntBank::B, 100);
    EXPECT_EQ(memory_->Read(loc_before), 0)
        << "Values outside the range should remain default";
    EXPECT_EQ(memory_->Read(loc_after), 0)
        << "Values outside the range should remain default";

    EXPECT_NO_THROW(memory_->Fill(IntBank::B, 0, 0, 9));
    EXPECT_EQ(memory_->Read(IntBank::B, 0), 0);
    EXPECT_NO_THROW(memory_->Fill(IntBank::B,
                                  memory_->Size(IntBank::B),
                                  memory_->Size(IntBank::B),
                                  9));
    EXPECT_NO_THROW(memory_->Fill(IntBank::B, 2001, 2001, 9));
    EXPECT_EQ(memory_->Size(IntBank::B), 2000);
    EXPECT_THROW(memory_->Fill(IntBank::B, 100, 50, 5), std::invalid_argument)
        << "Should throw when range (begin > end)";
    EXPECT_NO_THROW(memory_->Fill(IntBank::B, 1990, 2010, 5));
    EXPECT_EQ(memory_->Size(IntBank::B), 2010);
    EXPECT_EQ(memory_->Read(IntBank::B, 2009), 5);
  }

  {
    memory_->Fill(StrBank::M, 20, 30, "TestString");

    for (size_t i = 20; i < 30; ++i) {
      StrMemoryLocation loc(StrBank::M, i);
      EXPECT_EQ(memory_->Read(loc), "TestString");
    }

    StrMemoryLocation loc_before(StrBank::M, 19);
    StrMemoryLocation loc_after(StrBank::M, 30);
    EXPECT_EQ(memory_->Read(loc_before), "");
    EXPECT_EQ(memory_->Read(loc_after), "");

    EXPECT_NO_THROW(memory_->Fill(StrBank::M, 0, 0, "Empty"));
    EXPECT_EQ(memory_->Read(StrBank::M, 0), "");
    EXPECT_NO_THROW(memory_->Fill(StrBank::M,
                                  memory_->Size(StrBank::M),
                                  memory_->Size(StrBank::M),
                                  "Empty"));
    EXPECT_NO_THROW(memory_->Fill(StrBank::M, 2001, 2001, "OutOfRange"));
    EXPECT_EQ(memory_->Size(StrBank::M), 2000);
    EXPECT_THROW(memory_->Fill(StrBank::M, 30, 20, "Invalid"),
                 std::invalid_argument);
    EXPECT_NO_THROW(memory_->Fill(StrBank::M, 1995, 2005, "OutOfRange"));
    EXPECT_EQ(memory_->Size(StrBank::M), 2005);
    EXPECT_EQ(memory_->Read(StrBank::M, 2004), "OutOfRange");
  }
}

TEST_F(MemoryTest, Resize) {
  {
    IntMemoryLocation loc(IntBank::C, 2500), loc_existing(IntBank::C, 1999);
    memory_->Write(loc_existing, 77);
    memory_->Resize(IntBank::C, 3000);

    EXPECT_EQ(memory_->Read(loc_existing), 77)
        << "Existing data should be preserved";
    EXPECT_EQ(memory_->Read(loc), 0)
        << "New elements should be default initialized";

    // Resize to a smaller size
    memory_->Resize(IntBank::C, 1000);
    EXPECT_EQ(memory_->Read(loc_existing), 0);
    EXPECT_EQ(memory_->Size(IntBank::C), 2000);
    memory_->Resize(IntBank::C, 8888);
    EXPECT_EQ(memory_->Read(loc), 0);
  }

  {
    StrMemoryLocation loc(StrBank::K, 2500), loc_existing(StrBank::K, 1999);
    memory_->Resize(StrBank::K, 3000);
    memory_->Write(loc_existing, "Hello, World!");

    EXPECT_EQ(memory_->Read(loc_existing), "Hello, World!");
    EXPECT_EQ(memory_->Read(loc), "");

    memory_->Resize(StrBank::K, 0);
    EXPECT_EQ(memory_->Read(loc_existing), "");
    EXPECT_EQ(memory_->Size(StrBank::K), 2000);
  }
}

TEST_F(MemoryTest, EdgeCases) {
  IntMemoryLocation invalid_loc(IntBank::D, static_cast<size_t>(-1));
  EXPECT_THROW(memory_->Read(invalid_loc), std::out_of_range);
  EXPECT_THROW(memory_->Write(invalid_loc, 10), std::out_of_range);

  EXPECT_THROW(memory_->Read(IntMemoryLocation(static_cast<IntBank>(100), 0)),
               std::invalid_argument)
      << "Should throw when an invalid memory bank is specified";

  IntMemoryLocation loc(IntBank::E, 0);
  memory_->Resize(IntBank::E, 0);
  EXPECT_EQ(memory_->Read(loc), 0);
  EXPECT_EQ(memory_->Size(IntBank::E), 1);
}

TEST_F(MemoryTest, GetStack) {
  for (int i = 0; i < 15; ++i) {
    memory_->Write(IntBank::L, i, i);
    memory_->Write(StrBank::K, i, std::to_string(i));
  }
  auto frame1 = memory_->GetStackMemory();

  for (int i = 10; i < 20; ++i) {
    memory_->Write(IntBank::L, i, i * i);
    memory_->Write(StrBank::K, i, std::to_string(i * i));
  }
  auto frame2 = memory_->GetStackMemory();

  // check stack frame 1
  for (int i = 0; i < 15; ++i) {
    EXPECT_EQ(IntListProxy(frame1.L.back()).Get(i), i);
    EXPECT_EQ(StrListProxy(frame1.K.back()).Get(i), std::to_string(i));
  }

  // check stack frame 2
  for (int i = 10; i < 20; ++i) {
    EXPECT_EQ(IntListProxy(frame2.L.back()).Get(i), i * i);
    EXPECT_EQ(StrListProxy(frame2.K.back()).Get(i), std::to_string(i * i));
  }

  // check memory
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(memory_->Read(IntBank::L, i), i);
    EXPECT_EQ(memory_->Read(StrBank::K, i), std::to_string(i));
  }
  for (int i = 10; i < 20; ++i) {
    EXPECT_EQ(memory_->Read(IntBank::L, i), i * i);
    EXPECT_EQ(memory_->Read(StrBank::K, i), std::to_string(i * i));
  }
}

TEST_F(MemoryTest, SetStack) {
  StackMemory frame1, frame2;
  frame1.L.emplace_back(50);
  frame1.K.emplace_back(50);
  frame2.L.emplace_back(60);
  frame2.K.emplace_back(60);
  IntListProxy frame1_l(frame1.L.back());
  StrListProxy frame1_k(frame1.K.back());
  IntListProxy frame2_l(frame2.L.back());
  StrListProxy frame2_k(frame2.K.back());

  for (int i = 0; i < 15; ++i) {
    frame1_l.Set(i, i);
    frame1_k.Set(i, std::to_string(i));
  }
  for (int i = 10; i < 20; ++i) {
    frame2_l.Set(i, i * i);
    frame2_k.Set(i, std::to_string(i * i));
  }

  memory_->Fill(IntBank::L, 0, 100, -123);
  memory_->Fill(StrBank::K, 0, 100, "some string");
  memory_->ResetStackMemory(frame1);
  for (int i = 0; i < 15; ++i) {
    EXPECT_EQ(memory_->Read(IntBank::L, i), i);
    EXPECT_EQ(memory_->Read(StrBank::K, i), std::to_string(i));
  }

  memory_->ResetStackMemory(frame2);
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(memory_->Read(IntBank::L, i), 0);
    EXPECT_EQ(memory_->Read(StrBank::K, i), "");
  }
  for (int i = 10; i < 20; ++i) {
    EXPECT_EQ(memory_->Read(IntBank::L, i), i * i);
    EXPECT_EQ(memory_->Read(StrBank::K, i), std::to_string(i * i));
  }
}
