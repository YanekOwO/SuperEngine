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

#include "libsiglus/bindings/registry.hpp"

#include "core/counter.hpp"
#include "libsiglus/bindings/wait_helpers.hpp"
#include "srbind/module.hpp"
#include "srbind/srbind.hpp"
#include "systems/event_system.hpp"
#include "systems/system.hpp"
#include "vm/exception.hpp"
#include "vm/object.hpp"
#include "vm/value.hpp"
#include "vm/vm.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

namespace libsiglus::binding {
namespace sb = srbind;
namespace sr = serilang;

// A VM-facing non-owning reference to a Counter. Other Siglus elements use
// this handle to expose the same native Counter type as the global list.
struct CounterHandle {
  std::weak_ptr<Counter> counter_;

  std::shared_ptr<Counter> Get() const {
    std::shared_ptr<Counter> counter = counter_.lock();
    if (!counter)
      throw sr::RuntimeError("counter reference is no longer valid");
    return counter;
  }
};

void BindCounter(SiglusRuntime& runtime) {
  std::shared_ptr<CounterList> list = runtime.counter_list;
  if (!list)
    throw std::runtime_error("counter binding requires counter state");

  sr::VM& vm = *runtime.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  EventSystem* event = runtime.system ? &runtime.system->event() : nullptr;

  // ------------------------------------------------------------------------------
  // Counter
  sb::class_<CounterHandle> counter_class(m, "Counter");
  counter_class.def("set",
                    [](CounterHandle* h, int value) { h->Get()->Set(value); });
  counter_class.def("get",
                    [](const CounterHandle* h) { return h->Get()->Get(); });
  counter_class.def("reset", [](CounterHandle* h) { h->Get()->Reset(); });
  counter_class.def("start", [](CounterHandle* h) { h->Get()->Start(false); });
  counter_class.def("start_real",
                    [](CounterHandle* h) { h->Get()->Start(true); });
  auto counter_start_frame = [](bool real_time, bool loop) {
    return
        [real_time, loop](CounterHandle* h, int start, int end, int duration) {
          h->Get()->StartFrame(start, end, duration, real_time, loop);
        };
  };
  counter_class.def("start_frame", counter_start_frame(false, false));
  counter_class.def("start_frame_real", counter_start_frame(true, false));
  counter_class.def("start_frame_loop", counter_start_frame(false, true));
  counter_class.def("start_frame_loop_real", counter_start_frame(true, true));
  counter_class.def("stop", [](CounterHandle* h) { h->Get()->Stop(); });
  counter_class.def("resume", [](CounterHandle* h) { h->Get()->Resume(); });
  // wait until the counter reaches the requested value
  counter_class.def("wait", [event](CounterHandle* h, sr::VM& vm, int value) {
    auto check = [counter = *h, value] {
      return counter.Get()->Get() >= value;
    };
    return MakePollingWaitFuture(vm, check, false, event);
  });
  counter_class.def("wait_key",
                    [event](CounterHandle* h, sr::VM& vm, int value) {
                      auto check = [counter = *h, value] {
                        return counter.Get()->Get() >= value;
                      };
                      return MakePollingWaitFuture(vm, check, true, event);
                    });
  counter_class.def("check_value", [](const CounterHandle* h, int value) {
    return h->Get()->Get() >= value ? 1 : 0;
  });
  counter_class.def("check_active", [](const CounterHandle* h) {
    return h->Get()->IsActive() ? 1 : 0;
  });

  // ------------------------------------------------------------------------------
  // CounterList
  struct CounterListHandle {
    std::shared_ptr<CounterList> list;
  };
  sb::class_<CounterListHandle> list_class(m, "CounterList");
  auto counter_list =
      list_class.inst("counter", list);  // top level |counter| element
  counter_list.def(
      "__getitem__",
      [counter_class](CounterListHandle* h, int index) mutable {
        std::weak_ptr<Counter> counter = h->list->GetHandle(index);
        sr::NativeInstance* handle =
            counter_class.make_inst(std::move(counter));
        return sr::Value(handle);
      },
      sb::arg("index"));
  counter_list.def("size",
                   [](CounterListHandle* h) -> int { return h->list->size(); });

  runtime.reset_local_memory.emplace_back([list] { list->Reset(); });
}

RLVM_REGISTER(SiglusBindingRegistry, "1_counter", BindCounter)

}  // namespace libsiglus::binding
