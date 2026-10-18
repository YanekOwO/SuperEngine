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
//
// -----------------------------------------------------------------------

#include <gtest/gtest.h>

#include "core/rect.hpp"
#include "long_operations/effects/drawer_effect.hpp"

using namespace lop::DrawerEffectDetails;

TEST(RotatorTest, RotateSize) {
  Size screen(1920, 1080);

  EXPECT_EQ(Rotator(screen, Direction::TopToBottom).Rotate(screen),
            Size(1920, 1080));
  EXPECT_EQ(Rotator(screen, Direction::BottomToTop).Rotate(screen),
            Size(1920, 1080));
  EXPECT_EQ(Rotator(screen, Direction::LeftToRight).Rotate(screen),
            Size(1080, 1920));
  EXPECT_EQ(Rotator(screen, Direction::RightToLeft).Rotate(screen),
            Size(1080, 1920));
}

TEST(RotatorTest, RotateRect) {
  Size screen(1920, 1080);

  {
    Rotator TopBottom(screen, Direction::TopToBottom);
    auto result = TopBottom.Rotate(Rect::GRP(100, 100, 720, 680));
    EXPECT_EQ(result, Rect::GRP(100, 100, 720, 680));
  }

  {
    Rotator BottomTop(screen, Direction::BottomToTop);
    auto result = BottomTop.Rotate(Rect::GRP(100, 100, 720, 680));
    EXPECT_EQ(result, Rect::GRP(1200, 400, 1820, 980));
  }

  {
    Rotator LeftRight(screen, Direction::LeftToRight);
    auto result = LeftRight.Rotate(Rect::GRP(100, 100, 720, 680));
    EXPECT_EQ(result, Rect::GRP(100, 360, 680, 980));
  }

  {
    Rotator RightLeft(screen, Direction::RightToLeft);
    auto result = RightLeft.Rotate(Rect::GRP(100, 100, 720, 680));
    EXPECT_EQ(result, Rect::GRP(1240, 100, 1820, 720));
  }
}

TEST(DrawerTest, SlideOff) {
  const int amount_visible = 100;
  Size screen(1920, 1080);
  auto direction = Direction::LeftToRight;
  Composer drawer(screen, screen, screen, direction);

  auto result = drawer.Compose(NoneStrategy(), SlideStrategy(), amount_visible);
  EXPECT_EQ(result.ToString(),
            "src: (0,0,100,1080) -> (0,0,100,1080)\n"
            "dst: (0,0,1820,1080) -> (100,0,1920,1080)");
}

TEST(DrawerTest, SlideOn) {
  const int amount_visible = 100;
  Size screen(1920, 1080);
  auto direction = Direction::BottomToTop;
  Composer drawer(screen, screen, screen, direction);

  auto result = drawer.Compose(SlideStrategy(), NoneStrategy(), amount_visible);
  EXPECT_EQ(result.ToString(),
            "src: (0,0,1920,100) -> (0,980,1920,1080)\n"
            "dst: (0,0,1920,980) -> (0,0,1920,980)");
}

TEST(DrawerTest, SquashOnSquashOff) {
  const int amount_visible = 500;
  Size screen(1920, 1080);
  auto direction = Direction::TopToBottom;
  Composer drawer(screen, screen, screen, direction);

  auto result =
      drawer.Compose(SquashStrategy(), SquashStrategy(), amount_visible);
  EXPECT_EQ(result.ToString(),
            "src: (0,0,1920,1080) -> (0,0,1920,500)\n"
            "dst: (0,0,1920,1080) -> (0,500,1920,1080)");
}

TEST(DrawerTest, ScrollOnScrollOff) {
  const int amount_visible = 500;
  Size screen(1920, 1080);
  auto direction = Direction::RightToLeft;
  Composer drawer(screen, screen, screen, direction);

  auto result =
      drawer.Compose(ScrollStrategy(), ScrollStrategy(), amount_visible);
  EXPECT_EQ(result.ToString(),
            "src: (0,0,500,1080) -> (1420,0,1920,1080)\n"
            "dst: (500,0,1920,1080) -> (0,0,1420,1080)");
}
