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

#include "long_operations/after_long_operation.hpp"

namespace lop {

PerformAfterLongOperationDecorator::PerformAfterLongOperationDecorator(
    RLMachine& machine,
    LongOperation* operation)
    : machine_(machine), operation_(operation) {}

void PerformAfterLongOperationDecorator::OnEvent(std::shared_ptr<Event> event) {
  operation_->OnEvent(std::move(event));
}

Task<int> PerformAfterLongOperationDecorator::Run(WaitContext& waits) {
  const int result = co_await operation_->Run(waits);
  PerformAfterLongOperation(machine_);
  co_return result;
}

FrameRenderer PerformAfterLongOperationDecorator::GetFrameRenderer() {
  return operation_->GetFrameRenderer();
}

}  // namespace lop
