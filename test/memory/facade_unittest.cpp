// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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
//
// -----------------------------------------------------------------------

#include "core/memory_internal/facade.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

TEST(MemoryFacadeTest, SupportsContextFreeStorage) {
  std::vector<int> storage;
  IntListFacade<> facade([&storage]() -> std::vector<int>& { return storage; },
                         4);

  EXPECT_EQ(facade.size(), 4);
  facade.Set(1, {10, 20});
  EXPECT_EQ(facade.get(1), 10);
  EXPECT_EQ(facade.get(2), 20);

  facade.write_b4(0, 7);
  EXPECT_EQ(facade.b4(0), 7);
  facade.init();
  EXPECT_EQ(storage, std::vector<int>({0, 0, 0, 0}));
}

TEST(MemoryFacadeTest, ForwardsMultipleContextArgumentsToTheGetter) {
  struct Context {
    std::vector<std::string> primary;
    std::vector<std::string> alternate;
  } context;

  StrListFacade<Context&, bool> facade(
      [](Context& context, bool alternate) -> std::vector<std::string>& {
        return alternate ? context.alternate : context.primary;
      });

  facade.set(context, false, 0, "primary");
  facade.set(context, true, 0, "alternate");

  EXPECT_EQ(facade.get(context, false, 0), "primary");
  EXPECT_EQ(facade.get(context, true, 0), "alternate");
}
