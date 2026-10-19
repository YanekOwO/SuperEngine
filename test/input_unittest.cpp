// -----------------------------------------------------------------------
//
// This file is part of RLVM
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
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
// -----------------------------------------------------------------------

#include <gtest/gtest.h>

#include "core/input.hpp"
#include "systems/sdl/event_backend.hpp"

#include <SDL3/SDL.h>

#include <memory>

namespace {

template <typename T>
void Dispatch(InputListener& listener, T event) {
  listener.OnEvent(std::make_shared<Event>(event));
}

void ExpectNoEdges(const InputListener::State& state) {
  EXPECT_FALSE(state.on_down);
  EXPECT_FALSE(state.on_up);
  EXPECT_FALSE(state.down_up);
}

}  // namespace

TEST(InputListenerTest, DecideKeysStayDownAcrossFrames) {
  for (const auto code : {KeyCode::RETURN, KeyCode::SPACE, KeyCode::KP_ENTER}) {
    InputListener listener;

    Dispatch(listener, KeyDown{code});
    EXPECT_TRUE(listener.decide.down);
    EXPECT_TRUE(listener.decide.on_down);
    EXPECT_FALSE(listener.decide.on_up);
    EXPECT_FALSE(listener.decide.down_up);

    listener.ResetState();
    EXPECT_TRUE(listener.decide.down);
    ExpectNoEdges(listener.decide);

    Dispatch(listener, KeyUp{code});
    EXPECT_FALSE(listener.decide.down);
    EXPECT_FALSE(listener.decide.on_down);
    EXPECT_TRUE(listener.decide.on_up);
    EXPECT_TRUE(listener.decide.down_up);
  }
}

TEST(InputListenerTest, EscapeControlsCancelState) {
  InputListener listener;

  Dispatch(listener, KeyDown{KeyCode::ESCAPE});
  EXPECT_TRUE(listener.cancel.down);
  EXPECT_TRUE(listener.cancel.on_down);
  EXPECT_FALSE(listener.decide.down);

  listener.ResetState();
  Dispatch(listener, KeyUp{KeyCode::ESCAPE});
  EXPECT_FALSE(listener.cancel.down);
  EXPECT_TRUE(listener.cancel.on_up);
  EXPECT_TRUE(listener.cancel.down_up);
}

TEST(InputListenerTest, SameFrameCycleAccumulatesEdges) {
  InputListener listener;

  Dispatch(listener, KeyDown{KeyCode::RETURN});
  Dispatch(listener, KeyUp{KeyCode::RETURN});

  EXPECT_FALSE(listener.decide.down);
  EXPECT_TRUE(listener.decide.on_down);
  EXPECT_TRUE(listener.decide.on_up);
  EXPECT_TRUE(listener.decide.down_up);
}

TEST(InputListenerTest, RepeatedEventsDoNotCreateTransitions) {
  InputListener listener;

  Dispatch(listener, KeyUp{KeyCode::RETURN});
  ExpectNoEdges(listener.decide);

  Dispatch(listener, KeyDown{KeyCode::RETURN});
  listener.ResetState();
  Dispatch(listener, KeyDown{KeyCode::RETURN});
  EXPECT_TRUE(listener.decide.down);
  ExpectNoEdges(listener.decide);

  Dispatch(listener, KeyUp{KeyCode::RETURN});
  listener.ResetState();
  Dispatch(listener, KeyUp{KeyCode::RETURN});
  EXPECT_FALSE(listener.decide.down);
  ExpectNoEdges(listener.decide);
}

TEST(InputListenerTest, LogicalInputAggregatesKeyboardAndMouseSources) {
  InputListener listener;

  Dispatch(listener, KeyDown{KeyCode::RETURN});
  listener.ResetState();
  Dispatch(listener, MouseDown{MouseButton::LEFT});
  ExpectNoEdges(listener.decide);

  Dispatch(listener, KeyUp{KeyCode::RETURN});
  EXPECT_TRUE(listener.decide.down);
  ExpectNoEdges(listener.decide);

  Dispatch(listener, MouseUp{MouseButton::LEFT});
  EXPECT_FALSE(listener.decide.down);
  EXPECT_TRUE(listener.decide.on_up);
  EXPECT_TRUE(listener.decide.down_up);
}

