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

#include "core/input.hpp"

#include <gtest/gtest.h>

#include <memory>

class SiglusScriptInputTest : public ::testing::Test {
 protected:
  ScriptInput input;
  InputListener live;
  using Source = ScriptInput::Source;

  template <class T>
  void Send(T value) {
    auto event = std::make_shared<Event>(value);
    live.OnEvent(event);
    input.OnEvent(event);
  }

  void Click() {
    Send(MouseDown{MouseButton::LEFT});
    Send(MouseUp{MouseButton::LEFT});
  }
};

TEST_F(SiglusScriptInputTest, FastClickExposesPressBeforeCompletedClick) {
  // A complete click arrives in one poll; scripts must capture the pressed
  // target before receiving exactly one completed click on the next snapshot.
  Click();
  auto press = input.Get(Source::Decide);
  EXPECT_TRUE(press.down);
  EXPECT_TRUE(press.on_down);
  EXPECT_FALSE(press.down_up);

  input.Next();
  auto release = input.Get(Source::Decide);
  EXPECT_FALSE(release.down);
  EXPECT_TRUE(release.on_up);
  EXPECT_TRUE(release.down_up);

  input.Next();
  EXPECT_FALSE(input.Get(Source::Decide).down_up);
}

TEST_F(SiglusScriptInputTest, LiveFrameResetRetainsScriptRelease) {
  // A VM slice ends before reading a release; resetting native frame edges
  // must leave the completed click available to the suspended script.
  Send(MouseDown{MouseButton::LEFT});
  ASSERT_TRUE(input.Get(Source::Decide).down);
  input.Next();
  Send(MouseUp{MouseButton::LEFT});
  live.ResetState();

  EXPECT_TRUE(input.Get(Source::Decide).down_up);
}

TEST_F(SiglusScriptInputTest, ReadsKeepPointerAndButtonsStableUntilNext) {
  // Once a script has sampled input, new physical events cannot change its
  // hit test or held state halfway through the same logical iteration.
  Send(MouseMotion{Point(10, 20)});
  Send(MouseDown{MouseButton::LEFT});
  ASSERT_EQ(input.MousePosition(), Point(10, 20));
  Send(MouseMotion{Point(30, 40)});
  Send(MouseUp{MouseButton::LEFT});

  EXPECT_EQ(input.MousePosition(), Point(10, 20));
  EXPECT_TRUE(input.Get(Source::Left).down);
  input.Next();
  EXPECT_EQ(input.MousePosition(), Point(30, 40));
  EXPECT_TRUE(input.Get(Source::Left).down_up);
}

TEST_F(SiglusScriptInputTest, ExplicitNextAndFrameAdvanceConsumeOnlyOnce) {
  // Menus call next immediately before frame; that frame boundary must not
  // skip the queued release or erase its completion edge.
  Click();
  ASSERT_TRUE(input.Get(Source::Decide).down);
  input.Next();
  input.AdvanceFrame();

  EXPECT_TRUE(input.Get(Source::Decide).down_up);
}

TEST_F(SiglusScriptInputTest, PointerReadAlsoFreezesKeyboardSnapshot) {
  // A hit test begins before a physical key press; reading decide later in
  // the iteration must use the keyboard state from that same snapshot.
  input.MousePosition();
  Send(KeyDown{KeyCode::RETURN});
  EXPECT_FALSE(input.Get(Source::Decide).down);
  input.Next();

  EXPECT_TRUE(input.Get(Source::Decide).down);
}

TEST_F(SiglusScriptInputTest, FrameWithoutNextAdvancesInput) {
  // A script using frame alone still receives a fresh input snapshot after
  // the frame completes, including a previously queued release.
  Click();
  ASSERT_TRUE(input.Get(Source::Decide).down);
  input.AdvanceFrame();

  EXPECT_TRUE(input.Get(Source::Decide).down_up);
}

TEST_F(SiglusScriptInputTest, MouseNextPreservesKeyboardEdges) {
  // Mouse-only advancement must leave an already observed keyboard press
  // available to the logical decide input.
  Send(KeyDown{KeyCode::RETURN});
  ASSERT_TRUE(input.Get(Source::Decide).on_down);
  input.NextMouse();

  EXPECT_TRUE(input.Get(Source::Decide).on_down);
}

TEST_F(SiglusScriptInputTest, MouseClearCancelsPendingClickWhileRetainingHold) {
  // Clearing a pressed mouse button cancels its eventual click without
  // inventing a physical release or losing the pointer position.
  Send(MouseMotion{Point(12, 34)});
  Send(MouseDown{MouseButton::LEFT});
  ASSERT_TRUE(input.Get(Source::Left).down);
  input.ClearMouse();
  EXPECT_TRUE(input.Get(Source::Left).down);
  EXPECT_EQ(input.MousePosition(), Point(12, 34));
  Send(MouseUp{MouseButton::LEFT});
  input.NextMouse();

  auto release = input.Get(Source::Left);
  EXPECT_TRUE(release.on_up);
  EXPECT_FALSE(release.down_up);
}

