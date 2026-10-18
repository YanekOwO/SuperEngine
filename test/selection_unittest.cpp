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

#include "core/selection.hpp"

#include "core/gameexe.hpp"
#include "libsiglus/bindings/selection.hpp"
#include "long_operations/reallive_task.hpp"
#include "long_operations/wait_for_selection.hpp"
#include "mock_clock.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>

using namespace std::chrono_literals;

TEST(SelectionConfigTest, RlNormalizedTemplate) {
  Gameexe gexe;
  gexe.parseLine("SELBTN.CNT = 1");
  gexe.parseLine("SELBTN.007.NAME = name");
  gexe.parseLine("SELBTN.007.BACK = back");
  gexe.parseLine("SELBTN.007.BASEPOS = 10,20");
  gexe.parseLine("SELBTN.007.REPPOS = -5,30");
  gexe.parseLine("SELBTN.007.CENTERING = 1,2");
  gexe.parseLine("SELBTN.007.MOJISIZE = 32");
  gexe.parseLine("SELBTN.007.MOJIDEFAULTCOL = 6");
  gexe.parseLine("SELBTN.007.MOJISELECTCOL = 9");
  gexe.parseLine("SELBTN.007.NORMAL = 2,3,4");
  gexe.parseLine("SELBTN.007.SELECT = 5,6,7");
  gexe.parseLine("SELBTN.007.PUSH = 8,9,10");
  gexe.parseLine("SELBTN.007.DONTSEL = 11,12,13");

  const auto c = BtnSelTemplate::ParseReallive(gexe, 7);
  EXPECT_EQ(c.format, BtnSelTemplate::Format::Reallive);
  EXPECT_EQ(c.base_file, "name");
  EXPECT_EQ(c.filter_file, "back");
  EXPECT_EQ(c.base_pos, Point(10, 20));
  EXPECT_EQ(c.rep_pos, Point(-5, 30));
  EXPECT_TRUE(c.center_x);
  EXPECT_TRUE(c.center_y);
  EXPECT_EQ(c.font_size, 32);
  EXPECT_EQ(c.color, 6);
  EXPECT_EQ(c.hit_color, 9);
  ASSERT_TRUE(c.frame_actions);
  EXPECT_EQ(c.frame_actions->normal,
            (ButtonActionTable::State{2, Point(3, 4)}));
  EXPECT_EQ(c.frame_actions->hit, (ButtonActionTable::State{5, Point(6, 7)}));
  EXPECT_EQ(c.frame_actions->push, (ButtonActionTable::State{8, Point(9, 10)}));
  EXPECT_EQ(c.frame_actions->disable,
            (ButtonActionTable::State{11, Point(12, 13)}));
  EXPECT_EQ(c.open_type, 0);
  EXPECT_EQ(c.decide_type, 0);
  EXPECT_EQ(c.close_type, 0);
}

TEST(SelectionConfigTest, RlMalformedOptionalVectorsAreSafe) {
  Gameexe gexe;
  gexe.parseLine("SELBTN.000.BASEPOS = 10");
  gexe.parseLine("SELBTN.000.NORMAL = 1,2");
  const auto c = BtnSelTemplate::ParseReallive(gexe, 0);
  EXPECT_EQ(c.base_pos, Point());
  EXPECT_EQ(c.rep_pos, Point());
  EXPECT_FALSE(c.center_x);
  EXPECT_FALSE(c.center_y);
  EXPECT_EQ(c.font_size, 25);
  EXPECT_EQ(c.color, 0);
  EXPECT_EQ(c.hit_color, 1);
  ASSERT_TRUE(c.frame_actions);
  EXPECT_EQ(*c.frame_actions, ButtonActionTable::Entry{});
}

TEST(SelectionConfigTest, ReallivePreservesClannadAndLittleBustersColors) {
  Gameexe gexe;
  gexe.parseLine("SELBTN.000.MOJIDEFAULTCOL = 6");
  gexe.parseLine("SELBTN.000.MOJISELECTCOL = 6");
  EXPECT_EQ(BtnSelTemplate::ParseReallive(gexe, 0).hit_color, 1);
  gexe.parseLine("SELBTN.001.MOJIDEFAULTCOL = 6");
  gexe.parseLine("SELBTN.001.MOJISELECTCOL = -1");
  EXPECT_EQ(BtnSelTemplate::ParseReallive(gexe, 1).hit_color, 6);
}

