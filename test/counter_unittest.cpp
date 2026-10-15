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

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>

using std::chrono::milliseconds;
using std::chrono_literals::operator""ms;

// ------------------------------------------------------------------------------
// elapsed-time counter mode

TEST(SiglusCounterTest, SelectsGameOrRealTimeDomain) {
  Counter game;
  Counter real;
  game.StartGame();
  real.StartReal();

  game.Advance(25ms, 7ms);
  real.Advance(25ms, 7ms);

  EXPECT_EQ(game.Get(), 25);
  EXPECT_EQ(real.Get(), 7);
}

TEST(SiglusCounterTest, StopResumeSetAndResetAreFrameDriven) {
  Counter counter;
  counter.StartGame();
  counter.Advance(10ms, 20ms);
  counter.Stop();
  counter.Advance(30ms, 40ms);
  EXPECT_EQ(counter.Get(), 10);
  EXPECT_FALSE(counter.IsActive());

  counter.Resume();
  counter.Advance(5ms, 50ms);
  EXPECT_EQ(counter.Get(), 15);
  EXPECT_TRUE(counter.IsActive());

  counter.Set(80);
  EXPECT_EQ(counter.Get(), 80);
  counter.Reset();
  EXPECT_EQ(counter.Get(), 0);
  EXPECT_FALSE(counter.IsActive());
}

TEST(SiglusCounterTest, FastForwardAffectsOnlyGameTimeDomain) {
  Counter game;
  Counter real;
  game.StartGame();
  real.StartReal();

  game.Advance(32ms, 1ms);
  real.Advance(32ms, 1ms);
  EXPECT_EQ(game.Get(), 32);
  EXPECT_EQ(real.Get(), 1);

  game.Stop();
  real.Stop();
  game.Advance(320ms, 10ms);
  real.Advance(320ms, 10ms);
  EXPECT_EQ(game.Get(), 32);
  EXPECT_EQ(real.Get(), 1);
}

TEST(SiglusCounterTest, NegativeDeltasDoNotAdvanceEitherTimeDomain) {
  Counter game;
  Counter real;
  game.StartGame();
  real.StartReal();
  game.Set(-5);
  real.Set(10);

  game.Advance(-20ms, 30ms);
  real.Advance(30ms, -20ms);
  EXPECT_EQ(game.Get(), -5);
  EXPECT_EQ(real.Get(), 10);

  game.Advance(10ms, 0ms);
  EXPECT_EQ(game.Get(), 5);
}

// ------------------------------------------------------------------------------
// frame mode

TEST(SiglusCounterTest, FrameModesClampOrLoop) {
  Counter one_shot;
  one_shot.StartFrameGame(10, 20, 100ms, false);
  one_shot.Advance(50ms, 0ms);
  EXPECT_EQ(one_shot.Get(), 15);
  EXPECT_TRUE(one_shot.IsActive());
  one_shot.Advance(100ms, 0ms);
  EXPECT_EQ(one_shot.Get(), 20);
  EXPECT_FALSE(one_shot.IsActive());

  Counter loop;
  loop.StartFrameReal(0, 10, 100ms, true);
  loop.Advance(0ms, 150ms);
  EXPECT_EQ(loop.Get(), 5);
  EXPECT_TRUE(loop.IsActive());

  loop.Set(8);
  EXPECT_EQ(loop.Get(), 8);
}

TEST(SiglusCounterTest, SwitchingModesResetsProgressAndSelectsTimeDomain) {
  Counter counter;
  EXPECT_FALSE(counter.IsFrameMode());

  counter.StartFrameReal(10, 20, 100ms, true);
  EXPECT_TRUE(counter.IsFrameMode());
  counter.Advance(80ms, 40ms);
  EXPECT_EQ(counter.Get(), 14);

  counter.StartGame();
  EXPECT_FALSE(counter.IsFrameMode());
  EXPECT_EQ(counter.Get(), 0);
  counter.Advance(150ms, 3ms);
  EXPECT_EQ(counter.Get(), 150);
  EXPECT_TRUE(counter.IsActive());

  counter.StartFrameGame(20, 40, 100ms, false);
  EXPECT_TRUE(counter.IsFrameMode());
  EXPECT_EQ(counter.Get(), 20);
  counter.Advance(50ms, 7ms);
  EXPECT_EQ(counter.Get(), 30);

  counter.StartReal();
  EXPECT_FALSE(counter.IsFrameMode());
  EXPECT_EQ(counter.Get(), 0);
  counter.Advance(5ms, 200ms);
  EXPECT_EQ(counter.Get(), 200);
  EXPECT_TRUE(counter.IsActive());

  counter.StartFrameReal(5, 15, 100ms, false);
  counter.Advance(200ms, 50ms);
  EXPECT_EQ(counter.Get(), 10);
  counter.Reset();
  EXPECT_FALSE(counter.IsFrameMode());
  EXPECT_FALSE(counter.IsActive());
  EXPECT_EQ(counter.Get(), 0);
  counter.Resume();
  counter.Advance(9ms, 1ms);
  EXPECT_EQ(counter.Get(), 9);
  EXPECT_TRUE(counter.IsActive());
}

