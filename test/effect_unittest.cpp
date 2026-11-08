// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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

#include "core/effect.hpp"
#include "core/object.hpp"
#include "core/object_internal/object_parameter.hpp"
#include "mock_clock.hpp"
#include "serialization_test_helpers.hpp"

#include <gtest/gtest.h>
#include <limits>

namespace {

TEST(StageEffectTest, DefaultsAndAppliesToLegacyRange) {
  Effect effect;

  EXPECT_EQ(effect.begin_order, 0);
  EXPECT_EQ(effect.end_order, 0);
  EXPECT_EQ(effect.begin_layer, std::numeric_limits<int>::min());
  EXPECT_EQ(effect.end_layer, std::numeric_limits<int>::max());
  EXPECT_TRUE(effect.Contains(0, -100));
  EXPECT_TRUE(effect.Contains(0, 100));
  EXPECT_FALSE(effect.Contains(1, 0));
}

TEST(StageEffectTest, ComposesObjectColourParameters) {
  Effect effect;
  effect.x = 10;
  effect.y = -5;
  effect.mono = 128;
  effect.dark = 64;
  effect.color_add_r = 42;
  effect.color_add_g = 16;
  effect.color_add_b = 5;

  ObjectParameter param;
  param.SetX(20);
  param.SetY(30);
  param.SetMono(64);
  param.SetDark(32);
  param.SetTint(RGBColour(220, 250, 254));
  effect.ApplyTo(param);

  EXPECT_EQ(param.x(), 30);
  EXPECT_EQ(param.y(), 25);
  EXPECT_EQ(param.mono(), 160);
  EXPECT_EQ(param.Dark(), 88);
  EXPECT_EQ(param.tint(), RGBColour(255, 255, 255));
}

// ------------------------------------------------------------------------------

class EffectArchiveTest : public ::testing::Test {};

TEST_F(EffectArchiveTest, ContinuesEventAtSavedPosition) {
  // A partly elapsed translation continues after loading onto a new clock.
  serialization_test::ForEachFormat([&](auto format) {
    using namespace std::chrono_literals;
    auto clock = std::make_shared<MockClock>();
    Effect source;
    source.StartEvent(0, FrameCounter(clock, Interpolation({0, 100}), 100));
    clock->AdvanceTime(25ms);
    source.Execute();
    auto target_clock = std::make_shared<MockClock>();
    Effect target;
    serialization_test::RoundTrip(source, target, format, {},
                                  {.clock = target_clock});
    target_clock->AdvanceTime(25ms);
    target.Execute();
    ObjectParameter param;
    target.ApplyTo(param);
    EXPECT_EQ(param.x(), 50);
    EXPECT_TRUE(target.IsEventRunning(0));
    target_clock->AdvanceTime(50ms);
    target.Execute();
    EXPECT_FALSE(target.IsEventRunning(0));
  });
}

TEST_F(EffectArchiveTest, RetainsRangeAndColourEffects) {
  // A static effect retains its filtering range and observable colour
  // transformation.
  serialization_test::ForEachFormat([&](auto format) {
    Effect source, target;
    source.begin_order = 3;
    source.end_order = 8;
    source.begin_layer = -2;
    source.end_layer = 4;
    source.mono = 64;
    source.dark = 32;
    source.color_add_r = 17;
    source.wipe_copy = 1;
    source.wipe_erase = 2;
    serialization_test::RoundTrip(source, target, format);
    EXPECT_TRUE(target.Contains(5, 0));
    EXPECT_FALSE(target.Contains(2, 0));
    EXPECT_FALSE(target.Contains(8, 5));
    ObjectParameter expected, actual;
    source.ApplyTo(expected);
    target.ApplyTo(actual);
    EXPECT_EQ(actual.mono(), expected.mono());
    EXPECT_EQ(actual.Dark(), expected.Dark());
    EXPECT_EQ(actual.tint(), expected.tint());
    EXPECT_EQ(target.wipe_copy, 1);
    EXPECT_EQ(target.wipe_erase, 2);
  });
}

}  // namespace
