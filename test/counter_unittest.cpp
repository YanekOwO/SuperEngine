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

#include "core/counter.hpp"

#include <cstdint>
#include <limits>
#include <memory>

TEST(SiglusCounterTest, SelectsGameOrRealTimeDomain) {
  Counter game;
  Counter real;
  game.Start(false);
  real.Start(true);

  game.Advance(25, 7);
  real.Advance(25, 7);

  EXPECT_EQ(game.Get(), 25);
  EXPECT_EQ(real.Get(), 7);
}

TEST(SiglusCounterTest, StopResumeSetAndResetAreFrameDriven) {
  Counter counter;
  counter.Start(false);
  counter.Advance(10, 20);
  counter.Stop();
  counter.Advance(30, 40);
  EXPECT_EQ(counter.Get(), 10);
  EXPECT_FALSE(counter.IsActive());

  counter.Resume();
  counter.Advance(5, 50);
  EXPECT_EQ(counter.Get(), 15);
  EXPECT_TRUE(counter.IsActive());

  counter.Set(80);
  EXPECT_EQ(counter.Get(), 80);
  counter.Reset();
  EXPECT_EQ(counter.Get(), 0);
  EXPECT_FALSE(counter.IsActive());
}

TEST(SiglusCounterTest, FrameModesClampOrLoop) {
  Counter one_shot;
  one_shot.StartFrame(10, 20, 100, false, false);
  one_shot.Advance(50, 0);
  EXPECT_EQ(one_shot.Get(), 15);
  EXPECT_TRUE(one_shot.IsActive());
  one_shot.Advance(100, 0);
  EXPECT_EQ(one_shot.Get(), 20);
  EXPECT_FALSE(one_shot.IsActive());

  Counter loop;
  loop.StartFrame(0, 10, 100, true, true);
  loop.Advance(0, 150);
  EXPECT_EQ(loop.Get(), 5);
  EXPECT_TRUE(loop.IsActive());

  loop.Set(8);
  EXPECT_EQ(loop.Get(), 8);
}

TEST(SiglusCounterTest, DescendingFrameLoopRepeatsWithoutOverflow) {
  Counter loop;
  loop.StartFrame(10, 0, 100, false, true);
  EXPECT_EQ(loop.Get(), 10);

  loop.Advance(50, 0);
  EXPECT_EQ(loop.Get(), 5);
  loop.Advance(50, 0);
  EXPECT_EQ(loop.Get(), 10);

  loop.Set(5);
  EXPECT_EQ(loop.Get(), 5);

  Counter long_running_loop;
  long_running_loop.StartFrame(1'000'000'000, -1'000'000'000, 97, false, true);
  long_running_loop.Advance(std::numeric_limits<std::int64_t>::max(), 0);
  const std::int64_t cycle_time = std::numeric_limits<std::int64_t>::max() % 97;
  const std::int64_t expected =
      -2'000'000'000LL * cycle_time / 97 + 1'000'000'000;
  EXPECT_EQ(long_running_loop.Get(), static_cast<int>(expected));
}

TEST(SiglusCounterTest, SaturatesReadsAndOneShotInterpolation) {
  Counter counter;
  counter.Start(false);
  counter.Advance(std::numeric_limits<std::int64_t>::max(), 0);
  EXPECT_EQ(counter.Get(), std::numeric_limits<int>::max());

  // Repeated huge deltas stay saturated instead of overflowing or narrowing
  // to a negative script value.
  counter.Advance(std::numeric_limits<std::int64_t>::max(), 0);
  EXPECT_EQ(counter.Get(), std::numeric_limits<int>::max());

  Counter descending;
  descending.StartFrame(std::numeric_limits<int>::max(),
                        std::numeric_limits<int>::min(), 100, false, false);
  descending.Advance(std::numeric_limits<std::int64_t>::max(), 0);
  EXPECT_EQ(descending.Get(), std::numeric_limits<int>::min());
  EXPECT_FALSE(descending.IsActive());
}

TEST(SiglusCounterTest, FastForwardAffectsOnlyGameTimeDomain) {
  Counter game;
  Counter real;
  game.Start(false);
  real.Start(true);

  game.Advance(32, 1);
  real.Advance(32, 1);
  EXPECT_EQ(game.Get(), 32);
  EXPECT_EQ(real.Get(), 1);

  game.Stop();
  real.Stop();
  game.Advance(320, 10);
  real.Advance(320, 10);
  EXPECT_EQ(game.Get(), 32);
  EXPECT_EQ(real.Get(), 1);
}

TEST(SiglusCounterListTest, AdvancesEverySharedSlot) {
  CounterList counters(2);
  auto first = counters.GetHandle(0).lock();
  auto second = counters.GetHandle(1).lock();
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  first->Start(false);
  second->Start(true);

  counters.Advance(40, 9);

  EXPECT_EQ(first->Get(), 40);
  EXPECT_EQ(second->Get(), 9);
  counters.Reset();
  EXPECT_EQ(first->Get(), 0);
  EXPECT_EQ(second->Get(), 0);
}

TEST(SiglusCounterListTest, RejectsInvalidIndices) {
  CounterList counters(1);
  EXPECT_THROW(counters.GetHandle(-1), std::out_of_range);
  EXPECT_THROW(counters.GetHandle(1), std::out_of_range);
}
