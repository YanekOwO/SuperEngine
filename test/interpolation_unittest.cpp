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
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
// -----------------------------------------------------------------------

#include <gtest/gtest.h>

#include "core/interpolation.hpp"

#include <cmath>

const double EPS = 1e-6;

TEST(InterpolationTests, Interpolate) {
  {
    InterpolationRange range(0.0, 5.0, 10.0);
    double amount = 100.0;   // Suppose start_val = 0, end_val = 100
    double expected = 50.0;  // Halfway

    double result = Interpolate(range, amount, InterpolationMode::Linear);
    EXPECT_NEAR(result, expected, EPS);
  }

  {
    InterpolationRange range(0.0, 5.0, 10.0);
    double amount = 100.0;
    double expected = amount * log(1.0 * (5 - 0) / (10 - 0) + 1) / log(2);

    double result = Interpolate(range, amount, InterpolationMode::LogEaseOut);
    EXPECT_NEAR(result, expected, EPS);
  }

  {
    InterpolationRange range(0.0, 5.0, 10.0);
    double amount = 100.0;
    double p = log(1.5) / log(2);
    double expected = amount - (1 - p) * amount;

    double result = Interpolate(range, amount, InterpolationMode::LogEaseIn);
    EXPECT_NEAR(result, expected, EPS);
  }
}

TEST(InterpolationTests, InterpolateBetween) {
  {
    InterpolationRange time(0.0, 5.0, 10.0);
    Range value(100.0, 200.0);
    double expected = 150.0;

    double result = InterpolateBetween(time, value, InterpolationMode::Linear);
    EXPECT_NEAR(result, expected, EPS);
  }

  {
    InterpolationRange range(0.0, 5.0, 10.0);
    Range value(100.0, 200.0);
    double expected = 158.496;

    double result =
        InterpolateBetween(range, value, InterpolationMode::LogEaseOut);
    EXPECT_NEAR(result, expected, 1e-3);  // Allowing higher tolerance
  }

  {
    InterpolationRange range(0.0, 5.0, 10.0);
    Range value(100.0, 200.0);
    double expected = 158.49696;

    double result =
        InterpolateBetween(range, value, InterpolationMode::LogEaseIn);
    EXPECT_NEAR(result, expected, 1e-3);
  }
}

TEST(InterpolationTests, QuadraticEasingModes) {
  InterpolationRange midpoint(0.0, 5.0, 10.0);
  Range value(100.0, 200.0);

  EXPECT_NEAR(Interpolate(midpoint, 100.0, InterpolationMode::Accelerate), 25.0,
              EPS);
  EXPECT_NEAR(Interpolate(midpoint, 100.0, InterpolationMode::Decelerate), 75.0,
              EPS);
  EXPECT_NEAR(
      InterpolateBetween(midpoint, value, InterpolationMode::Accelerate), 125.0,
      EPS);
  EXPECT_NEAR(
      InterpolateBetween(midpoint, value, InterpolationMode::Decelerate), 175.0,
      EPS);

  EXPECT_NEAR(Interpolate(InterpolationRange(0.0, 0.0, 10.0), 100.0,
                          InterpolationMode::Accelerate),
              0.0, EPS);
  EXPECT_NEAR(Interpolate(InterpolationRange(0.0, 10.0, 10.0), 100.0,
                          InterpolationMode::Accelerate),
              100.0, EPS);

  EXPECT_NEAR(Interpolate(InterpolationRange(0.0, 0.0, 10.0), 100.0,
                          InterpolationMode::Decelerate),
              0.0, EPS);
  EXPECT_NEAR(Interpolate(InterpolationRange(0.0, 10.0, 10.0), 100.0,
                          InterpolationMode::Decelerate),
              100.0, EPS);
}

TEST(InterpolationTests, Clamped) {
  {
    InterpolationRange range(10.0, 5.0, 20.0);  // current < start
    double amount = 100.0;
    double expected = 0.0;  // percentage = 0

    double result = Interpolate(range, amount, InterpolationMode::Linear);
    EXPECT_NEAR(result, expected, EPS);
  }

  {
    InterpolationRange range(0.0, 25.0, 20.0);  // current > end
    double amount = 100.0;
    double expected = 100.0;  // percentage = 1

    double result = Interpolate(range, amount, InterpolationMode::Linear);
    EXPECT_NEAR(result, expected, EPS);
  }

  {
    InterpolationRange range(10.0, 5.0, 20.0);  // current < start
    Range value(100.0, 200.0);
    double expected = 100.0;  // start_val + 0

    double result = InterpolateBetween(range, value, InterpolationMode::Linear);
    EXPECT_NEAR(result, expected, EPS);
  }

  {
    InterpolationRange range(0.0, 25.0, 20.0);  // current > end
    Range value(100.0, 200.0);
    double expected = 200.0;  // start_val + amount (100)

    double result = InterpolateBetween(range, value, InterpolationMode::Linear);
    EXPECT_NEAR(result, expected, EPS);
  }
}

TEST(InterpolationTests, InvalidInterpolationMode) {
  InterpolationRange range(0.0, 5.0, 10.0);
  double amount = 100.0;

  // Casting an invalid integer to InterpolationMode
  InterpolationMode invalidMode = static_cast<InterpolationMode>(999);

  EXPECT_THROW(
      { Interpolate(range, amount, invalidMode); }, std::invalid_argument);
}

TEST(InterpolationTests, DefaultRange) {
  InterpolationRange defaultRange;
  EXPECT_DOUBLE_EQ(defaultRange.start, 0.0);
  EXPECT_DOUBLE_EQ(defaultRange.current, 0.0);
  EXPECT_DOUBLE_EQ(defaultRange.end, 1.0);
}

TEST(InterpolationTests, MaxValues) {
  InterpolationRange range(0.0, 100.0, 1000.0);
  double amount = 1000000.0;
  [[maybe_unused]] double percentage = (100.0 - 0.0) / (1000.0 - 0.0);  // 0.1
  double expected = 0.1 * amount;  // 100000.0

  double result = Interpolate(range, amount, InterpolationMode::Linear);
  EXPECT_NEAR(result, expected, EPS);
}

TEST(InterpolationTests, MinValues) {
  InterpolationRange range(-100.0, -50.0, 0.0);
  double amount = 200.0;
  [[maybe_unused]] double percentage =
      (-50.0 - (-100.0)) / (0.0 - (-100.0));  // 0.5
  double expected = 0.5 * amount;             // 100.0

  double result = Interpolate(range, amount, InterpolationMode::Linear);
  EXPECT_NEAR(result, expected, EPS);
}

TEST(InterpolationTests, BoundaryValues) {
  {
    InterpolationRange range(0.0, 0.0, 10.0);
    double amount = 100.0;
    double expected = 0.0;

    double result = Interpolate(range, amount, InterpolationMode::Linear);
    EXPECT_NEAR(result, expected, EPS);
  }

  {
    InterpolationRange range(0.0, 10.0, 10.0);
    double amount = 100.0;
    double expected = 100.0;

    double result = Interpolate(range, amount, InterpolationMode::Linear);
    EXPECT_NEAR(result, expected, EPS);
  }
}
