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
#include "libsiglus/frame_request_manager.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"
#include "vm/exception.hpp"
#include "vm/promise.hpp"

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
    System* system,
    libsiglus::FrameRequestManager& frame_requests,
    std::shared_ptr<TextWindow> window,
    std::shared_ptr<MwndMessageState> message_state,
    AfterWait after_wait)
    : system_(system),
      frame_requests_(frame_requests),
      window_(std::move(window)),
      message_state_(std::move(message_state)),
      after_wait_(after_wait) {}

Task<int> SiglusMwndWaitOperation::Run(WaitContext& waits) {
  while (system_ && system_->text().IsReadingBacklog())
    co_await waits.Schedule(false);
  while (system_ && window_->HasPendingMessageText()) {
    if (system_->text().IsReadingBacklog()) {
      co_await waits.Schedule(false);
      continue;
    }
    if (MessageNowait(system_)) {
      window_->FlushMessageText();
      break;
    }
    if ((co_await waits.WaitFor(std::chrono::milliseconds(5), true)) ==
        WaitOutcome::InterruptedByInput) {
      window_->FlushMessageText();
      // Finish the current input dispatch before listening for the pause's
      // advance input; one click must not both reveal and dismiss the text.
      co_await waits.WaitFor(std::chrono::milliseconds::zero());
    }
  }
  if (after_wait_ == AfterWait::MessageOnly)
    co_return 0;

  if (!system_ || MessageNowait(system_)) {
    co_await PaceMessage(waits);
    FinishWait();
    co_return 0;
  }

  PauseGuard pause(system_->text(), window_, after_wait_ == AfterWait::Clear);

  try {
    const auto start = system_->text().MessageTime();
    const int auto_time = EffectiveAutoTime(
        system_->local_config(), system_->global_config(), AutoModeCharCount());

    while (true) {
      const auto outcome =
          co_await waits.WaitFor(std::chrono::milliseconds(5), true);
      if (system_->text().IsReadingBacklog())
        continue;
      if (outcome == WaitOutcome::InterruptedByInput) {
        pause.Reset();
        co_await PaceMessage(waits);
        FinishWait();
        co_return 1;
      }

      if (MessageNowait(system_)) {
        pause.Reset();
        co_await PaceMessage(waits);
        FinishWait();
        co_return 0;
      }

      if (AutoModeEnabled(system_)) {
        if ((system_->text().MessageTime() - start).count() >=
            std::max(auto_time, 0)) {
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

Task<void> SiglusMwndWaitOperation::PaceMessage(WaitContext& waits) {
  if (!system_ || !system_->ShouldFastForward())
    co_return;

  auto frame = frame_requests_.RequestMessageFrame();
  // Input may initiate skipping, but repeated key events cannot substitute
  // for the engine frame that makes the skipped message visible.
  while (!frame->HasResult())
    co_await waits.Schedule(false);
  if (frame->status == serilang::Promise::Status::Rejected)
    throw serilang::RuntimeError(frame->result->error());
}

void SiglusMwndWaitOperation::FinishWait() {
  if (!system_ || !message_state_)
    return;
  switch (after_wait_) {
    case AfterWait::MessageOnly:
    case AfterWait::None:
      return;
    case AfterWait::Paragraph: {
      message_state_->MarkNovelClear(*window_);
      TextWindow& page = *window_;
      page.Submit(msgcmd::ClearIndentation{});
      page.Submit(msgcmd::HardBreak{});
      return;
    }
    case AfterWait::Clear:
      message_state_->MarkMessageClearReady(*window_, system_->local_config());
      return;
  }
}

int SiglusMwndWaitOperation::AutoModeCharCount() {
  const int configured_count = system_->local_config().auto_mode_moji_cnt;
  if (configured_count > 0)
    return configured_count;

  return window_->GetCharacterCount();
}

}  // namespace lop
