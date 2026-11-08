// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
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

#include "core/group.hpp"
#include "long_operations/long_operation.hpp"

#include <functional>
#include <memory>

namespace libsiglus {
struct SiglusMachine;
struct GroupSelectionOperationState;
class ExecutionWriter;
class ExecutionReader;
}  // namespace libsiglus

namespace lop {

class SiglusGroupSelection final : public LongOperation {
 public:
  SiglusGroupSelection(std::shared_ptr<Group> group,
                       std::function<void()> consume_input);
  ~SiglusGroupSelection() override;
  Task<int> Run(WaitContext& waits) override;
  libsiglus::GroupSelectionOperationState CaptureCheckpoint(
      libsiglus::ExecutionWriter&) const;
  static std::unique_ptr<LongOperation> RestoreCheckpoint(
      libsiglus::SiglusMachine&,
      libsiglus::ExecutionReader&,
      const libsiglus::GroupSelectionOperationState&);

 private:
  std::shared_ptr<Group> group_;
  std::function<void()> consume_input_;
  bool finished_ = false;
};

}  // namespace lop
