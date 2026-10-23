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
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
// -----------------------------------------------------------------------

#include "long_operations/siglus_textout.hpp"

#include "systems/system.hpp"
#include "systems/text_page.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"

#include <chrono>
#include <utility>

namespace lop {

SiglusTextoutOperation::SiglusTextoutOperation(System* system, std::string text)
    : system_(system), text_(std::move(text)) {}

Task<int> SiglusTextoutOperation::Run(WaitContext& waits) {
  if (!system_)
    co_return 0;

  auto window = system_->text().GetCurrentMwnd();
  window->PrepareMessageText([this](TextPage& page) { page.Text(text_); });
  while (window->HasPendingMessageText()) {
    if ((co_await waits.Schedule()) == WaitOutcome::InterruptedByInput) {
      window->FlushMessageText();
      // Finish this input dispatch before a subsequent pause can listen.
      co_await waits.WaitFor(std::chrono::milliseconds::zero());
    }
  }

  co_return 0;
}

}  // namespace lop
