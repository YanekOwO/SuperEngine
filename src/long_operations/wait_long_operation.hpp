// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2009 Elliot Glaysher
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

#pragma once

#include <functional>

#include "long_operations/long_operation.hpp"
#include "machine/reference.hpp"

class Point;

class RLMachine;

namespace lop {

// Pauses interpretation, waiting for input, an event or a user click.
class WaitLongOperation : public LongOperation {
 public:
  explicit WaitLongOperation(RLMachine& machine);
  virtual ~WaitLongOperation();

  // This instance should wait time milliseconds and then return.
  void WaitMilliseconds(unsigned int time);

  // Whether we should exit this long operation on a click.
  void BreakOnClicks();

  // Checks |function| on every LongOperation invocation to see if we are
  // finished. |function| should return true if we are done.
  void BreakOnEvent(const std::function<bool()>& function);

  // Whether we write out the location of a mouse click. Implies that we're
  // breaking on mouse click.
  void SaveClickLocation(IntReferenceIterator x, IntReferenceIterator y);

  // Overridden from EventListener:
  void OnEvent(std::shared_ptr<Event> event) override;

  void RecordMouseCursorPosition();

  // Overridden from LongOperation:
  Task<int> Run(WaitContext& waits) override;

 private:
  void OnMouseMotion(const Point&);
  bool OnMouseButtonStateChanged(MouseButton mouseButton, bool pressed);
  bool OnKeyStateChanged(KeyCode keyCode, bool pressed);

  RLMachine& machine_;

  bool wait_until_target_time_;
  unsigned int target_time_;

  bool break_on_clicks_;
  int button_pressed_;

  bool break_on_event_;
  std::function<bool()> event_function_;

  bool break_on_ctrl_pressed_;
  bool ctrl_pressed_;

  bool mouse_moved_;

  bool save_click_location_;
  IntReferenceIterator x_;
  IntReferenceIterator y_;
};

}  // namespace lop
