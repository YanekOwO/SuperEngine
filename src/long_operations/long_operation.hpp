// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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
//
// -----------------------------------------------------------------------

#pragma once

#include "core/event_listener.hpp"
#include "long_operations/wait_context.hpp"
#include "utilities/task.hpp"

namespace lop {

// A runtime adapter owns and drives the coroutine returned by Run. Operations
// suspend through the supplied context, including when awaiting child tasks.
// The coroutine must be destroyed before its operation and wait context.
class LongOperation : public EventListener {
 public:
  ~LongOperation() override = default;
  virtual Task<int> Run(WaitContext& waits) = 0;
};

}  // namespace lop
