// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
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
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
// -----------------------------------------------------------------------

#include "libsiglus/bindings/registry.hpp"

#include "core/input.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "systems/event_system.hpp"

#include <memory>

namespace libsiglus::binding {
namespace sr = serilang;
namespace sb = srbind;

void BindInput(SiglusMachine& runtime) {
  sr::VM& vm = *runtime.vm;
  sb::module_ mm(vm.gc_.get(), vm.globals_.get());
  mm.def("frame", [] {});

  sb::module_ m(vm, "input");

  std::shared_ptr<InputListener> listener = std::make_shared<InputListener>();
  runtime.input_event_listener = listener;
  runtime.system->event().AddListener(19, listener);

  m.def("next", [listener] { listener->ResetState(); });
  m.def("clear", [listener] { listener->ResetState(); });

  struct InputButton {
    int type;
    std::shared_ptr<InputListener> listener;
    InputListener::State Get() {
      return type == 0 ? listener->decide : listener->cancel;
    }
  };
  sb::class_<InputButton> key(m, "InputButton", false);
  key.def("on_down", [](InputButton* ref) -> int {
    auto state = ref->Get();
    return state.on_down;
  });
  key.def("on_up", [](InputButton* ref) -> int {
    auto state = ref->Get();
    return state.on_up;
  });
  key.def("on_down_up", [](InputButton* ref) -> int {
    auto state = ref->Get();
    return state.down_up;
  });
  key.def("is_down", [](InputButton* ref) -> int {
    auto state = ref->Get();
    return state.down;
  });
  key.def("is_up", [](InputButton* ref) -> int {
    auto state = ref->Get();
    return !state.down;
  });
  key.def("on_flick", [](InputButton* ref) -> int { return 0; });
  key.def("get_flick_angle", [](InputButton* ref) -> int { return 0; });
  key.def("get_flick_pixel", [](InputButton* ref) -> int { return 0; });
  key.def("get_flick_mm", [](InputButton* ref) -> int { return 0; });

  key.inst("decide", 0, listener);
  key.inst("cancel", 1, listener);
}

RLVM_REGISTER(SiglusBindingRegistry, "0_input", BindInput)

}  // namespace libsiglus::binding
