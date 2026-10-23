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
#include "systems/system.hpp"
#include "systems/system_error.hpp"
#include "systems/text_page.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"
#include "utilities/exception.hpp"

#include <utf8.h>

#include <string>
#include <utility>

namespace lop {

// -----------------------------------------------------------------------
// TextoutLongOperation
// -----------------------------------------------------------------------

TextoutLongOperation::TextoutLongOperation(RLMachine& machine,
                                           const std::string& utf8string)
    : machine_(machine), utf8_string_(utf8string), no_wait_(false) {
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

  if (result) {
    flush_input_ = true;
    *event = std::monostate();
  }
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

bool TextoutLongOperation::PrepareNextPage(TextPage& page) {
  const auto end = utf8_string_.cend();
  while (position_ < utf8_string_.size()) {
    if (page.IsFull())
      return false;

    auto current = utf8_string_.cbegin() + position_;
    auto next = current;
    const int codepoint = utf8::next(next, end);
    if (codepoint == 0x3010) {
      const auto name_begin = next;
      auto name_end = next;
      bool closed = false;
      while (next != end) {
        name_end = next;
        if (utf8::next(next, end) == 0x3011) {
          closed = true;
          break;
        }
      }
      if (!closed)
        throw SystemError(
            "Malformed string code. Opening bracket in \\{name}"
            " construct,  but missing closing bracket.");

      auto peek = next;
      if (peek != end)
        utf8::next(peek, end);
      page.Name(machine_.name_mapping().Map(std::string(name_begin, name_end)),
                std::string(next, peek));
    } else if (codepoint != 0 && !page.Character(std::string(current, next),
                                                 std::string(next, end))) {
      // Do not consume the rejected character, including the final one.
      return false;
    }
    position_ = static_cast<std::size_t>(next - utf8_string_.cbegin());
  }
  return true;
}

Task<int> TextoutLongOperation::Run(WaitContext& waits) {
  auto window = machine_.GetSystem().text().GetCurrentMwnd();
  while (true) {
    if (!machine_.GetSystem().text().system_visible())
      throw rlvm::Exception("Trying to Textout while TextSystem is hidden!");
    bool complete = false;
    window->PrepareMessageText(
        [&](TextPage& page) { complete = PrepareNextPage(page); });
    while (window->HasPendingMessageText()) {
      if (no_wait_) {
        window->FlushMessageText();
        break;
      }
      co_await waits.Schedule();
    }
    if (std::exchange(flush_input_, false))
      co_await waits.WaitFor(std::chrono::milliseconds::zero());
    if (complete)
      co_return 0;

    // A full page pauses only after all its prepared text has been painted.
    machine_.PushLongOperation(std::make_shared<NewPageAfterLongop>(
        machine_, new PauseLongOperation(machine_)));
    co_await waits.Schedule();
  }
}

}  // namespace lop
