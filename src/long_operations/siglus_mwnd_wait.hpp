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
class TextPage;
class TextWindow;
struct LocalConfig;
struct MwndMessageState;
namespace libsiglus {
struct SiglusMachine;
}

namespace lop {

class SiglusMwndWaitOperation final : public LongOperation {
 public:
  enum class AfterWait { MessageOnly, None, Paragraph, Clear };

  SiglusMwndWaitOperation(libsiglus::SiglusMachine& runtime,
                          std::shared_ptr<TextWindow> window,
                          std::shared_ptr<MwndMessageState> message_state,
                          AfterWait after_wait);
  Task<int> Run(WaitContext& waits) override;

 private:
  class PauseGuard;
  void FinishWait();
  int AutoModeCharCount();
  TextPage& Page();

  libsiglus::SiglusMachine& runtime_;
  System* system_;
  std::shared_ptr<TextWindow> window_;
  LocalConfig& local_config_;
  std::shared_ptr<MwndMessageState> message_state_;
  AfterWait after_wait_;
};

}  // namespace lop
