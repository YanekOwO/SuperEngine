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
// -----------------------------------------------------------------------

#include "utilities/stopwatch.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>
#include "mock_clock.hpp"
#include "serialization_test_helpers.hpp"
#include "utilities/clock.hpp"

namespace {
namespace chr = std::chrono;
using namespace std::chrono_literals;

using timepoint_t = Clock::timepoint_t;
using duration_t = Clock::duration_t;

class StopwatchTest : public ::testing::Test {
 protected:
  struct StopwatchTestCtx {
    std::vector<std::pair<chr::milliseconds, Stopwatch::Action>> actions;
    std::vector<
        std::tuple<chr::milliseconds, Stopwatch::State, Stopwatch::duration_t>>
        checkers;

    void Doit() {
      std::vector<chr::milliseconds> keytimes;
      keytimes.reserve(actions.size() + checkers.size());
      for (const auto& it : actions)
        keytimes.push_back(it.first);
      for (const auto& it : checkers)
        keytimes.push_back(std::get<0>(it));

      std::sort(actions.begin(), actions.end());
      std::sort(checkers.begin(), checkers.end());
      std::sort(keytimes.begin(), keytimes.end());
      keytimes.erase(std::unique(keytimes.begin(), keytimes.end()),
                     keytimes.end());

      auto clock = std::make_shared<MockClock>();
      auto epoch = std::chrono::steady_clock::now();
      clock->SetEpoch(epoch);
      Stopwatch stopwatch(clock);

      size_t action_idx = 0, checker_idx = 0;
      for (const auto& duration : keytimes) {
        auto now = epoch + duration;
        clock->SetEpoch(now);

        while (action_idx < actions.size() &&
               actions[action_idx].first == duration) {
          stopwatch.Apply(actions[action_idx++].second);
        }

        while (checker_idx < checkers.size() &&
               std::get<0>(checkers[checker_idx]) == duration) {
          auto expect_state = std::get<1>(checkers[checker_idx]);
          auto expect_anmtime =
              chr::duration_cast<Stopwatch::duration_t>(std::get<2>(
                  checkers[checker_idx]));  // Cast before EXPECT_ marco to
                                            // prevent unexpected gtest behavior
          ++checker_idx;

          EXPECT_EQ(expect_state, stopwatch.GetState())
              << "at tick " << duration.count();
          EXPECT_EQ(expect_anmtime, stopwatch.GetReading())
              << "at tick " << duration.count();
        }
      }
    }
  };

  // possible controlling actions
  inline static constexpr auto Run = Stopwatch::Action::Run,
                               Pause = Stopwatch::Action::Pause,
                               Reset = Stopwatch::Action::Reset;

