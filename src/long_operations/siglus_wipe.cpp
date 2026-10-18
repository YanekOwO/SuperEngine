// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
// Copyright (C) 2026 RLVM contributors
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

#include "long_operations/siglus_wipe.hpp"

#include "core/game_config.hpp"
#include "core/stage.hpp"
#include "systems/system.hpp"
#include "vm/promise.hpp"

#include <algorithm>
#include <utility>

namespace lop {

void WipeCompletion::RequestFinish(int requested_result,
                                   int minimum_total_boundaries) {
  if (finish_after_boundary) {
    // Input interruption takes precedence over natural completion.
    if (requested_result != 0) {
      result = requested_result;
      finish_after_boundary =
          std::min(*finish_after_boundary, regular_boundaries + 1);
    }
    return;
  }
  result = requested_result;
  finish_after_boundary =
      std::max(std::max(1, minimum_total_boundaries), regular_boundaries + 1);
}

bool WipeCompletion::AfterFrameActions() {
  ++regular_boundaries;
  return finish_after_boundary && regular_boundaries >= *finish_after_boundary;
}

SiglusWipeOperation::SiglusWipeOperation(System* system,
                                         Stage* stage,
                                         bool wait_flag,
                                         std::weak_ptr<SiglusWipeState> active,
                                         FrameCounter progress_counter,
                                         const GlobalConfig& global_config)
    : system_(system),
      stage_(stage),
      wait_flag_(wait_flag),
      active_(std::move(active)),
      progress_counter_(std::move(progress_counter)),
      global_config_(global_config) {}

Task<int> SiglusWipeOperation::Run(WaitContext& waits) {
  // Polling starts after the priming interval. Start handles
  // already-terminal wipes separately so their exact two regular
  // frame-action boundaries do not depend on timer phase.
  const WaitOutcome primed =
      co_await waits.WaitFor(kPollInterval, ShouldInterruptOnInput());
  if (primed == WaitOutcome::InterruptedByInput) {
    FinishStage(1);
    co_return 1;
  }

  while (true) {
    if (IsCancelled())
      co_return CurrentResult();

    if (!system_ || !stage_) {
      FinishStage(0);
      co_return 0;
    }

    const bool fast_forward = system_->ShouldFastForward();
    if (fast_forward && wait_flag_) {
      // Legacy's wipe-wait process explicitly ends a skipped wipe before
      // its next regular traversal. StartPrepared pre-schedules this case
      // when skipping is already active; this also handles it becoming
      // active later.
      FinishStage(0, 1);
      co_return 0;
    }

    // Non-waiting wipes follow the game delta supplied by the machine,
    // including changes to fast-forward while a wipe is in progress.
    const float progress = progress_counter_.ReadFrame();
    if (progress_counter_.IsFinished()) {
      FinishStage(0, 3);
      co_return 0;
    }

    stage_->SetTransitionRenderAlpha(progress, 1.0 - progress);

    const WaitOutcome outcome =
        co_await waits.WaitFor(kPollInterval, ShouldInterruptOnInput());
    if (outcome == WaitOutcome::InterruptedByInput) {
      FinishStage(1);
      co_return 1;
    }
  }
}

bool SiglusWipeOperation::IsCancelled() const {
  std::shared_ptr<SiglusWipeState> active = active_.lock();
  if (!active)
    return true;
  if (active->cancelled)
    return true;
  return active->promise && active->promise->HasResult();
}

int SiglusWipeOperation::CurrentResult() const {
  if (std::shared_ptr<SiglusWipeState> active = active_.lock())
    return active->completion.result;
  return 0;
}

bool SiglusWipeOperation::ShouldInterruptOnInput() const {
  std::shared_ptr<SiglusWipeState> active = active_.lock();
  if (!active)
    return false;

  const int key_wait_mode = active->key_wait_mode;
  if (key_wait_mode == 0)
    return false;
  if (key_wait_mode == 1)
    return true;

  return global_config_.system.skip_wipe_anime;
}

void SiglusWipeOperation::FinishStage(int result, int regular_boundaries) {
  if (auto active = active_.lock(); active && !active->cancelled)
    active->completion.RequestFinish(result, regular_boundaries);
}

}  // namespace lop