TEST(SiglusCounterTest, DescendingFrameLoopRepeatsWithoutOverflow) {
  Counter loop;
  loop.StartFrameGame(10, 0, 100ms, true);
  EXPECT_EQ(loop.Get(), 10);

  loop.Advance(50ms, 0ms);
  EXPECT_EQ(loop.Get(), 5);
  loop.Advance(50ms, 0ms);
  EXPECT_EQ(loop.Get(), 10);

  loop.Set(5);
  EXPECT_EQ(loop.Get(), 5);

  Counter long_running_loop;
  long_running_loop.StartFrameGame(1'000'000'000, -1'000'000'000, 97ms, true);
  long_running_loop.Advance(milliseconds::max(), 0ms);
  const std::int64_t cycle_time = std::numeric_limits<std::int64_t>::max() % 97;
  const std::int64_t expected =
      -2'000'000'000LL * cycle_time / 97 + 1'000'000'000;
  EXPECT_EQ(long_running_loop.Get(), static_cast<int>(expected));
}

TEST(SiglusCounterTest, SaturatesReadsAndOneShotInterpolation) {
  Counter counter;
  counter.StartGame();
  counter.Advance(milliseconds::max(), 0ms);
  EXPECT_EQ(counter.Get(), std::numeric_limits<int>::max());

  // Repeated huge deltas stay saturated instead of overflowing or narrowing
  // to a negative script value.
  counter.Advance(milliseconds::max(), 0ms);
  EXPECT_EQ(counter.Get(), std::numeric_limits<int>::max());

  Counter descending;
  descending.StartFrameGame(std::numeric_limits<int>::max(),
                            std::numeric_limits<int>::min(), 100ms, false);
  descending.Advance(milliseconds::max(), 0ms);
  EXPECT_EQ(descending.Get(), std::numeric_limits<int>::min());
  EXPECT_FALSE(descending.IsActive());
}

TEST(SiglusCounterTest, FrameDurationAcceptsOtherChronoUnits) {
  Counter counter;
  counter.StartFrameGame(0, 100, std::chrono::seconds(2), false);
  counter.Advance(std::chrono::seconds(1), 0ms);
  EXPECT_EQ(counter.Get(), 50);
  EXPECT_TRUE(counter.IsActive());

  counter.Advance(std::chrono::seconds(1), 0ms);
  EXPECT_EQ(counter.Get(), 100);
  EXPECT_FALSE(counter.IsActive());
}

TEST(SiglusCounterTest, LongFrameDurationsInterpolateAndSetWithoutOverflow) {
  const auto duration = milliseconds::max() - 1ms;
  for (bool loop : {false, true}) {
    Counter counter;
    counter.StartFrameGame(-1'000'000'000, 1'000'000'000, duration, loop);
    counter.Advance(duration / 2, 0ms);
    EXPECT_EQ(counter.Get(), 0);
    EXPECT_TRUE(counter.IsActive());

    counter.Set(0);
    EXPECT_EQ(counter.Get(), 0);
    counter.Set(std::numeric_limits<int>::min());
    EXPECT_EQ(counter.Get(), -1'000'000'000);
    counter.Set(std::numeric_limits<int>::max());
    EXPECT_EQ(counter.Get(), loop ? 999'999'999 : 1'000'000'000);

    counter.Advance(milliseconds::max(), 0ms);
    EXPECT_EQ(counter.Get(), loop ? -1'000'000'000 : 1'000'000'000);
    EXPECT_EQ(counter.IsActive(), loop);

    counter.StartFrameReal(1'000'000'000, -1'000'000'000, duration, loop);
    counter.Advance(0ms, duration / 2);
    EXPECT_EQ(counter.Get(), 0);
    counter.Set(0);
    EXPECT_EQ(counter.Get(), 0);
    counter.Set(std::numeric_limits<int>::max());
    EXPECT_EQ(counter.Get(), 1'000'000'000);
    counter.Set(std::numeric_limits<int>::min());
    EXPECT_EQ(counter.Get(), loop ? -999'999'999 : -1'000'000'000);
  }
}

TEST(SiglusCounterTest, NonpositiveFrameDurationsHoldEndpoint) {
  for (auto duration : {0ms, -1ms, milliseconds::min()}) {
    for (bool loop : {false, true}) {
      Counter counter;
      counter.StartFrameGame(10, 20, duration, loop);
      EXPECT_EQ(counter.Get(), 20);
      EXPECT_TRUE(counter.IsActive());
      counter.Set(15);
      EXPECT_EQ(counter.Get(), 20);
      counter.Advance(0ms, 0ms);
      EXPECT_EQ(counter.Get(), 20);
      EXPECT_EQ(counter.IsActive(), loop);
    }
  }
}

TEST(SiglusCounterListTest, AdvancesActiveCountersInTheirSelectedTimeDomain) {
  CounterList counters(3);
  auto game = counters.GetHandle(0);
  auto real = counters.GetHandle(1);
  auto inactive = counters.GetHandle(2);
  game->StartGame();
  real->StartReal();
  inactive->Set(42);

  counters.Advance(32ms, 1ms);
  EXPECT_EQ(game->Get(), 32);
  EXPECT_EQ(real->Get(), 1);
  EXPECT_EQ(inactive->Get(), 42);

  game->Stop();
  counters.Advance(10ms, 2ms);
  EXPECT_EQ(game->Get(), 32);
  EXPECT_EQ(real->Get(), 3);

  counters.Reset();
  EXPECT_EQ(counters.size(), 3);
  EXPECT_EQ(counters.GetHandle(0), game);
  EXPECT_EQ(counters.GetHandle(1), real);
  EXPECT_EQ(counters.GetHandle(2), inactive);
  counters.Advance(10ms, 20ms);
  for (const auto& counter : {game, real, inactive}) {
    EXPECT_EQ(counter->Get(), 0);
    EXPECT_FALSE(counter->IsActive());
  }
}
