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

TEST(MemoryHandleTest, RoutesStackBanksToTopFrame) {
  Memory memory;
  auto handle = memory.CreateHandle();

  handle.PushStackFrame(40, 40);
  handle.Write(IntBank::L, 0, 10);
  handle.Write(StrBank::K, 0, "root");
  handle.PushStackFrame(8, 8);

  EXPECT_EQ(handle.StackDepth(), 2);
  EXPECT_EQ(handle.Read(IntBank::L, 0), 0);
  EXPECT_EQ(handle.Read(StrBank::K, 0), "");
  handle.Write(IntBank::L, 0, 20);
  handle.Write(StrBank::K, 0, "callee");

  handle.PopStackFrame();
  EXPECT_EQ(handle.Read(IntBank::L, 0), 10);
  EXPECT_EQ(handle.Read(StrBank::K, 0), "root");
}

TEST(MemoryHandleTest, HandlesHaveIndependentStacksAndSharedNonStackMemory) {
  Memory memory;
  auto first = memory.CreateHandle();
  auto second = memory.CreateHandle();
  first.PushStackFrame(8, 8);
  second.PushStackFrame(8, 8);

  first.Write(IntBank::L, 0, 10);
  second.Write(IntBank::L, 0, 20);
  first.Write(IntBank::A, 0, 30);

  EXPECT_EQ(first.Read(IntBank::L, 0), 10);
  EXPECT_EQ(second.Read(IntBank::L, 0), 20);
  EXPECT_EQ(second.Read(IntBank::A, 0), 30);
}

TEST(MemoryHandleTest, CopiesAreViewsAndCloneIsASnapshot) {
  Memory memory;
  auto original = memory.CreateHandle();
  original.PushStackFrame(8, 8);
  original.Write(IntBank::L, 0, 10);

  auto view = original;
  auto snapshot = original.Clone();
  view.Write(IntBank::L, 0, 20);

  EXPECT_EQ(original.Read(IntBank::L, 0), 20);
  EXPECT_EQ(snapshot.Read(IntBank::L, 0), 10);
}

TEST(MemoryHandleTest, AddressesFramesRelativeToTheTop) {
  Memory memory;
  auto handle = memory.CreateHandle();
  handle.PushStackFrame(8, 8);
  handle.Write(StrBank::K, 0, "caller");
  handle.PushStackFrame(8, 8);

  handle.WriteStack(StrBank::K, 1, 0, "updated");

  EXPECT_EQ(handle.ReadStack(StrBank::K, 1, 0), "updated");
  EXPECT_THROW(handle.ReadStack(StrBank::K, 2, 0), std::out_of_range);
}

TEST(MemoryHandleTest, RejectsStackAccessWithoutAFrame) {
  Memory memory;
  auto handle = memory.CreateHandle();

  EXPECT_THROW(handle.Read(IntBank::L, 0), std::runtime_error);
  EXPECT_THROW(handle.Read(StrBank::K, 0), std::runtime_error);
  EXPECT_THROW(handle.PopStackFrame(), std::underflow_error);
}
