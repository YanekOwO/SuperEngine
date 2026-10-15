// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
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

#include "long_operations/textout_long_operation.hpp"

#include "long_operations/pause_long_operation.hpp"
#include "machine/rlmachine.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"
#include "systems/system_error.hpp"
#include "systems/text_page.hpp"
#include "systems/text_system.hpp"
#include "utilities/exception.hpp"

#include <utf8.h>

#include <algorithm>
#include <string>

// Timing information must stay the same between individual
// TextoutLongOperations. rlBabel compiled games will always display one
// character per TextoutLongOperation.
unsigned int TextoutLongOperation::time_at_last_pass_ = 0;
int TextoutLongOperation::next_character_countdown_ = 0;

// -----------------------------------------------------------------------
// TextoutLongOperation
// -----------------------------------------------------------------------

TextoutLongOperation::TextoutLongOperation(RLMachine& machine,
                                           const std::string& utf8string)
    : utf8_string_(utf8string),
      current_codepoint_(0),
      current_position_(utf8_string_.begin()),
      no_wait_(false) {
  // Retrieve the first character (prime the loop in operator())
  string::iterator tmp = current_position_;
  if (tmp == utf8_string_.end()) {
    current_char_ = "";
  } else {
    current_codepoint_ = utf8::next(tmp, utf8_string_.end());
    current_char_ = string(current_position_, tmp);
    current_position_ = tmp;
  }

  // If we are inside a ruby gloss right now, don't delay at
  // all. Render the entire gloss!
  if (machine.GetSystem().text().GetCurrentPage().in_ruby_gloss())
    no_wait_ = true;
}

TextoutLongOperation::~TextoutLongOperation() {}

void TextoutLongOperation::OnEvent(std::shared_ptr<Event> event) {
  const bool result = std::visit(
      [&](const auto& event) -> bool {
        using T = std::decay_t<decltype(event)>;

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

bool TextoutLongOperation::OnMouseButtonStateChanged(MouseButton mouseButton,
                                                     bool pressed) {
  if (!pressed && mouseButton == MouseButton::LEFT) {
    no_wait_ = true;
    return true;
  }

  return false;
}

bool TextoutLongOperation::OnKeyStateChanged(KeyCode keyCode, bool pressed) {
  if (pressed && (keyCode == KeyCode::LCTRL || keyCode == KeyCode::RCTRL)) {
    no_wait_ = true;
    return true;
  }

  return false;
}

bool TextoutLongOperation::DisplayAsMuchAsWeCanThenPause(RLMachine& machine) {
  bool paused = false;
  while (!DisplayOneMoreCharacter(machine, paused))
    if (paused)
      return false;

  return true;
}

bool TextoutLongOperation::DisplayName(RLMachine& machine) {
  // TODO(erg): Right now, this doesn't deal with \#\#\#PRINT() syntax in the
  // name, even though character names are one of the places where that's
  // evaluated.

  // Ignore the starting bracket
  string::iterator it = current_position_;
  string::iterator curend = it;
  string::iterator strend = utf8_string_.end();
  int codepoint = utf8::next(it, strend);

  // Eat all characters between the name brackets
  while (codepoint != 0x3011 && it != strend) {
    curend = it;
    codepoint = utf8::next(it, strend);
  }

  if (codepoint != 0x3011 && it == strend) {
    throw SystemError(
        "Malformed string code. Opening bracket in \\{name}"
        " construct,  but missing closing bracket.");
  }

  // Grab the name
  std::string name(current_position_, curend);

  // Consume the next character
  current_position_ = it;

  if (it != strend) {
    current_codepoint_ = utf8::next(it, strend);
    current_char_ = string(current_position_, it);
    current_position_ = it;
  }

  TextSystem& text = machine.GetSystem().text();
  TextPage& page = text.GetCurrentPage();
  name = text.InterpretName(name);
  page.Name(name, current_char_);

  // Stop if this was the end of input
  return it == strend;
}

bool TextoutLongOperation::DisplayOneMoreCharacter(RLMachine& machine,
                                                   bool& paused) {
  if (current_codepoint_ == 0x3010) {
    // The current character is the opening character for a name. We
    // treat names as a single display operation
    return DisplayName(machine);
  } else {
    // Isolate the next character
    string::iterator next_position = current_position_;
    string::iterator strend = utf8_string_.end();

    if (next_position != strend) {
      int codepoint = utf8::next(next_position, strend);
      TextPage& page = machine.GetSystem().text().GetCurrentPage();
      if (codepoint) {
        string rest(current_position_, strend);
        bool rendered = page.Character(current_char_, rest);

        // Check to see if this character was rendered to the screen. If
        // this is false, then the page is probably full and the check
        // later on will do something about that.
        if (rendered) {
          current_char_ = std::string(current_position_, next_position);
          current_position_ = next_position;
        }
      } else {
        // advance to the next character if we've somehow hit an
        // embedded NULL that isn't the end of the string
        current_position_ = next_position;
      }

      // Call the pause operation if we've filled up the current page.
      if (page.IsFull()) {
        paused = true;
        machine.PushLongOperation(std::make_shared<NewPageAfterLongop>(
            new PauseLongOperation(machine)));
      }

      return false;
    } else {
      machine.GetSystem().text().GetCurrentPage().Character(current_char_, "");

      return true;
    }
  }
}

bool TextoutLongOperation::operator()(RLMachine& machine) {
  // Check to make sure we're not trying to do a textout (impossible!)
  if (!machine.GetSystem().text().system_visible())
    throw rlvm::Exception("Trying to Textout while TextSystem is hidden!");

  if (no_wait_) {
    return DisplayAsMuchAsWeCanThenPause(machine);
  } else {
    int current_time = machine.GetSystem().event().GetTicks();
    int time_since_last_pass = current_time - time_at_last_pass_;
    time_at_last_pass_ = current_time;

    next_character_countdown_ -= time_since_last_pass;
    if (next_character_countdown_ <= 0) {
      bool paused = false;
      next_character_countdown_ = EffectiveMessageSpeed(
          machine.GetLocalConfig(), machine.GetGlobalConfig());
      return DisplayOneMoreCharacter(machine, paused);
    } else {
      // Let's sleep a bit and then try again.
      return false;
    }
  }
}
