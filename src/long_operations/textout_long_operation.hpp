// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006 Elliot Glaysher
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

#pragma once

#include <string>

#include "long_operations/long_operation.hpp"

class RLMachine;

namespace lop {

class TextoutLongOperation : public LongOperation {
 public:
  TextoutLongOperation(RLMachine& machine, const std::string& utf8string);
  virtual ~TextoutLongOperation();

  void set_no_wait() { no_wait_ = true; }

  // Overriden from EventListener:
  void OnEvent(std::shared_ptr<Event> event) override;

  // Overriden from LongOperation:
  Task<int> Run(WaitContext& waits) override;

 private:
  RLMachine& machine_;

  bool OnMouseButtonStateChanged(MouseButton mouseButton, bool pressed);
  bool OnKeyStateChanged(KeyCode keyCode, bool pressed);

  bool DisplayAsMuchAsWeCanThenPause(RLMachine& machine);

  // Extract a name and send it to the text system as an automic
  // operation.
  bool DisplayName(RLMachine& machine);
  bool DisplayOneMoreCharacter(RLMachine& machine, bool& paused);

  std::string utf8_string_;

  int current_codepoint_;
  std::string current_char_;
  std::string::iterator current_position_;

  // Sets whether we should display as much text as we can immediately.
  bool no_wait_;

  // How long it's been since the last time we've added time to |total_time_|.
  static unsigned int time_at_last_pass_;

  // A countdown in milliseconds until we display the next character.
  static int next_character_countdown_;
};

}  // namespace lop
