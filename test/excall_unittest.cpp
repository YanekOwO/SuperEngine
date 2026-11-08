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

#include "core/excall.hpp"

#include <gtest/gtest.h>

class ExcallTest : public ::testing::Test {};

TEST_F(ExcallTest, AllocatesExplicitResourceSizes) {
  // Explicit counts allocate independent resource lists and all stage layers.
  Excall excall;
  excall.Alloc({.flag_count = 2,
                .counter_count = 3,
                .frame_action_count = 4,
                .object_count = 5,
                .effect_count = 6,
                .group_count = 7,
                .quake_count = 8,
                .window_count = 9});
  EXPECT_TRUE(excall.allocated);
  EXPECT_NE(excall.lifetime, nullptr);
  EXPECT_EQ(excall.flags.size(), 2);
  EXPECT_EQ(excall.counters->elements_.size(), 3);
  for (const auto& counter : excall.counters->elements_)
    EXPECT_NE(counter, nullptr);
  EXPECT_EQ(excall.actions->global_channels().size(), 4);
  for (int layer : {kLayerFg, kLayerBg, kLayerNext}) {
    EXPECT_EQ(excall.stage.objects.Size(layer), 5);
    EXPECT_EQ(excall.stage.effects.Size(layer), 6);
    EXPECT_EQ(excall.stage.groups.Size(layer), 7);
    EXPECT_EQ(excall.stage.quakes.Size(layer), 8);
    EXPECT_EQ(excall.stage.mwnd.Size(layer), 9);
  }
  EXPECT_EQ(excall.stage.screen_effects.size(), 6);
}

TEST_F(ExcallTest, RepeatedAllocationPreservesResources) {
  // A second allocation with different sizes retains the existing storage.
  Excall excall;
  excall.Alloc({.flag_count = 2});
  excall.flags[1] = 42;
  const auto lifetime = excall.lifetime;
  excall.Alloc({});
  ASSERT_EQ(excall.flags.size(), 2);
  EXPECT_EQ(excall.flags[1], 42);
  EXPECT_EQ(excall.lifetime, lifetime);
}

TEST_F(ExcallTest, ReleasingPreventsAllocation) {
  // Allocation during release must leave the resource storage unavailable.
  Excall excall;
  excall.releasing = true;
  excall.Alloc({.flag_count = 2});
  EXPECT_FALSE(excall.allocated);
  EXPECT_TRUE(excall.flags.empty());
  EXPECT_EQ(excall.lifetime, nullptr);
}
