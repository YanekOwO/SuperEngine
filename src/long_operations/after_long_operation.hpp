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

#include "long_operations/long_operation.hpp"

#include <memory>

class RLMachine;

namespace lop {

// Owns a child operation and performs an action after its coroutine completes.
class PerformAfterLongOperationDecorator : public LongOperation {
 public:
  PerformAfterLongOperationDecorator(RLMachine& machine,
                                     LongOperation* operation);
  ~PerformAfterLongOperationDecorator() override = default;
  void OnEvent(std::shared_ptr<Event> event) override;
  Task<int> Run(WaitContext& waits) override;

 private:
  virtual void PerformAfterLongOperation(RLMachine& machine) = 0;
  RLMachine& machine_;
  std::unique_ptr<LongOperation> operation_;
};

}  // namespace lop
