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

#pragma once

#include "long_operations/long_operation.hpp"

#include <memory>

class System;
class TextWindow;
struct MwndMessageState;
namespace libsiglus {
class FrameRequestManager;
}

namespace lop {

class SiglusMwndWaitOperation final : public LongOperation {
 public:
  enum class AfterWait { MessageOnly, None, Paragraph, Clear };

  // The system and frame manager must outlive the operation.
  SiglusMwndWaitOperation(System* system,
                          libsiglus::FrameRequestManager& frame_requests,
                          std::shared_ptr<TextWindow> window,
                          std::shared_ptr<MwndMessageState> message_state,
                          AfterWait after_wait);
  Task<int> Run(WaitContext& waits) override;

 private:
  class PauseGuard;
  Task<void> PaceMessage(WaitContext& waits);
  void FinishWait();
  int AutoModeCharCount();

  System* system_;
  libsiglus::FrameRequestManager& frame_requests_;
  std::shared_ptr<TextWindow> window_;
  std::shared_ptr<MwndMessageState> message_state_;
  AfterWait after_wait_;
};

}  // namespace lop
