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

#include "core/object_internal/animator.hpp"
#include "mock_clock.hpp"
#include "serialization_test_helpers.hpp"

namespace {

class AnimatorArchiveTest : public ::testing::Test {};

TEST_F(AnimatorArchiveTest, PausedAnimationRetainsPendingLap) {
  // A paused animation loads at another epoch without charging offline time.
  serialization_test::ForEachFormat([&](auto format) {
    using namespace std::chrono_literals;
    auto clock = std::make_shared<MockClock>();
    Animator source(clock);
    source.SetIsPlaying(true);
    clock->AdvanceTime(400ms);
    EXPECT_EQ(source.GetDeltaTime(), 400ms);
    clock->AdvanceTime(150ms);
    source.SetIsPlaying(false);
    auto target_clock = std::make_shared<MockClock>();
    target_clock->SetElapsed(9000ms);
    Animator target(target_clock);
    serialization_test::RoundTrip(source, target, format, {},
                                  {.clock = target_clock});
    EXPECT_FALSE(target.IsPlaying());
    EXPECT_EQ(target.GetAnimationTime(), 550ms);
    EXPECT_EQ(target.GetDeltaTime(), 150ms);
    target_clock->AdvanceTime(100ms);
    EXPECT_EQ(target.GetAnimationTime(), 550ms);
  });
}

TEST_F(AnimatorArchiveTest, RetainsAfterAction) {
  // Each animation completion policy survives restoration.
  serialization_test::ForEachFormat([&](auto format) {
    for (auto after : {AFTER_NONE, AFTER_CLEAR, AFTER_LOOP}) {
      auto clock = std::make_shared<MockClock>();
      Animator source(clock), target(clock);
      source.SetAfterAction(after);
      serialization_test::RoundTrip(source, target, format, {},
                                    {.clock = clock});
      EXPECT_EQ(target.GetAfterAction(), after);
    }
  });
}

}  // namespace
