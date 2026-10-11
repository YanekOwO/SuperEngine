// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
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
//
// -----------------------------------------------------------------------

#include "systems/text_window_button.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/text_waku_normal.hpp"

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

std::unique_ptr<TextWindowButton> MakeButton(std::shared_ptr<Clock> clock,
                                             int& pressed,
                                             int& decided) {
  auto button = std::make_unique<TextWindowButton>(
      std::move(clock), true, Rect(Point(10, 10), Size(20, 20)));
  button->on_pressed_ = [&pressed] { ++pressed; };
  button->on_release_ = [&decided] { ++decided; };
  return button;
}

std::shared_ptr<SDLSurface> MakeAlphaSurface(const Size& size,
                                             std::vector<GrpRect> patterns) {
  SDL_Surface* raw = SDL_CreateSurface(size.width(), size.height(),
                                       SDL_PIXELFORMAT_ARGB8888);
  if (!raw)
    throw std::runtime_error(SDL_GetError());
  auto surface = std::make_shared<SDLSurface>(raw, std::move(patterns));
  surface->Fill(RGBAColour(255, 255, 255, 0));
  return surface;
}

std::unique_ptr<TextWindowButton> MakeAlphaButton(
    std::shared_ptr<Clock> clock,
    Rect button_rect,
    std::shared_ptr<SDLSurface> surface,
    int& pressed,
    int& decided) {
  auto button =
      std::make_unique<TextWindowButton>(std::move(clock), true, button_rect);
  button->SetUsePatternGeometry(true);
  button->SetAlphaHitTest(true);
  button->SetSurface(std::move(surface), 0);
  button->SetActionTableEntry(ButtonActionTable::Entry{}, 0);
  button->on_pressed_ = [&pressed] { ++pressed; };
  button->on_release_ = [&decided] { ++decided; };
  return button;
}

}  // namespace

TEST(TextWindowButtonTest, ReleaseWithoutCaptureDoesNotDecide) {
  int pressed = 0, decided = 0;
  auto button = MakeButton(std::make_shared<Clock>(), pressed, decided);

  EXPECT_FALSE(button->HandleMouseClick(Point(15, 15), false));
  EXPECT_EQ(pressed, 0);
  EXPECT_EQ(decided, 0);
}

TEST(TextWindowButtonTest, DragOutsideRetainsCaptureAndCancelsDecision) {
  int pressed = 0, decided = 0;
  auto button = MakeButton(std::make_shared<Clock>(), pressed, decided);

  EXPECT_TRUE(button->HandleMouseClick(Point(15, 15), true));
  button->SetMousePosition(Point(40, 40));
  EXPECT_TRUE(button->IsCaptured());
  EXPECT_EQ(button->state_, TextWindowButtonState::Normal);
  EXPECT_TRUE(button->HandleMouseClick(Point(40, 40), false));
  EXPECT_EQ(pressed, 1);
  EXPECT_EQ(decided, 0);
}

TEST(TextWindowButtonTest, DisabledAndTouchDisabledButtonsStillBlock) {
  int pressed = 0, decided = 0;
  auto button = MakeButton(std::make_shared<Clock>(), pressed, decided);

  button->SetPresentation(true, false, true, false);
  EXPECT_TRUE(button->BlocksInput());
  EXPECT_EQ(button->state_, TextWindowButtonState::Disabled);
  EXPECT_TRUE(button->HandleMouseClick(Point(15, 15), true));
  EXPECT_TRUE(button->HandleMouseClick(Point(15, 15), false));

  button->SetPresentation(true, true, false, false);
  EXPECT_TRUE(button->BlocksInput());
  EXPECT_EQ(button->state_, TextWindowButtonState::Normal);
  EXPECT_TRUE(button->HandleMouseClick(Point(15, 15), true));
  EXPECT_TRUE(button->HandleMouseClick(Point(15, 15), false));
  EXPECT_EQ(pressed, 0);
  EXPECT_EQ(decided, 0);
}

TEST(TextWakuNormalTest, LaterOverlappingButtonWins) {
  auto clock = std::make_shared<Clock>();
  int first_pressed = 0, first_decided = 0;
  int second_pressed = 0, second_decided = 0;
  TextWakuNormal waku;
  waku.AddButton("first", MakeButton(clock, first_pressed, first_decided));
  waku.AddButton("second", MakeButton(clock, second_pressed, second_decided));

  waku.SetMousePosition(Point(15, 15));
  EXPECT_TRUE(waku.HandleMouseClick(Point(15, 15), true));
  EXPECT_TRUE(waku.HandleMouseClick(Point(15, 15), false));
  EXPECT_EQ(first_pressed, 0);
  EXPECT_EQ(first_decided, 0);
  EXPECT_EQ(second_pressed, 1);
  EXPECT_EQ(second_decided, 1);
}

TEST(TextWindowButtonTest, AlphaHitTestRejectsTransparentCanvasPixels) {
  std::vector<GrpRect> patterns(1);
  patterns[0].rect = Rect::REC(0, 0, 100, 100);
  auto surface = MakeAlphaSurface(Size(100, 100), std::move(patterns));
  surface->Fill(RGBAColour(255, 255, 255, 1), Rect::REC(79, 79, 1, 1));
  surface->Fill(RGBAColour(255, 255, 255, 255), Rect::REC(80, 80, 10, 10));

  TextWindowButton button(std::make_shared<Clock>(), true,
                          Rect::REC(0, 0, 100, 100));
  button.SetUsePatternGeometry(true);
  button.SetAlphaHitTest(true);
  button.SetSurface(surface, 0);

  EXPECT_FALSE(button.HitTest(Point(10, 10)));
  EXPECT_TRUE(button.HitTest(Point(79, 79)));
  EXPECT_TRUE(button.HitTest(Point(80, 80)));
  EXPECT_FALSE(button.HitTest(Point(90, 90)));

  button.SetAlphaHitTest(false);
  EXPECT_TRUE(button.HitTest(Point(10, 10)));
}

