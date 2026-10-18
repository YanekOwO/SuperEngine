// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
// -----------------------------------------------------------------------

#pragma once

#include "long_operations/long_operation.hpp"
#include "vm/value.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <vector>

class EventSystem;

namespace serilang {
class GarbageCollector;
struct Future;
class VM;
}  // namespace serilang

namespace lop {

serilang::Value MakeResolvedFuture(serilang::GarbageCollector& gc,
                                   int result = 0);
serilang::Value MakePollingWaitFuture(
    serilang::VM& vm,
    std::function<bool()> done,
    bool key_skip = false,
    EventSystem* event_system = nullptr,
    std::chrono::milliseconds poll_interval = std::chrono::milliseconds(5),
    std::function<void()> on_key = {});

// Owns a native operation and exposes its result through a VM promise. Lazy
// until first VM await unless Start() is called. All operation suspensions must
// use the supplied WaitContext. One owner drives and consumes the native task;
// VM futures may share its promise. The VM and event system must outlive it.
// All operations run on the driving thread. Running operations must not cancel
// or destroy their task. Cancellation callbacks must not cancel or destroy the
// task or mutate its owning collection.
class FutureBackedCoroutineTask {
 public:
  FutureBackedCoroutineTask(serilang::VM& vm,
                            std::unique_ptr<LongOperation> operation,
                            EventSystem* event_system = nullptr);
  ~FutureBackedCoroutineTask();
  FutureBackedCoroutineTask(const FutureBackedCoroutineTask&) = delete;
  FutureBackedCoroutineTask& operator=(const FutureBackedCoroutineTask&) =
      delete;
  FutureBackedCoroutineTask(FutureBackedCoroutineTask&&) noexcept;
  FutureBackedCoroutineTask& operator=(FutureBackedCoroutineTask&&) noexcept;

  void Start();
  void Cancel() noexcept;
  serilang::Future* MakeFuture(serilang::GarbageCollector& gc);
  bool Done() const noexcept;

 private:
  struct State;
  std::unique_ptr<State> state_;
};

class PendingCoroutineTasks {
 public:
  serilang::Future* MakeFuture(serilang::VM& vm,
                               std::unique_ptr<LongOperation> operation,
                               EventSystem* event_system = nullptr);
  serilang::Future* MakeFuture(serilang::GarbageCollector& gc,
                               FutureBackedCoroutineTask task);
  void PruneDone();
  std::size_t size() const noexcept;

 private:
  std::vector<FutureBackedCoroutineTask> tasks_;
};

}  // namespace lop