TEST(InputListenerTest, MouseEventsUpdatePhysicalAndLogicalState) {
  InputListener listener;

  Dispatch(listener, MouseMotion{Point(23, 42)});
  EXPECT_EQ(listener.mouse_pos, Point(23, 42));

  Dispatch(listener, MouseDown{MouseButton::LEFT});
  EXPECT_TRUE(listener.mouse_left.down);
  EXPECT_TRUE(listener.decide.down);
  EXPECT_TRUE(listener.decide.on_down);

  Dispatch(listener, MouseDown{MouseButton::RIGHT});
  EXPECT_TRUE(listener.mouse_right.down);
  EXPECT_TRUE(listener.cancel.down);
  EXPECT_TRUE(listener.cancel.on_down);

  listener.ResetState();
  Dispatch(listener, MouseUp{MouseButton::LEFT});
  Dispatch(listener, MouseUp{MouseButton::RIGHT});
  EXPECT_FALSE(listener.mouse_left.down);
  EXPECT_FALSE(listener.mouse_right.down);
  EXPECT_TRUE(listener.decide.on_up);
  EXPECT_TRUE(listener.decide.down_up);
  EXPECT_TRUE(listener.cancel.on_up);
  EXPECT_TRUE(listener.cancel.down_up);
}

TEST(InputListenerTest, UnrelatedAndNullEventsAreIgnored) {
  InputListener listener;

  Dispatch(listener, KeyDown{KeyCode::a});
  Dispatch(listener, MouseDown{MouseButton::MIDDLE});
  listener.OnEvent(nullptr);

  EXPECT_FALSE(listener.decide.down);
  EXPECT_FALSE(listener.cancel.down);
  EXPECT_FALSE(listener.mouse_left.down);
  EXPECT_FALSE(listener.mouse_right.down);
  ExpectNoEdges(listener.decide);
  ExpectNoEdges(listener.cancel);
}

TEST(FromSDLKeycodeTest, MapsEngineConsumedKeys) {
  EXPECT_EQ(FromSDLKeycode(SDLK_RETURN), KeyCode::RETURN);
  EXPECT_EQ(FromSDLKeycode(SDLK_ESCAPE), KeyCode::ESCAPE);
  EXPECT_EQ(FromSDLKeycode(SDLK_SPACE), KeyCode::SPACE);
  EXPECT_EQ(FromSDLKeycode(SDLK_LCTRL), KeyCode::LCTRL);
  EXPECT_EQ(FromSDLKeycode(SDLK_RCTRL), KeyCode::RCTRL);
  EXPECT_EQ(FromSDLKeycode(SDLK_LSHIFT), KeyCode::LSHIFT);
  EXPECT_EQ(FromSDLKeycode(SDLK_RSHIFT), KeyCode::RSHIFT);
  EXPECT_EQ(FromSDLKeycode(SDLK_UP), KeyCode::UP);
  EXPECT_EQ(FromSDLKeycode(SDLK_DOWN), KeyCode::DOWN);
  EXPECT_EQ(FromSDLKeycode(SDLK_KP_ENTER), KeyCode::KP_ENTER);
}

TEST(FromSDLKeycodeTest, MapsPrintableAsciiByValue) {
  EXPECT_EQ(FromSDLKeycode(SDLK_A), KeyCode::a);
  EXPECT_EQ(FromSDLKeycode(SDLK_Z), KeyCode::z);
  EXPECT_EQ(FromSDLKeycode(SDLK_0), KeyCode::NUM0);
  EXPECT_EQ(FromSDLKeycode(SDLK_9), KeyCode::NUM9);
  EXPECT_EQ(FromSDLKeycode(SDLK_BACKSPACE), KeyCode::BACKSPACE);
  EXPECT_EQ(FromSDLKeycode(SDLK_TAB), KeyCode::TAB);
}

TEST(FromSDLKeycodeTest, ScancodeMaskedKeysDoNotPassThrough) {
  EXPECT_EQ(FromSDLKeycode(SDLK_F1), KeyCode::F1);
  EXPECT_EQ(FromSDLKeycode(SDLK_F15), KeyCode::F15);
  EXPECT_EQ(FromSDLKeycode(SDLK_LEFT), KeyCode::LEFT);
  EXPECT_EQ(FromSDLKeycode(SDLK_RIGHT), KeyCode::RIGHT);
  // An unmapped masked key must not leak SDL3 numeric values into KeyCode.
  EXPECT_EQ(FromSDLKeycode(SDLK_MEDIA_PLAY), KeyCode::UNKNOWN);
}