TEST_F(SiglusScriptInputTest, MouseClearPreservesKeyboardState) {
  // A held decide key remains pressed with its edge after mouse input clears.
  Send(KeyDown{KeyCode::SPACE});
  ASSERT_TRUE(input.Get(Source::Decide).on_down);
  input.ClearMouse();

  EXPECT_TRUE(input.Get(Source::Decide).down);
  EXPECT_TRUE(input.Get(Source::Decide).on_down);
}

TEST_F(SiglusScriptInputTest,
       ClearDiscardsBufferedClickAndUsesCurrentPosition) {
  // Clearing input before queued events are read discards the click while
  // retaining the latest physical position.
  Click();
  Send(MouseMotion{Point(50, 60)});
  input.Clear();

  EXPECT_EQ(input.MousePosition(), Point(50, 60));
  EXPECT_FALSE(input.Get(Source::Decide).down);
  EXPECT_FALSE(input.Get(Source::Decide).on_down);
  EXPECT_FALSE(input.Get(Source::Decide).down_up);
}

TEST_F(SiglusScriptInputTest, HeldKeyboardDoesNotHideMouseCompletion) {
  // A mouse click completes while a decide key remains held; the aggregate
  // input must expose the click even though its held state stays true.
  Send(KeyDown{KeyCode::KP_ENTER});
  Click();
  ASSERT_TRUE(input.Get(Source::Decide).down);
  input.Next();

  auto state = input.Get(Source::Decide);
  EXPECT_TRUE(state.down);
  EXPECT_TRUE(state.down_up);
}

TEST_F(SiglusScriptInputTest, KeyboardRepeatDoesNotCreateExtraClicks) {
  // Repeated physical keydown events represent one held press, so only its
  // matching release can produce a completed click.
  Send(KeyDown{KeyCode::RETURN});
  Send(KeyDown{KeyCode::RETURN});
  Send(KeyUp{KeyCode::RETURN});
  ASSERT_TRUE(input.Get(Source::Decide).on_down);
  input.Next();
  EXPECT_TRUE(input.Get(Source::Decide).down_up);
  input.Next();
  EXPECT_FALSE(input.Get(Source::Decide).on_down);
  EXPECT_FALSE(input.Get(Source::Decide).down_up);
}

TEST_F(SiglusScriptInputTest, RapidClicksEachProduceOneCompletion) {
  // Two fast clicks are buffered before script execution; each must retain
  // its own pressed snapshot and produce exactly one completion.
  Click();
  Click();
  for (int i = 0; i < 2; ++i) {
    EXPECT_TRUE(input.Get(Source::Decide).on_down);
    input.Next();
    EXPECT_TRUE(input.Get(Source::Decide).down_up);
    input.Next();
  }
  EXPECT_FALSE(input.Get(Source::Decide).down_up);
}

TEST_F(SiglusScriptInputTest, RightClickUsesCancelInput) {
  // A fast right click must have the same ordered press/release semantics
  // through cancel while leaving decide inactive.
  Send(MouseDown{MouseButton::RIGHT});
  Send(MouseUp{MouseButton::RIGHT});
  EXPECT_TRUE(input.Get(Source::Cancel).down);
  EXPECT_FALSE(input.Get(Source::Decide).down);
  input.Next();

  EXPECT_TRUE(input.Get(Source::Cancel).down_up);
}

TEST_F(SiglusScriptInputTest, MouseWheelAccumulatesUntilMouseNext) {
  // Wheel events keep their existing direction and accumulate in one script
  // snapshot; advancing mouse input clears that count.
  Send(MouseDown{MouseButton::WHEELUP});
  Send(MouseDown{MouseButton::WHEELUP});
  Send(MouseDown{MouseButton::WHEELDOWN});
  EXPECT_EQ(input.MouseWheel(), -1);
  input.NextMouse();

  EXPECT_EQ(input.MouseWheel(), 0);
}

TEST_F(SiglusScriptInputTest, ReleaseAllDiscardsHeldAndQueuedInput) {
  // Ownership transfer discards a held key and queued click while retaining
  // the latest pointer position for the newly active native control.
  Send(KeyDown{KeyCode::RETURN});
  Click();
  Send(MouseMotion{Point(70, 80)});
  input.ReleaseAll();

  EXPECT_EQ(input.MousePosition(), Point(70, 80));
  EXPECT_FALSE(input.Get(Source::Decide).down);
  EXPECT_FALSE(input.Get(Source::Decide).down_up);
}

TEST_F(SiglusScriptInputTest, ScriptNextDoesNotResetNativeEdges) {
  // Native controls share the physical events, but script advancement must
  // not consume the completion edge used by native controls in that frame.
  Click();
  input.Next();

  EXPECT_TRUE(live.mouse_left.down_up);
}