TEST(SelectionConfigTest, SgAnimationsAndTemplateValidation) {
  Gameexe gexe;
  gexe.parseLine("SELBTN.CNT = 1");
  gexe.parseLine("SELBTN.000.BASE_POS = 5,10");
  gexe.parseLine("SELBTN.000.MOJI_SIZE = 30,1,2,3");
  gexe.parseLine("SELBTN.000.OPEN_ANIME = 2,150");
  const auto c = BtnSelTemplate::ParseSiglus(gexe, 0);
  EXPECT_EQ(c.format, BtnSelTemplate::Format::Siglus);
  EXPECT_EQ(c.base_pos, Point(5, 10));
  EXPECT_EQ(c.font_size, 30);
  EXPECT_EQ(c.x_spacing, 1);
  EXPECT_EQ(c.y_spacing, 2);
  EXPECT_EQ(c.text_count, 3);
  EXPECT_EQ(c.open_type, 2);
  EXPECT_EQ(c.open_time, 150);
  EXPECT_EQ(c.decide_type, 1);
  EXPECT_EQ(c.close_type, 1);
  EXPECT_FALSE(c.frame_actions);
  EXPECT_THROW(BtnSelTemplate::ParseSiglus(gexe, 1), std::runtime_error);
  EXPECT_THROW(BtnSelTemplate::ParseSiglus(gexe, -1), std::runtime_error);
}

TEST(SelectionLayoutTest, CenteredButtonsSkipOffOptionsAndRetainOtherSlots) {
  Gameexe gexe;
  gexe.parseLine("SELBTN.000.CENTERING = 1,1");
  gexe.parseLine("SELBTN.000.BASEPOS = 999,999");
  gexe.parseLine("SELBTN.000.REPPOS = -10,30");
  Selection s(
      {.kind = SelectionRequest::Kind::Buttons,
       .options =
           {{"first"}, {"off", 0}, {"disabled", 2}, {"hidden", 3}, {"last"}}},
      BtnSelTemplate::ParseReallive(gexe, 0));
  s.LayoutButtons(Size(100, 20), Size(800, 600));
  EXPECT_EQ(s.origin, Point(400, 300));
  EXPECT_EQ(s.items[0].position, Point(-35, -55));
  EXPECT_EQ(s.items[1].hit_rect.size(), Size());
  EXPECT_EQ(s.items[2].position, Point(-45, -25));
  EXPECT_EQ(s.items[3].position, Point(-55, 5));
  EXPECT_EQ(s.items[4].position, Point(-65, 35));
  s.Start();
  s.open_hook_done = true;
  EXPECT_FALSE(s.Choose(1));
  EXPECT_FALSE(s.Choose(2));
  EXPECT_FALSE(s.Choose(3));
  EXPECT_TRUE(s.Choose(4));
  EXPECT_EQ(s.result, 4);
  EXPECT_TRUE(s.CanResume());
}

TEST(SelectionLayoutTest, WindowColumnsCountSlots) {
  Selection s(
      {.options = {
           {"off", 0}, {"disabled", 2}, {"first"}, {"hidden", 3}, {"last"}}});
  EXPECT_EQ(s.LayoutWindow(Size(200, 60), 20, 20, 20, false), Size(200, 60));
  EXPECT_EQ(s.items[1].position, Point(0, 20));
  EXPECT_EQ(s.items[2].hit_rect, Rect(0, 40, Size(100, 20)));
  EXPECT_EQ(s.items[3].position, Point(100, 20));
  EXPECT_EQ(s.items[4].hit_rect, Rect(100, 40, Size(100, 20)));
}

TEST(SelectionLayoutTest, SgBuildRetainsHeadlessLayout) {
  libsiglus::SiglusMachine machine;
  machine.gameexe = std::make_shared<Gameexe>();
  machine.gameexe->parseLine("SELBTN.000.BASE_POS = 30,40");
  machine.gameexe->parseLine("SELBTN.000.REP_POS = 0,50");
  auto s =
      libsiglus::binding::Build(machine,
                                {.kind = SelectionRequest::Kind::Buttons,
                                 .options = {{"off", 0}, {"choice", 1, 7}}},
                                nullptr);
  EXPECT_EQ(s->origin, Point(30, 40));
  EXPECT_EQ(s->items[1].hit_rect, Rect(0, 0, Size(100, 25)));
  EXPECT_EQ(s->request.options[1].color, 7);
  EXPECT_EQ(s->phase(), Selection::Phase::Prepared);
}

