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

#pragma once

#include "core/frame_counter.hpp"
#include "long_operations/long_operation.hpp"

#include <memory>
#include <optional>

class Stage;
class System;
struct GlobalConfig;
namespace serilang {
class Promise;
}

namespace lop {

struct WipeCompletion {
  // Completion requests specify a minimum total number of traversals since
  // the wipe started. They always leave at least one traversal still to run.
  void RequestFinish(int result, int minimum_total_boundaries = 1);
  bool AfterFrameActions();

  int regular_boundaries = 0;
  std::optional<int> finish_after_boundary;
  int result = 0;
};

struct SiglusWipeState {
  std::shared_ptr<serilang::Promise> promise;
  int key_wait_mode = -1;
  bool cancelled = false;
  WipeCompletion completion;
};

class SiglusWipeOperation final : public LongOperation {
 public:
  SiglusWipeOperation(System* system,
                      Stage* stage,
                      bool wait_flag,
                      std::weak_ptr<SiglusWipeState> active,
                      FrameCounter progress_counter,
                      const GlobalConfig& global_config);
  Task<int> Run(WaitContext& waits) override;

 private:
  static constexpr std::chrono::milliseconds kPollInterval =
      std::chrono::milliseconds(5);
  bool IsCancelled() const;
  int CurrentResult() const;
  bool ShouldInterruptOnInput() const;
  void FinishStage(int result, int regular_boundaries = 1);

  System* system_;
  Stage* stage_;
  bool wait_flag_;
  std::weak_ptr<SiglusWipeState> active_;
  FrameCounter progress_counter_;
  const GlobalConfig& global_config_;
};

}  // namespace lop
