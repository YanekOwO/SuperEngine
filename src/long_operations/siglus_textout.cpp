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

#include "core/game_config.hpp"
#include "systems/system.hpp"
#include "systems/text_page.hpp"
#include "systems/text_system.hpp"

#include <utf8.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace lop {

SiglusTextoutOperation::SiglusTextoutOperation(System* system,
                                               LocalConfig& local_config,
                                               std::string text)
    : system_(system), local_config_(local_config), text_(std::move(text)) {}

Task<int> SiglusTextoutOperation::Run(WaitContext& waits) {
  for (std::string::const_iterator cur = text_.cbegin(), next;
       cur != text_.cend(); cur = next) {
    if (!ShouldFlushText()) {
      const int speed = system_ ? EffectiveMessageSpeed(
                                      local_config_, system_->global_config())
                                : 0;

      const std::chrono::milliseconds duration(std::max(1, speed));
      if ((co_await waits.WaitFor(duration, true)) ==
          WaitOutcome::InterruptedByInput) {
        flush_ = true;
      }
    }

    next = cur;
    utf8::next(next, text_.cend());

    const std::string current(cur, next);
    const std::string rest(next, text_.cend());
    TextPage& page = system_->text().GetCurrentPage();
    page.Character(current, rest);
  }

  co_return 0;
}

bool SiglusTextoutOperation::ShouldFlushText() const {
  if (!system_)
    return true;
  if (flush_)
    return true;
  if (system_->ShouldFastForward())
    return true;
  if (EffectiveMessageNowait(local_config_, system_->global_config()))
    return true;
  return false;
}

}  // namespace lop
