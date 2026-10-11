// -----------------------------------------------------------------------
//
// This file is part of RLVM
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
// -----------------------------------------------------------------------

#include "core/event_listener.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "srbind/srbind.hpp"
#include "systems/event_system.hpp"
#include "systems/system.hpp"
#include "utilities/overload.hpp"
#include "vm/future.hpp"
#include "vm/vm.hpp"

#include <memory>

namespace libsiglus::binding {
namespace sb = srbind;
using namespace serilang;

void BindEvent(SiglusMachine& runtime) {
  sb::module_ m(*runtime.vm, "event");

  m.def("keydown", [event = runtime.system->event_ptr()](VM& vm) {
    struct KeyDownListener : public EventListener {
      std::weak_ptr<Promise> promise;
      KeyDownListener(std::weak_ptr<Promise> p) : promise(p) {}

      void OnEvent(std::shared_ptr<Event> event) override {
        std::visit(overload(
                       [promise = this->promise](const KeyDown& event) {
                         if (auto locked = promise.lock()) {
                           int keycode = static_cast<int>(event.code);
                           locked->Resolve(Value(keycode));
                         }
                       },
                       [](const auto&) { /*ignore*/ }),
                   *event);
      }
    };

    Future* future = vm.gc_->Allocate<Future>();
    future->promise->initial_await = [event](VM& vm, Value&, Value& fut) {
      std::shared_ptr<Promise> promise = get_promise(fut);
      auto listener = std::make_shared<KeyDownListener>(promise);
      event->AddListener(listener);
      promise->usrdata.emplace_back(std::move(listener));
    };

    return Value(future);
  });
}

RLVM_REGISTER(SiglusBindingRegistry, "event", BindEvent)

}  // namespace libsiglus::binding
