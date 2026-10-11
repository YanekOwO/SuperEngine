// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
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

#include "vm/function.hpp"
#include "vm/gc.hpp"
#include "vm/object.hpp"
#include "vm/scheduler.hpp"
#include "vm/value.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace serilang {

struct Fiber;
struct Chunk;

namespace helper {
inline Value pop(std::vector<Value>& stack) {
  Value val = std::move(stack.back());
  stack.pop_back();
  return val;
}
inline void push(std::vector<Value>& stack, Value v) {
  stack.emplace_back(std::move(v));
}
}  // namespace helper

struct UnhandledError : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

class VM {
 public:
  explicit VM(std::shared_ptr<GarbageCollector> gc);
  explicit VM(std::shared_ptr<GarbageCollector> gc,
              std::unordered_map<std::string, Value> globals,
              std::unordered_map<std::string, Value> builtins);

  Fiber* AddFiber(Code* entry);
  Fiber* ScheduleExecute(Function* fn);

  // Invoke a script callback on a VM-owned isolated fiber through its first
  // suspension or completion. Suspended continuations use the normal scheduler.
  // Inline failures propagate to the caller; return values are discarded.
  void InvokeUntilBlocked(Function& callback, std::vector<Value> arguments);

  // Run the event loop until no runnable work and no timers remain.
  // Returns the last dead fiber's result if any.
  Value Run();
  inline void RequestStop() noexcept { stop_requested_ = true; }
  inline bool IsStopRequested() const noexcept { return stop_requested_; }

  /// Pop an exception from f.op_stack and unwind to the nearest handler.
  void Error(Fiber& f);
  /// Push exc onto f.op_stack, then unwind to the nearest handler.
  void Error(Fiber& f, std::string exc);

  // REPL support: execute a single chunk, preserving VM state (globals, etc.)
  Value Evaluate(Code* chunk);

  // Trigger garbage collection: mark-root and sweep unreachable objects
  void CollectGarbage();
  // Let the garbage collector track a Value allocated elsewhere
  Value AddTrack(TempValue&& t);

  bool Await(
      Value& awaiter,
      Value& awaited,
      std::function<void(const expected<Value, std::string>&)> result_callback);

  void TrackPendingPromise(std::shared_ptr<Promise> promise);
  std::size_t CountPendingPromises();

 public:
  //----------------------------------------------------------------
  // Public VM state
  std::shared_ptr<GarbageCollector> gc_;
  size_t gc_threshold_ = 1024 * 1024;

  Fiber* main_fiber_ = nullptr;
  std::vector<Fiber*> fibres_;
  Value last_ = nil;  // last fiber's return value

  std::shared_ptr<std::unordered_map<std::string, Value>> globals_, builtins_;
  std::unordered_map<std::string, Module*> module_cache_;

  Scheduler scheduler_;

  std::vector<std::weak_ptr<Promise>> pending_promises_;

 private:
  //----------------------------------------------------------------
  // Execution helpers
  void Return(Fiber& f);
  std::unordered_map<std::string, Value>* GetNamespace(Fiber& f);

  void SweepDeadFibres();
  void MaybeSweepDeadFibres();

  //----------------------------------------------------------------
  // Core interpreter loop for one fiber
  void ExecuteFiber(Fiber* fib);

  std::size_t dead_fibre_count_ = 0;
  bool failed_fibre_pending_ = false;
  bool stop_requested_ = false;
};

}  // namespace serilang