class SelectionInputTest : public ::testing::Test {
 protected:
  std::shared_ptr<SelState> state = std::make_shared<SelState>();
  std::shared_ptr<Selection> s;

  void Start(bool cancel = false, bool animate = false) {
    Gameexe gexe;
    auto config =
        animate ? BtnSelTemplate{} : BtnSelTemplate::ParseReallive(gexe, 0);
    config.rep_pos = Point(0, 30);
    config.open_time = config.decide_time = config.close_time = 10;
    s = std::make_shared<Selection>(
        SelectionRequest{.kind = SelectionRequest::Kind::Buttons,
                         .options = {{"off", 0},
                                     {"locked", 2},
                                     {"first", 1, 7},
                                     {"hidden", 3},
                                     {"last"}},
                         .cancel_enabled = cancel},
        config);
    s->LayoutButtons(Size(100, 20));
    s->Start();
    s->open_hook_done = !animate;
    state->active_ = s;
  }

  template <class T>
  SelState::InputResult Send(T value) {
    auto event = std::make_shared<Event>(value);
    auto feedback = state->HandleEvent(event);
    if constexpr (!std::is_same_v<T, MouseMotion>) {
      EXPECT_TRUE(std::holds_alternative<std::monostate>(*event));
    }
    return feedback;
  }
};

TEST_F(SelectionInputTest, KeyboardSkipsDisabledHiddenAndOffOptions) {
  Start();
  EXPECT_TRUE(Send(KeyDown{KeyCode::DOWN}).focus_changed);
  EXPECT_EQ(s->focused, 2);
  EXPECT_FALSE(Send(KeyDown{KeyCode::DOWN}).focus_changed);  // held repeat
  Send(KeyUp{KeyCode::DOWN});
  Send(KeyDown{KeyCode::DOWN});
  EXPECT_EQ(s->focused, 4);
  Send(KeyUp{KeyCode::DOWN});
  Send(KeyDown{KeyCode::DOWN});
  EXPECT_EQ(s->focused, 2);
  Send(KeyUp{KeyCode::DOWN});
  EXPECT_FALSE(Send(KeyUp{KeyCode::RETURN}).choice);  // no matching press
  Send(KeyDown{KeyCode::RETURN});
  EXPECT_EQ(Send(KeyUp{KeyCode::RETURN}).choice, 2);
  EXPECT_EQ(state->last_message_, "first");
  auto duplicate =
      state->HandleEvent(std::make_shared<Event>(KeyUp{KeyCode::RETURN}));
  EXPECT_FALSE(duplicate.choice);
}

TEST_F(SelectionInputTest, MouseRequiresPressAndRelease) {
  Start();
  Send(MouseMotion{Point(1, 1)});  // disabled slot
  EXPECT_FALSE(s->focused);
  Send(MouseDown{MouseButton::LEFT});
  EXPECT_FALSE(Send(MouseUp{MouseButton::LEFT}).choice);
  EXPECT_TRUE(Send(MouseMotion{Point(1, 31)}).focus_changed);
  EXPECT_EQ(s->focused, 2);
  Send(MouseDown{MouseButton::LEFT});
  Send(MouseMotion{Point(1, 91)});  // last option
  EXPECT_FALSE(Send(MouseUp{MouseButton::LEFT}).choice);
  EXPECT_FALSE(Send(MouseUp{MouseButton::LEFT}).choice);
  Send(MouseDown{MouseButton::LEFT});
  EXPECT_EQ(Send(MouseUp{MouseButton::LEFT}).choice, 4);
}

