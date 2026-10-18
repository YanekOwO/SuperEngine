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

#include "long_operations/siglus_mwnd_wait.hpp"

#include "core/game_config.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "systems/system.hpp"
#include "systems/text_page.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace lop {
namespace {

bool MessageNowait(System* system) {
  return !system || system->ShouldFastForward() ||
         EffectiveMessageNowait(system->local_config(),
                                system->global_config());
}

bool AutoModeEnabled(System* system) {
  return system &&
         EffectiveAutoMode(system->local_config(), system->global_config());
}

}  // namespace

class SiglusMwndWaitOperation::PauseGuard {
 public:
  PauseGuard(TextSystem& text,
             std::shared_ptr<TextWindow> window,
             bool page_icon)
      : text_(&text), window_(std::move(window)) {
    text_->set_in_pause_state(true);
    window_->ShowWaitIcon(page_icon);
  }

  ~PauseGuard() { Reset(); }

  void Reset() {
    if (!text_)
      return;

    window_->HideWaitIcon();
    text_->set_in_pause_state(false);
    text_ = nullptr;
  }

 private:
  TextSystem* text_;
  std::shared_ptr<TextWindow> window_;
};

SiglusMwndWaitOperation::SiglusMwndWaitOperation(
    libsiglus::SiglusMachine& runtime,
    std::shared_ptr<TextWindow> window,
    std::shared_ptr<MwndMessageState> message_state,
    AfterWait after_wait)
    : runtime_(runtime),
      system_(runtime.system.get()),
      window_(std::move(window)),
      local_config_(runtime.local_config),
      message_state_(std::move(message_state)),
      after_wait_(after_wait) {}

Task<int> SiglusMwndWaitOperation::Run(WaitContext& waits) {
  if (!system_ || MessageNowait(system_)) {
    FinishWait();
    co_return 0;
  }

  PauseGuard pause(system_->text(), window_, after_wait_ == AfterWait::Clear);

  try {
    auto counter = runtime_.CreateCounter();
    counter->StartGame();
    const int auto_time = EffectiveAutoTime(
        local_config_, system_->global_config(), AutoModeCharCount());

    while (true) {
      if ((co_await waits.WaitFor(std::chrono::milliseconds(5), true)) ==
          WaitOutcome::InterruptedByInput) {
        pause.Reset();
        FinishWait();
        co_return 1;
      }

      if (MessageNowait(system_)) {
        pause.Reset();
        FinishWait();
        co_return 0;
      }

      if (AutoModeEnabled(system_)) {
        if (counter->Get() >= std::max(auto_time, 0)) {
          pause.Reset();
          FinishWait();
          co_return 0;
        }
      }
    }
  } catch (...) {
    pause.Reset();
    throw;
  }
}

void SiglusMwndWaitOperation::FinishWait() {
  if (!system_ || !message_state_)
    return;
  switch (after_wait_) {
    case AfterWait::None:
      return;
    case AfterWait::Paragraph: {
      message_state_->MarkNovelClear(Page());
      TextPage& page = Page();
      page.ResetIndentation();
      page.HardBrake();
      return;
    }
    case AfterWait::Clear:
      message_state_->MarkMessageClearReady(Page());
      return;
  }
}

int SiglusMwndWaitOperation::AutoModeCharCount() {
  const int configured_count = local_config_.auto_mode_moji_cnt;
  if (configured_count > 0)
    return configured_count;

  const int current_count = Page().number_of_chars_on_page();
  if (!message_state_)
    return current_count;
  return std::max(current_count - message_state_->auto_mode_base_chars, 0);
}

TextPage& SiglusMwndWaitOperation::Page() {
  return system_->text().GetPageOnWindow(window_);
}

}  // namespace lop
