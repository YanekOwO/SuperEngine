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

#include <gtest/gtest.h>

#include "core/effect.hpp"
#include "core/object_internal/object_parameter.hpp"

#include <limits>

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