TEST_F(SelectionInputTest, OpeningCannotAcceptStaleReleases) {
  Start(false, true);
  Send(MouseMotion{Point(1, 31)});
  Send(MouseDown{MouseButton::LEFT});
  Send(KeyDown{KeyCode::RETURN});
  s->Advance(10);
  EXPECT_FALSE(s->CanInput());
  s->open_hook_done = true;
  s->focused = 2;
  EXPECT_FALSE(Send(MouseUp{MouseButton::LEFT}).choice);
  EXPECT_FALSE(Send(KeyUp{KeyCode::RETURN}).choice);
  Send(KeyDown{KeyCode::RETURN});
  EXPECT_EQ(Send(KeyUp{KeyCode::RETURN}).choice, 2);
  EXPECT_EQ(s->phase(), Selection::Phase::Deciding);
  EXPECT_FALSE(s->CanResume());
  s->request.sync_type = 2;
  EXPECT_TRUE(s->CanResume());
  s->request.sync_type = 1;
  EXPECT_FALSE(s->CanResume());
  s->Advance(10);
  EXPECT_EQ(s->phase(), Selection::Phase::Closing);
  EXPECT_TRUE(s->CanResume());
  s->request.sync_type = 0;
  EXPECT_FALSE(s->CanResume());
  s->Advance(10);
  EXPECT_TRUE(s->CanResume());
}

TEST_F(SelectionInputTest, CancellationNeedsPermissionAndPress) {
  Start(/*cancel=*/true);
  EXPECT_FALSE(Send(MouseUp{MouseButton::RIGHT}).choice);
  Send(MouseDown{MouseButton::RIGHT});
  EXPECT_EQ(Send(MouseUp{MouseButton::RIGHT}).choice, -1);
  EXPECT_EQ(state->last_message_, "（キャンセル）");

  Start(/*cancel=*/false);
  Send(KeyDown{KeyCode::ESCAPE});
  EXPECT_FALSE(Send(KeyUp{KeyCode::ESCAPE}).choice);
  EXPECT_FALSE(s->result);
  EXPECT_TRUE(s->CanInput());
}

TEST_F(SelectionInputTest, WaiterCompletesExactlyOnce) {
  Start();
  int finished = 0, canceled = 0, decisions = 0;
  auto clock = std::make_shared<MockClock>();
  {
    auto wait = std::make_shared<lop::WaitForSelection>(
        state, s,
        [&](auto result) {
          ++finished;
          EXPECT_EQ(result->result, 2);
        },
        [&](auto) { ++canceled; },
        [&](auto event) {
          if (state->HandleEvent(event).choice)
            ++decisions;
        });
    lop::RealliveTask task(wait, clock);
    task.Advance();
    task.OnEvent(std::make_shared<Event>(KeyDown{KeyCode::DOWN}));
    task.OnEvent(std::make_shared<Event>(KeyDown{KeyCode::RETURN}));
    task.OnEvent(std::make_shared<Event>(KeyUp{KeyCode::RETURN}));
    EXPECT_FALSE(task.Done());
    clock->AdvanceTime(5ms);
    task.Advance();
    EXPECT_TRUE(task.Done());
    task.Advance();
  }
  EXPECT_EQ(decisions, 1);
  EXPECT_EQ(finished, 1);
  EXPECT_EQ(canceled, 0);
}

TEST_F(SelectionInputTest, WaiterCancellationCleanup) {
  Start();
  int finished = 0, canceled = 0;
  {
    auto wait = std::make_shared<lop::WaitForSelection>(
        state, s, [&](auto) { ++finished; },
        [&](auto selection) {
          ++canceled;
          EXPECT_EQ(selection, s);
          if (state->active_ == selection)
            state->active_.reset();
        });
    lop::RealliveTask task(wait, std::make_shared<MockClock>());
    task.Advance();
    EXPECT_FALSE(task.Done());
  }
  EXPECT_EQ(finished, 0);
  EXPECT_EQ(canceled, 1);
  EXPECT_FALSE(state->active_);
}

TEST_F(SelectionInputTest, ReplacingAnUnfinishedSessionFailsItsWaiter) {
  Start();
  int finished = 0, canceled = 0;
  auto clock = std::make_shared<MockClock>();
  {
    auto wait = std::make_shared<lop::WaitForSelection>(
        state, s, [&](auto) { ++finished; }, [&](auto) { ++canceled; });
    lop::RealliveTask task(wait, clock);
    task.Advance();
    state->active_ = std::make_shared<Selection>(SelectionRequest{});
    clock->AdvanceTime(5ms);
    EXPECT_THROW(task.Advance(), std::runtime_error);
    EXPECT_TRUE(task.Done());
  }
  EXPECT_EQ(finished, 0);
  EXPECT_EQ(canceled, 1);
}