TEST(TextWindowButtonTest, AlphaHitTestUsesCurrentPatternAndLogicalOrigin) {
  std::vector<GrpRect> patterns(5);
  for (int i = 0; i < 5; ++i) {
    patterns[i].rect = Rect::REC(0, i * 10, 10, 10);
    patterns[i].originX = 2;
    patterns[i].originY = 3;
  }
  auto surface = MakeAlphaSurface(Size(10, 50), std::move(patterns));
  for (int i = 0; i < 5; ++i) {
    surface->Fill(RGBAColour(255, 255, 255, 255),
                  Rect::REC(i + 1, i * 10 + i + 1, 1, 1));
  }

  ButtonActionTable::Entry action;
  action.normal.pattern = 0;
  action.hit.pattern = 1;
  action.push.pattern = 2;
  action.push.rep_pos = Point(4, 5);
  action.select.pattern = 3;
  action.disable.pattern = 4;

  TextWindowButton button(std::make_shared<Clock>(), true,
                          Rect::REC(20, 30, 10, 10));
  button.SetUsePatternGeometry(true);
  button.SetAlphaHitTest(true);
  button.SetSurface(surface, 0);
  button.SetActionTableEntry(action, 0);

  EXPECT_TRUE(button.HitTest(Point(19, 28)));
  EXPECT_FALSE(button.HitTest(Point(20, 29)));

  button.SetHovered(true);
  EXPECT_FALSE(button.HitTest(Point(19, 28)));
  EXPECT_TRUE(button.HitTest(Point(20, 29)));

  EXPECT_TRUE(button.HandleMouseClick(Point(20, 29), true));
  EXPECT_TRUE(button.HitTest(Point(21, 30)));
  EXPECT_FALSE(button.HitTest(Point(25, 35)));
  EXPECT_EQ(button.Render().destination, Point(22, 32));

  EXPECT_TRUE(button.HandleMouseClick(Point(21, 30), false));
  button.SetHovered(false);
  button.SetPresentation(true, true, true, true);
  EXPECT_TRUE(button.HitTest(Point(22, 31)));

  button.SetPresentation(true, false, true, false);
  EXPECT_TRUE(button.HitTest(Point(23, 32)));
}

TEST(TextWindowButtonTest, TransparentDragCancelsCapturedButton) {
  std::vector<GrpRect> patterns(1);
  patterns[0].rect = Rect::REC(0, 0, 20, 20);
  auto surface = MakeAlphaSurface(Size(20, 20), std::move(patterns));
  surface->Fill(RGBAColour(255, 255, 255, 255), Rect::REC(5, 5, 5, 5));

  int pressed = 0, decided = 0;
  auto button =
      MakeAlphaButton(std::make_shared<Clock>(), Rect::REC(10, 10, 20, 20),
                      surface, pressed, decided);

  EXPECT_TRUE(button->HandleMouseClick(Point(16, 16), true));
  button->SetMousePosition(Point(11, 11));
  EXPECT_TRUE(button->IsCaptured());
  EXPECT_EQ(button->state_, TextWindowButtonState::Normal);
  EXPECT_TRUE(button->HandleMouseClick(Point(11, 11), false));
  EXPECT_EQ(pressed, 1);
  EXPECT_EQ(decided, 0);
}

TEST(TextWakuNormalTest, TransparentTopButtonDoesNotOccludeOpaqueButton) {
  std::vector<GrpRect> patterns(1);
  patterns[0].rect = Rect::REC(0, 0, 20, 20);
  auto first_surface = MakeAlphaSurface(Size(20, 20), patterns);
  auto second_surface = MakeAlphaSurface(Size(20, 20), std::move(patterns));
  first_surface->Fill(RGBAColour(255, 255, 255, 255), Rect::REC(5, 5, 5, 5));
  second_surface->Fill(RGBAColour(255, 255, 255, 255), Rect::REC(15, 15, 5, 5));

  auto clock = std::make_shared<Clock>();
  int first_pressed = 0, first_decided = 0;
  int second_pressed = 0, second_decided = 0;
  TextWakuNormal waku;
  waku.AddButton(
      "first", MakeAlphaButton(clock, Rect::REC(10, 10, 20, 20), first_surface,
                               first_pressed, first_decided));
  waku.AddButton("second", MakeAlphaButton(clock, Rect::REC(10, 10, 20, 20),
                                           second_surface, second_pressed,
                                           second_decided));

  EXPECT_FALSE(waku.HasButtonAt(Point(11, 11)));
  EXPECT_TRUE(waku.HasButtonAt(Point(16, 16)));
  waku.SetMousePosition(Point(16, 16));
  EXPECT_TRUE(waku.HandleMouseClick(Point(16, 16), true));
  EXPECT_TRUE(waku.HandleMouseClick(Point(16, 16), false));
  EXPECT_EQ(first_pressed, 1);
  EXPECT_EQ(first_decided, 1);
  EXPECT_EQ(second_pressed, 0);
  EXPECT_EQ(second_decided, 0);
}
