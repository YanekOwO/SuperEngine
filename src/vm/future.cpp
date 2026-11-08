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

#include "vm/future.hpp"
#include "vm/serialization.hpp"

#include "srbind/srbind.hpp"
#include "vm/gc.hpp"
#include "vm/list.hpp"
#include "vm/object.hpp"
#include "vm/promise.hpp"
#include "vm/vm.hpp"

#include <chrono>
#include <format>

namespace serilang {
Future::Future() : promise(std::make_shared<Promise>()) {}

std::string Future::Str() const { return Desc(); }
std::string Future::Desc() const { return "<future>"; }

void Future::MarkRoots(GCVisitor& visitor) {
  for (IObject* it : promise->roots)
    visitor.MarkSub(it);
}

namespace async_builtin {
namespace chr = std::chrono;
Value Sleep(VM& vm, int ms, Value result) {
  if (ms < 0)
    ms = 0;
  Future* future = vm.gc_->Allocate<Future>();
  future->AddRoot(result);
  future->promise->initial_await = [r = std::move(result),
                                    dur = chr::milliseconds(ms)](VM& vm, Value&,
                                                                 Value& sleep) {
    std::weak_ptr<Promise> promise = get_promise(sleep);

    vm.scheduler_.PushCallbackAfter(
        [promise, r = std::move(r)]() {
          if (auto p = promise.lock())
            p->Resolve(std::move(r));
        },
        dur);
  };

  return Value(future);
}

Value Timeout(VM& vm, Value awaited, int ms) {
  if (ms < 0)
    ms = 0;
  Future* future = vm.gc_->Allocate<Future>();
  future->promise->AddRoot(awaited);

  future->promise->initial_await = [ms, awaited = std::move(awaited)](
                                       VM& vm, Value&, Value& timeout) {
    std::weak_ptr<Promise> promise = get_promise(timeout);

    vm.scheduler_.PushCallbackAfter(
        [ms, promise]() {
          if (auto p = promise.lock())
            p->Reject(std::format("Timeout after {} ms", ms));
        },
        chr::milliseconds(ms));

    vm.Await(timeout, const_cast<Value&>(awaited),
             [promise](const expected<Value, std::string>& outcome) {
               if (auto p = promise.lock()) {
                 if (outcome.has_value())
                   p->Resolve(outcome.value());
                 else
                   p->Reject(outcome.error());
               }
             });
  };

  return Value(future);
}

Value Gather(VM& vm, std::vector<Value> awaitables) {
  Future* future = vm.gc_->Allocate<Future>();
  if (awaitables.empty()) {
    auto* empty_list = vm.gc_->Allocate<List>();
    future->promise->Resolve(Value(empty_list));
    return Value(future);
  }

  struct GatherData {
    List* results;
    size_t remaining;
  };
  auto data = std::make_shared<GatherData>();
  data->results =
      vm.gc_->Allocate<List>(std::vector<Value>(awaitables.size(), nil));
  data->remaining = awaitables.size();
  future->promise->roots.push_back(data->results);

  future->promise->initial_await = [data = std::move(data),
                                    awaitables = std::move(awaitables)](
                                       VM& vm, Value&, Value& gather) {
    std::weak_ptr<Promise> promise = get_promise(gather);

    for (size_t i = 0; i < awaitables.size(); ++i) {
      Value& awaitable = const_cast<Value&>(awaitables[i]);

      vm.Await(gather, awaitable,
               [data, i, promise](expected<Value, std::string> outcome) {
                 if (auto p = promise.lock()) {
                   if (!outcome.has_value())
                     p->Reject(outcome.error());
                   else
                     data->results->items[i] = outcome.value();

                   if (--data->remaining == 0)
                     p->Resolve(Value(data->results));
                 }
               });
    }
  };

  return Value(future);
}

Value Race(VM& vm, std::vector<Value> awaitables) {
  Future* future = vm.gc_->Allocate<Future>();
  if (awaitables.empty()) {
    future->promise->Resolve(nil);
    return Value(future);
  }

  future->promise->initial_await = [awaitables = std::move(awaitables)](
                                       VM& vm, Value&, Value& race) {
    std::weak_ptr<Promise> promise = get_promise(race);

    for (Value const& awaitable : awaitables) {
      vm.Await(race, const_cast<Value&>(awaitable),
               [promise](expected<Value, std::string> outcome) {
                 if (auto p = promise.lock()) {
                   if (!outcome.has_value())
                     p->Reject(outcome.error());
                   else
                     p->Resolve(outcome.value());
                 }
               });
    }
  };

  return Value(future);
}

}  // namespace async_builtin

void InstallAsyncBuiltins(VM& vm) {
  srbind::module_ m(vm, "async");

  m.def(
      "Sleep",
      +[](VM& vm, int ms, Value result) {
        return async_builtin::Sleep(vm, ms, std::move(result));
      },
      srbind::arg("msecs"), srbind::arg("result") = nil);

  m.def(
      "Timeout",
      +[](VM& vm, Value awaited, int ms) {
        return async_builtin::Timeout(vm, std::move(awaited), ms);
      },
      srbind::arg("awaitable"), srbind::arg("timeout_ms"));

  m.def(
      "Gather", +[](VM& vm, std::vector<Value> awaitables) {
        return async_builtin::Gather(vm, std::move(awaitables));
      });

  m.def(
      "Race", +[](VM& vm, std::vector<Value> awaitables) {
        return async_builtin::Race(vm, std::move(awaitables));
      });
}

void Future::Save(ObjectWriter& out) const { out.WritePromise(promise); }
void Future::Load(ObjectReader& in) {
  promise = in.ReadPromise();
  if (!promise)
    throw std::runtime_error("saved future has no promise");
}

}  // namespace serilang
