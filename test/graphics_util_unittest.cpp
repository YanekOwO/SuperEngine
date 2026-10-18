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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "core/gameexe.hpp"
#include "core/rect.hpp"
#include "systems/system_error.hpp"
#include "utilities/graphics.hpp"

#include <stdexcept>
#include <vector>

TEST(ScreenSizeTest, RealliveModesIgnoreSiglusDimensions) {
  Gameexe gameexe;
  gameexe.parseLine("SCREEN_SIZE = 1920,1080");
  gameexe.parseLine("SCREENSIZE_MOD = 0");
  EXPECT_EQ(GetRealliveScreenSize(gameexe), Size(640, 480));
  gameexe.SetIntAt("SCREENSIZE_MOD", 1);
  EXPECT_EQ(GetRealliveScreenSize(gameexe), Size(800, 600));
  gameexe.SetAt("SCREENSIZE_MOD", {999, 1280, 720});
  EXPECT_EQ(GetRealliveScreenSize(gameexe), Size(1280, 720));
}

TEST(ScreenSizeTest, RealliveMissingAndInvalidModesKeepTheirErrors) {
  Gameexe gameexe;
  gameexe.parseLine("SCREEN_SIZE = 1280,720");
  EXPECT_THAT([&] { GetRealliveScreenSize(gameexe); },
              testing::ThrowsMessage<std::runtime_error>(testing::StrEq(
                  "Gameexe[SCREENSIZE_MOD]: Unknown Gameexe key")));
  gameexe.SetAt("SCREENSIZE_MOD", {});
  EXPECT_THAT([&] { GetRealliveScreenSize(gameexe); },
              testing::ThrowsMessage<SystemError>(
                  testing::StrEq("Missing #SCREENSIZE_MOD key")));
  gameexe.SetIntAt("SCREENSIZE_MOD", 2);
  EXPECT_THAT([&] { GetRealliveScreenSize(gameexe); },
              testing::ThrowsMessage<SystemError>(
                  testing::StrEq("Illegal #SCREENSIZE_MOD value: 2")));
  gameexe.SetAt("SCREENSIZE_MOD", {999, 1280});
  EXPECT_THAT([&] { GetRealliveScreenSize(gameexe); },
              testing::ThrowsMessage<SystemError>(
                  testing::StrEq("Illegal #SCREENSIZE_MOD value: 999")));
}

TEST(ScreenSizeTest, SiglusDimensionsIgnoreRealliveModes) {
  Gameexe gameexe;
  gameexe.parseLine("SCREENSIZE_MOD = 999,1920,1080");
  gameexe.parseLine("SCREEN_SIZE = 1280,720");
  EXPECT_EQ(GetSiglusScreenSize(gameexe), Size(1280, 720));
  gameexe.SetIntAt("SCREENSIZE_MOD", 0);
  gameexe.SetAt("SCREEN_SIZE", {1920, 1080});
  EXPECT_EQ(GetSiglusScreenSize(gameexe), Size(1920, 1080));
}

TEST(ScreenSizeTest, SiglusMissingDimensionsUseLegacyDefault) {
  Gameexe gameexe;
  EXPECT_EQ(GetSiglusScreenSize(gameexe), Size(800, 600));
  gameexe.parseLine("SCREENSIZE_MOD = 0");
  EXPECT_EQ(GetSiglusScreenSize(gameexe), Size(800, 600));
}

TEST(ScreenSizeTest, SiglusInvalidDimensionsNameTheSetting) {
  Gameexe gameexe;
  const std::vector<std::vector<GexeVal>> invalid = {
      {}, {1280}, {1280, 720, 1}, {0, 720}, {1280, 0}, {-1, 720}, {1280, -1}};
  for (const auto& dimensions : invalid) {
    SCOPED_TRACE(testing::PrintToString(dimensions));
    gameexe.SetAt("SCREEN_SIZE", dimensions);
    EXPECT_THAT(
        [&] { GetSiglusScreenSize(gameexe); },
        testing::ThrowsMessage<SystemError>(testing::HasSubstr("SCREEN_SIZE")));
  }
  gameexe.SetStringAt("SCREEN_SIZE", "invalid");
  EXPECT_THROW(GetSiglusScreenSize(gameexe), SystemError);
}

constexpr float EPS = 1e-6f;

TEST(RotateAroundTest, ZeroDegreeRotationLeavesPointUnchanged) {
  auto [x, y] = RotateAround(5, -3, 2, 7, 0);

  EXPECT_FLOAT_EQ(x, 5);
  EXPECT_FLOAT_EQ(y, -3);
}

TEST(RotateAroundTest, CenterPointIsInvariantUnderAnyRotation) {
  auto [x, y] = RotateAround(10, 20, 10, 20, 123);

  EXPECT_FLOAT_EQ(x, 10);
  EXPECT_FLOAT_EQ(y, 20);
}

TEST(RotateAroundTest, PositiveNinetyDegreesAroundOrigin) {
  auto [x, y] = RotateAround(1, 0, 0, 0, 90);

  EXPECT_NEAR(x, 0, EPS);
  EXPECT_NEAR(y, 1, EPS);
}

TEST(RotateAroundTest, PositiveNinetyDegreesAroundNonOriginCenter) {
  auto [x, y] = RotateAround(12, 10, 10, 10, 90);

  EXPECT_NEAR(x, 10, EPS);
  EXPECT_NEAR(y, 12, EPS);
}

TEST(RotateAroundTest, NegativeNinetyDegreesUsesOppositeDirection) {
  auto [x, y] = RotateAround(0, 1, 0, 0, -90);

  EXPECT_NEAR(x, 1, EPS);
  EXPECT_NEAR(y, 0, EPS);
}
