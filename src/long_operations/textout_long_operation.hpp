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
class TextPage;

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

  // Prepare until the source ends or a character needs a new page.
  bool PrepareNextPage(TextPage& page);

  std::string utf8_string_;

  std::size_t position_ = 0;

  // Sets whether we should display as much text as we can immediately.
  bool no_wait_;
  bool flush_input_ = false;
};

}  // namespace lop
