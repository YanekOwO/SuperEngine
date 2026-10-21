// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2007 Elliot Glaysher
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

#include "long_operations/pause_long_operation.hpp"

#include "core/rlevent_listener.hpp"
#include "machine/rlmachine.hpp"
#include "systems/event_system.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"
#include "systems/text_page.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"

namespace lop {

// -----------------------------------------------------------------------
// PauseLongOperation
// -----------------------------------------------------------------------

PauseLongOperation::PauseLongOperation(RLMachine& machine)
    : LongOperation(), machine_(machine), is_done_(false) {
  TextSystem& text = machine_.GetSystem().text();
  EventSystem& event = machine_.GetSystem().event();

  // Initialize Auto Mode (in case it's activated, or in case it gets
  // activated)
  int numChars = text.GetCurrentPage().number_of_chars_on_page();
  automode_time_ = EffectiveAutoTime(machine_.GetLocalConfig(),
                                     machine_.GetGlobalConfig(), numChars);
  time_at_last_pass_ = event.GetTicks();
  total_time_ = 0;

  // We undo this in the destructor
  text.set_in_pause_state(true);
}

PauseLongOperation::~PauseLongOperation() {
  machine_.GetSystem().text().set_in_pause_state(false);
}

void PauseLongOperation::OnEvent(std::shared_ptr<Event> event) {
  const bool result = std::visit(
      [&](const auto& event) -> bool {
        using T = std::decay_t<decltype(event)>;

        if constexpr (std::same_as<T, MouseMotion>)
          this->OnMouseMotion(event.pos);

        if constexpr (std::same_as<T, MouseDown> || std::same_as<T, MouseUp>)
          return this->OnMouseButtonStateChanged(event.button,
                                                 std::same_as<T, MouseDown>);

        if constexpr (std::same_as<T, KeyDown> || std::same_as<T, KeyUp>)
          return this->OnKeyStateChanged(event.code, std::same_as<T, KeyDown>);

        return false;
      },
      *event);

  if (result)
    *event = std::monostate();
}

void PauseLongOperation::OnMouseMotion(const Point& p) {
  // Tell the text system about the move
  machine_.GetSystem().text().SetMousePosition(p);
}

bool PauseLongOperation::OnMouseButtonStateChanged(MouseButton mouseButton,
                                                   bool pressed) {
  TextSystem& text = machine_.GetSystem().text();

  switch (mouseButton) {
    case MouseButton::LEFT: {
      Point pos = machine_.GetSystem().rlEvent().GetCursorPos();
      // Only unhide the interface on release of the left mouse button
      if (machine_.is_interface_hidden()) {
        if (!pressed) {
          machine_.ToggleInterfaceHidden();
          return true;
        }
      } else if (!text.HandleMouseClick(pos, pressed)) {
        // We *must* only respond on mouseups! This detail matters because in
        // rlBabel, if glosses are enabled, an spause() is called and then the
        // mouse button value returned by GetCursorPos needs to be "2" for the
        // rest of the gloss implementation to work. If we respond on a
        // mousedown, then it'll return "1" instead.
        if (!pressed) {
          if (text.IsReadingBacklog()) {
            // Move back to the main page.
            text.StopReadingBacklog();
          } else {
            is_done_ = true;
          }

          return true;
        }
      }
      break;
    }
    case MouseButton::RIGHT:
      if (!pressed) {
        machine_.GetSystem().ShowSyscomMenu(machine_);
        return true;
      }
      break;
    case MouseButton::WHEELUP:
      if (pressed) {
        text.BackPage();
        return true;
      }
      break;
    case MouseButton::WHEELDOWN:
      if (pressed) {
        text.ForwardPage();
        return true;
      }
      break;
    default:
      break;
  }

  return false;
}

bool PauseLongOperation::OnKeyStateChanged(KeyCode keyCode, bool pressed) {
  bool handled = false;

  if (pressed) {
    if (machine_.is_interface_hidden()) {
      machine_.ToggleInterfaceHidden();
      handled = true;
    } else {
      TextSystem& text = machine_.GetSystem().text();
      bool ctrl_key_skips = !machine_.GetLocalConfig().disable_ctrl;

      if (ctrl_key_skips &&
          (keyCode == KeyCode::RCTRL || keyCode == KeyCode::LCTRL)) {
        is_done_ = true;
        handled = true;
      } else if (keyCode == KeyCode::SPACE) {
        machine_.ToggleInterfaceHidden();
        handled = true;
      } else if (keyCode == KeyCode::UP) {
        text.BackPage();
        handled = true;
      } else if (keyCode == KeyCode::DOWN) {
        text.ForwardPage();
        handled = true;
      } else if (keyCode == KeyCode::RETURN) {
        if (text.IsReadingBacklog())
          text.StopReadingBacklog();
        else
          is_done_ = true;

        handled = true;
      }
    }
  }

  return handled;
}

Task<int> PauseLongOperation::Run(WaitContext& waits) {
  while (!is_done_) {
    if (EffectiveAutoMode(machine_.GetLocalConfig(),
                          machine_.GetGlobalConfig()) &&
        AutomodeTimerFired() && !machine_.GetSystem().sound().KoePlaying())
      is_done_ = true;
    if (machine_.GetSystem().ShouldFastForward())
      is_done_ = true;
    if (!is_done_)
      co_await waits.Schedule();
  }
  machine_.GetSystem().sound().KoeStop();
  co_return 0;
}

bool PauseLongOperation::AutomodeTimerFired() {
  int current_time = machine_.GetSystem().event().GetTicks();
  int time_since_last_pass = current_time - time_at_last_pass_;
  time_at_last_pass_ = current_time;

  if (machine_.GetSystem().rlEvent().TimeOfLastMouseMove() <
      (current_time - 2000)) {
    // If the mouse has been moved within the last two seconds, don't advance
    // the timer so the user has a chance to click on buttons.
    total_time_ += time_since_last_pass;
    return total_time_ >= automode_time_;
  }

  return false;
}

// -----------------------------------------------------------------------
// NewPageAfterLongop
// -----------------------------------------------------------------------
NewPageAfterLongop::NewPageAfterLongop(RLMachine& machine, LongOperation* inOp)
    : PerformAfterLongOperationDecorator(machine, inOp) {}

NewPageAfterLongop::~NewPageAfterLongop() {}

void NewPageAfterLongop::PerformAfterLongOperation(RLMachine& machine) {
  TextSystem& text = machine.GetSystem().text();
  text.Snapshot();
  text.GetCurrentMwnd()->ClearWin();
  text.NewPageOnWindow(text.active_window());
}

// -----------------------------------------------------------------------
// NewPageOnAllAfterLongop
// -----------------------------------------------------------------------
NewPageOnAllAfterLongop::NewPageOnAllAfterLongop(RLMachine& machine,
                                                 LongOperation* inOp)
    : PerformAfterLongOperationDecorator(machine, inOp) {}

NewPageOnAllAfterLongop::~NewPageOnAllAfterLongop() {}

void NewPageOnAllAfterLongop::PerformAfterLongOperation(RLMachine& machine) {
  TextSystem& text = machine.GetSystem().text();
  text.Snapshot();
  for (int window : text.GetActiveWindows()) {
    text.GetMwnd(window)->ClearWin();
    text.NewPageOnWindow(window);
  }
}

// -----------------------------------------------------------------------
// NewParagraphAfterLongop
// -----------------------------------------------------------------------
NewParagraphAfterLongop::NewParagraphAfterLongop(RLMachine& machine,
                                                 LongOperation* inOp)
    : PerformAfterLongOperationDecorator(machine, inOp) {}

NewParagraphAfterLongop::~NewParagraphAfterLongop() {}

void NewParagraphAfterLongop::PerformAfterLongOperation(RLMachine& machine) {
  TextPage& page = machine.GetSystem().text().GetCurrentPage();
  page.ResetIndentation();
  page.HardBrake();
}

}  // namespace lop