  // possible stopwatch state
  inline static constexpr auto Running = Stopwatch::State::Running,
                               Paused = Stopwatch::State::Paused,
                               Finished = Stopwatch::State::Stopped;
};

TEST_F(StopwatchTest, Countup) {
  StopwatchTestCtx testinstance;

  testinstance.actions = {{0ms, Run}, {15ms, Run}};
  testinstance.checkers = {
      {1ms, Running, 1ms},
      {10ms, Running, 10ms},
      {50ms, Running, 50ms},
  };

  testinstance.Doit();
}

TEST_F(StopwatchTest, ToggleRun) {
  StopwatchTestCtx testinstance;

  testinstance.actions = {
      {0ms, Run},
      {12ms, Pause},
      {20ms, Pause},
      {22ms, Run},
  };
  testinstance.checkers = {{0ms, Running, 0ms},
                           {11ms, Running, 11ms},
                           {20ms, Paused, 12ms},
                           {32ms, Running, 22ms}};

  testinstance.Doit();
}

TEST_F(StopwatchTest, StopReset) {
  StopwatchTestCtx testinstance;

  testinstance.actions = {
      {5ms, Run}, {12ms, Reset}, {15ms, Pause}, {20ms, Run}};
  testinstance.checkers = {
      {0ms, Paused, 0ms},
      {11ms, Running, 7ms},
      {13ms, Finished, 0ms},
      {16ms, Finished,
       0ms},  // Pause action should be ignored when at Finished state
      {32ms, Running, 12ms}};
}

TEST_F(StopwatchTest, StopWhenAlreadyStopped) {
  // Test applying Stop action when already in Finished state.
  StopwatchTestCtx testinstance;

  testinstance.actions = {
      {0ms, Reset},
      {10ms, Reset},
  };

  testinstance.checkers = {
      {5ms, Finished, 0ms},
      {15ms, Finished, 0ms},
  };

  testinstance.Doit();
}

TEST_F(StopwatchTest, RapidTransitions) {
  StopwatchTestCtx testinstance;

  testinstance.actions = {
      {0ms, Run}, {1ms, Pause}, {2ms, Run}, {3ms, Pause}, {4ms, Run},
  };

  testinstance.checkers = {
      {0ms, Running, 0ms}, {1ms, Paused, 1ms},  {2ms, Running, 1ms},
      {3ms, Paused, 2ms},  {4ms, Running, 2ms}, {5ms, Running, 3ms},
  };

  testinstance.Doit();
}

TEST_F(StopwatchTest, NoActions) {
  StopwatchTestCtx testinstance;

  testinstance.checkers = {
      {0ms, Paused, 0ms},
      {10ms, Paused, 0ms},
      {20ms, Paused, 0ms},
  };

  testinstance.Doit();
}

TEST_F(StopwatchTest, Interrupt) {
  StopwatchTestCtx testinstance;

  testinstance.actions = {
      {0ms, Run},
      {0ms, Reset},
  };

  testinstance.checkers = {
      {0ms, Finished, 0ms},
      {10ms, Finished, 0ms},
  };

  testinstance.Doit();
}

TEST_F(StopwatchTest, LongDuration) {
  using namespace std::chrono_literals;
  StopwatchTestCtx testinstance;

  testinstance.actions = {
      {0ms, Run},
      {5000h, Pause},
      {7000h, Run},
      {10000h, Reset},
  };

  testinstance.checkers = {
      {2500h, Running, 2500h},
      {6000h, Paused, 5000h},
      {8000h, Running, 6000h},
      {11000h, Finished, 0ms},
  };

  testinstance.Doit();
}

TEST_F(StopwatchTest, MultipleToggles) {
  StopwatchTestCtx testinstance;

  testinstance.actions = {
      {0ms, Run},     {100ms, Pause}, {200ms, Run},
      {300ms, Pause}, {400ms, Run},   {500ms, Reset},
  };

  testinstance.checkers = {
      {50ms, Running, 50ms},  {150ms, Paused, 100ms},  {250ms, Running, 150ms},
      {350ms, Paused, 200ms}, {450ms, Running, 250ms}, {550ms, Finished, 0ms},
  };

  testinstance.Doit();
}

TEST_F(StopwatchTest, TypicalControl) {
  StopwatchTestCtx testinstance;

  testinstance.actions = {
      {0ms, Run},
      {5ms, Pause},
      {10ms, Run},
      {15ms, Reset},
  };

  testinstance.checkers = {
      {3ms, Running, 3ms},
      {8ms, Paused, 5ms},
      {12ms, Running, 7ms},
      {18ms, Finished, 0ms},
  };

  testinstance.Doit();
}

TEST_F(StopwatchTest, ReadFromInit) {
  StopwatchTestCtx testinstance;

  testinstance.actions = {
      // none
  };

  testinstance.checkers = {{0ms, Paused, 0ms}};

  testinstance.Doit();
}

TEST_F(StopwatchTest, BrokenClock) {
  auto clock = std::make_shared<MockClock>();
  auto epoch = std::chrono::steady_clock::now();
  clock->SetEpoch(epoch);

  Stopwatch stopwatch(clock);
  stopwatch.Apply(Run);

  clock->SetEpoch(epoch + 10ms);  // Advance time
  EXPECT_EQ(stopwatch.GetReading(), 10ms);

  clock->SetEpoch(epoch + 5ms);  // Move time backward
  EXPECT_THROW(stopwatch.GetReading(), std::runtime_error);
}

TEST_F(StopwatchTest, LapTime) {
  auto clock = std::make_shared<MockClock>();
  auto epoch = std::chrono::steady_clock::now();
  clock->SetEpoch(epoch);

  Stopwatch stopwatch(clock);
  stopwatch.Apply(Run);

  clock->SetEpoch(epoch + 5ms);
  EXPECT_EQ(stopwatch.LapTime(), 5ms);

  clock->SetEpoch(epoch + 10ms);
  stopwatch.Apply(Pause);

  clock->SetEpoch(epoch + 25ms);
  stopwatch.Apply(Run);

  clock->SetEpoch(epoch + 30ms);
  EXPECT_EQ(stopwatch.LapTime(), 10ms);

  clock->SetEpoch(epoch + 40ms);
  EXPECT_EQ(stopwatch.LapTime(), 10ms);
}

// ------------------------------------------------------------------------------

class StopwatchArchiveTest : public ::testing::Test {};

TEST_F(StopwatchArchiveTest, SavesUnpolledElapsedTime) {
  // Saving a running timer captures elapsed time without consuming its pending
  // lap.
  serialization_test::ForEachFormat([&](auto format) {
    using namespace std::chrono_literals;
    auto clock = std::make_shared<MockClock>();
    Stopwatch source(clock);
    source.Apply(Stopwatch::Action::Run);
    clock->AdvanceTime(40ms);
    EXPECT_EQ(source.LapTime(), 40ms);
    clock->AdvanceTime(15ms);
    auto loading_clock = std::make_shared<MockClock>();
    loading_clock->SetElapsed(9000ms);
    Stopwatch target(loading_clock);
    serialization_test::RoundTrip(source, target, format, {},
                                  {.clock = loading_clock});
    EXPECT_EQ(target.GetReading(), 55ms);
    EXPECT_EQ(target.LapTime(), 15ms);
    EXPECT_EQ(source.LapTime(), 15ms);
    loading_clock->AdvanceTime(5ms);
    EXPECT_EQ(target.GetReading(), 60ms);
  });
}

TEST_F(StopwatchArchiveTest, StoppedTimerDoesNotRestart) {
  // A stopped timer remains stopped after loading and clock advancement.
  serialization_test::ForEachFormat([&](auto format) {
    using namespace std::chrono_literals;
    auto clock = std::make_shared<MockClock>();
    Stopwatch source(clock), target(clock);
    source.Apply(Stopwatch::Action::Run);
    clock->AdvanceTime(100ms);
    source.Apply(Stopwatch::Action::Reset);
    serialization_test::RoundTrip(source, target, format, {}, {.clock = clock});
    clock->AdvanceTime(50ms);
    EXPECT_EQ(target.GetState(), Stopwatch::State::Stopped);
    EXPECT_EQ(target.GetReading(), 0ms);
  });
}

}  // namespace
