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

#include "libsiglus/backlog.hpp"
#include "long_operations/long_operation.hpp"

namespace libsiglus {
struct BacklogOperationState;
class ExecutionWriter;
class ExecutionReader;
struct SiglusMachine;
}  // namespace libsiglus

namespace lop {
class SiglusBacklogWait final : public LongOperation {
 public:
  explicit SiglusBacklogWait(
      std::shared_ptr<libsiglus::SiglusBacklog::Session> session)
      : session_(std::move(session)) {}
  ~SiglusBacklogWait() override { session_->open = false; }
  Task<int> Run(WaitContext& waits) override {
    while (session_->open)
      co_await waits.Schedule(false);
    co_return 0;
  }
  libsiglus::BacklogOperationState CaptureCheckpoint(
      libsiglus::ExecutionWriter&) const;
  static std::unique_ptr<LongOperation> RestoreCheckpoint(
      libsiglus::SiglusMachine&,
      libsiglus::ExecutionReader&,
      const libsiglus::BacklogOperationState&);

 private:
  std::shared_ptr<libsiglus::SiglusBacklog::Session> session_;
};

}  // namespace lop