TEST(InputListenerTest, PhysicalButtonsAreIndependentOfKeyboard) {
  InputListener listener;
  Dispatch(listener, KeyDown{KeyCode::RETURN});
  Dispatch(listener, KeyDown{KeyCode::ESCAPE});
  EXPECT_FALSE(listener.mouse_left.down);
  EXPECT_FALSE(listener.mouse_right.down);
  listener.ResetState();
  Dispatch(listener, MouseDown{MouseButton::LEFT});
  Dispatch(listener, MouseDown{MouseButton::RIGHT});
  EXPECT_TRUE(listener.mouse_left.on_down);
  EXPECT_TRUE(listener.mouse_right.on_down);
  ExpectNoEdges(listener.decide);
  ExpectNoEdges(listener.cancel);
  listener.NextMouse();
  Dispatch(listener, MouseUp{MouseButton::LEFT});
  Dispatch(listener, MouseUp{MouseButton::RIGHT});
  EXPECT_TRUE(listener.mouse_left.down_up);
  EXPECT_TRUE(listener.mouse_right.down_up);
  EXPECT_TRUE(listener.decide.down);
  EXPECT_TRUE(listener.cancel.down);
}

TEST(InputListenerTest, MouseNextPreservesPendingClickAndIgnoresRepeats) {
  InputListener listener;
  Dispatch(listener, MouseUp{MouseButton::LEFT});
  ExpectNoEdges(listener.mouse_left);
  Dispatch(listener, MouseDown{MouseButton::LEFT});
  listener.ResetState();
  Dispatch(listener, MouseDown{MouseButton::LEFT});
  EXPECT_TRUE(listener.mouse_left.down);
  ExpectNoEdges(listener.mouse_left);
  Dispatch(listener, MouseUp{MouseButton::LEFT});
  EXPECT_TRUE(listener.mouse_left.on_up);
  EXPECT_TRUE(listener.mouse_left.down_up);
  listener.NextMouse();
  Dispatch(listener, MouseUp{MouseButton::LEFT});
  ExpectNoEdges(listener.mouse_left);
}

TEST(InputListenerTest, MouseClearCancelsPendingClickWithoutReleasing) {
  InputListener listener;
  Dispatch(listener, MouseMotion{Point(23, 42)});
  Dispatch(listener, MouseDown{MouseButton::LEFT});
  listener.ClearMouse();
  EXPECT_TRUE(listener.mouse_left.down);
  EXPECT_TRUE(listener.decide.on_down);
  EXPECT_EQ(listener.mouse_pos, Point(23, 42));
  ExpectNoEdges(listener.mouse_left);
  Dispatch(listener, MouseUp{MouseButton::LEFT});
  EXPECT_TRUE(listener.mouse_left.on_up);
  EXPECT_FALSE(listener.mouse_left.down_up);
  Dispatch(listener, MouseDown{MouseButton::LEFT});
  Dispatch(listener, MouseUp{MouseButton::LEFT});
  EXPECT_TRUE(listener.mouse_left.on_down);
  EXPECT_TRUE(listener.mouse_left.on_up);
  EXPECT_TRUE(listener.mouse_left.down_up);
}

TEST(InputListenerTest, WheelCountsDownEventsOnlyAndResetsWithMouse) {
  InputListener listener;
  for (auto button :
       {MouseButton::WHEELDOWN, MouseButton::WHEELDOWN, MouseButton::WHEELUP}) {
    Dispatch(listener, MouseDown{button});
    Dispatch(listener, MouseUp{button});
  }
  EXPECT_EQ(listener.mouse_wheel, 1);
  listener.NextMouse();
  EXPECT_EQ(listener.mouse_wheel, 0);
  Dispatch(listener, MouseDown{MouseButton::WHEELUP});
  EXPECT_EQ(listener.mouse_wheel, -1);
  listener.ClearMouse();
  EXPECT_EQ(listener.mouse_wheel, 0);
}

TEST(InputListenerTest, ClearAndReleaseAllResetPendingMouseState) {
  for (bool release : {false, true}) {
    InputListener listener;
    Dispatch(listener, MouseMotion{Point(23, 42)});
    Dispatch(listener, MouseDown{MouseButton::RIGHT});
    Dispatch(listener, MouseDown{MouseButton::WHEELDOWN});
    Dispatch(listener, KeyDown{KeyCode::RETURN});
    if (release)
      listener.ReleaseAll();
    else
      listener.ClearState();
    EXPECT_EQ(listener.mouse_pos, Point(23, 42));
    EXPECT_EQ(listener.mouse_wheel, 0);
    EXPECT_EQ(listener.mouse_right.down, !release);
    EXPECT_EQ(listener.decide.down, !release);
    ExpectNoEdges(listener.cancel);
    Dispatch(listener, MouseUp{MouseButton::RIGHT});
    EXPECT_FALSE(listener.mouse_right.down_up);
  }
}
