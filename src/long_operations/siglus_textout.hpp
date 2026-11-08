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

namespace libsiglus {
struct TextoutOperationState;
struct SiglusMachine;
class ExecutionWriter;
class ExecutionReader;
}  // namespace libsiglus

#include "long_operations/long_operation.hpp"

#include <memory>
#include <string>

class System;
class TextWindow;

namespace lop {

class SiglusTextoutOperation final : public LongOperation {
 public:
  SiglusTextoutOperation(System* system,
                         std::shared_ptr<TextWindow> window,
                         std::string text);
  Task<int> Run(WaitContext& waits) override;
  libsiglus::TextoutOperationState CaptureCheckpoint(
      libsiglus::ExecutionWriter& execution) const;
  static std::unique_ptr<LongOperation> RestoreCheckpoint(
      libsiglus::SiglusMachine& machine,
      libsiglus::ExecutionReader& execution,
      const libsiglus::TextoutOperationState& state);

 private:
  System* system_;
  std::shared_ptr<TextWindow> window_;
  std::string text_;
  enum class Phase { Submit, Reveal, Debounce, Done };
  Phase phase_ = Phase::Submit;
};

}  // namespace lop
