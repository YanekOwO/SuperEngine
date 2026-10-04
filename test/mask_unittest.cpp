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
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
// -----------------------------------------------------------------------

#include <gtest/gtest.h>

#include "core/mask.hpp"
#include "mock_clock.hpp"

#include <chrono>
#include <memory>

using std::chrono_literals::operator""ms;

namespace {

FrameCounter MakeMaskCounter(
    const std::shared_ptr<Clock>& clock,
    int start,
    int end,
    int duration_ms,
    InterpolationType type = InterpolationType::OneShot) {
  return FrameCounter(
      clock, Interpolation(Range(start, end), type, InterpolationMode::Linear),
      duration_ms);
}

TEST(MaskTest, MutatorsForDifferentParametersRunIndependently) {
  auto clock = std::make_shared<MockClock>();
  Mask mask;
  mask.AddMutator(Mask::ParameterMutator(&Mask::Parameters::x,
                                         MakeMaskCounter(clock, 0, 10, 100)));
  mask.AddMutator(Mask::ParameterMutator(&Mask::Parameters::y,
                                         MakeMaskCounter(clock, 20, 40, 200)));

  mask.Execute();
  EXPECT_EQ(mask.Param().x, 0);
  EXPECT_EQ(mask.Param().y, 20);
  EXPECT_TRUE(mask.IsMutatorRunning(&Mask::Parameters::x));
  EXPECT_TRUE(mask.IsMutatorRunning(&Mask::Parameters::y));

  clock->AdvanceTime(100ms);
  mask.Execute();
  EXPECT_EQ(mask.Param().x, 10);
  EXPECT_EQ(mask.Param().y, 30);
  EXPECT_FALSE(mask.IsMutatorRunning(&Mask::Parameters::x));
  EXPECT_TRUE(mask.IsMutatorRunning(&Mask::Parameters::y));

  clock->AdvanceTime(100ms);
  mask.Execute();
  EXPECT_EQ(mask.Param().y, 40);
  EXPECT_FALSE(mask.IsMutatorRunning(&Mask::Parameters::y));
}

TEST(MaskTest, EndMutatorOnlyEndsMatchingParameter) {
  auto clock = std::make_shared<MockClock>();
  Mask mask;
  mask.AddMutator(Mask::ParameterMutator(&Mask::Parameters::x,
                                         MakeMaskCounter(clock, 0, 100, 1000)));
  mask.AddMutator(Mask::ParameterMutator(
      &Mask::Parameters::y,
      MakeMaskCounter(clock, 0, 100, 1000, InterpolationType::Loop)));

  clock->AdvanceTime(250ms);
  mask.EndMutator(&Mask::Parameters::x);
  EXPECT_EQ(mask.Param().x, 100);
  EXPECT_FALSE(mask.IsMutatorRunning(&Mask::Parameters::x));
  EXPECT_TRUE(mask.IsMutatorRunning(&Mask::Parameters::y));

  mask.EndMutator(&Mask::Parameters::y);
  EXPECT_EQ(mask.Param().y, 25);
  EXPECT_FALSE(mask.IsMutatorRunning(&Mask::Parameters::y));
}

TEST(MaskTest, ResetClearsParametersAssetAndMutators) {
  auto clock = std::make_shared<MockClock>();
  Mask mask;
  mask.Create("mask.png", nullptr);
  mask.Param().x = 12;
  mask.Param().y = 34;
  mask.AddMutator(Mask::ParameterMutator(&Mask::Parameters::x,
                                         MakeMaskCounter(clock, 12, 50, 100)));

  mask.Reset();

  EXPECT_EQ(mask.Param().x, 0);
  EXPECT_EQ(mask.Param().y, 0);
  EXPECT_TRUE(mask.filename().empty());
  EXPECT_FALSE(mask.surface());
  EXPECT_FALSE(mask.IsMutatorRunning(&Mask::Parameters::x));
}

TEST(MaskTest, CreateReinitializesParametersAndMutators) {
  auto clock = std::make_shared<MockClock>();
  Mask mask;
  mask.Param().x = 12;
  mask.Param().y = 34;
  mask.AddMutator(Mask::ParameterMutator(&Mask::Parameters::x,
                                         MakeMaskCounter(clock, 12, 50, 100)));

  mask.Create("replacement.png", nullptr);

  EXPECT_EQ(mask.Param().x, 0);
  EXPECT_EQ(mask.Param().y, 0);
  EXPECT_EQ(mask.filename(), "replacement.png");
  EXPECT_FALSE(mask.IsMutatorRunning(&Mask::Parameters::x));
}

}  // namespace
