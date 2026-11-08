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

#include "utilities/checkpoint_clock.hpp"

#include <gtest/gtest.h>
#include "mock_clock.hpp"

namespace {
using namespace std::chrono_literals;

TEST(CheckpointClockTest, PreparationDoesNotAdvanceResourceTime) {
  // Loading may spend arbitrary time constructing resources; both clock
  // domains remain at their captured instant until activation.
  auto source = std::make_shared<MockClock>();
  CheckpointClock clock(source);
  auto now = clock.GetTime();
  source->AdvanceTime(10s);
  EXPECT_EQ(clock.GetTime(), now);
  EXPECT_EQ(clock.GetTicks(), 0ms);
}

TEST(CheckpointClockTest, ActivationExcludesTimeSpentPreparing) {
  // The loaded resource resumes from its frozen instant rather than counting
  // preparation or fade time; repeated activation does not reset progress.
  auto source = std::make_shared<MockClock>();
  CheckpointClock clock(source);
  auto now = clock.GetTime();
  source->AdvanceTime(10s);
  clock.Activate();
  source->AdvanceTime(7ms);
  clock.Activate();
  EXPECT_EQ(clock.GetTime(), now + 7ms);
  EXPECT_EQ(clock.GetTicks(), 7ms);
}

}  // namespace
