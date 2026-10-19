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
#include "systems/graphics_system.hpp"

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
  m.def("clear", [listener] { listener->ClearState(); });

  sb::module_ mouse(vm, "mouse");
  mouse.def("next", [listener] { listener->NextMouse(); });
  mouse.def("clear", [listener] { listener->ClearMouse(); });
  mouse.def("pos_x", [listener] { return listener->mouse_pos.x(); });
  mouse.def("pos_y", [listener] { return listener->mouse_pos.y(); });
  mouse.def("get_pos_x", [listener] { return listener->mouse_pos.x(); });
  mouse.def("get_pos_y", [listener] { return listener->mouse_pos.y(); });

  // TODO: mouse.get_pos(x_ref, y_ref) needs writable output references. The
  // current argument lowering reads their values and loses the destinations.

  mouse.def("wheel", [listener] { return listener->mouse_wheel; });
  mouse.def("set_pos", [&runtime](int x, int y) {
    runtime.system->graphics().WarpMouse(Point(x, y));
  });

  enum class Source { Decide, Cancel, Left, Right };
  struct InputButton {
    Source source;
    std::shared_ptr<InputListener> listener;
    InputListener::State Get() {
      switch (source) {
        case Source::Decide:
          return listener->decide;
        case Source::Cancel:
          return listener->cancel;
        case Source::Left:
          return listener->mouse_left;
        case Source::Right:
          return listener->mouse_right;
      }
      return {};
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

  key.inst("decide", Source::Decide, listener);
  key.inst("cancel", Source::Cancel, listener);
  (*mouse.dict())["left"] = sr::Value(key.make_inst(Source::Left, listener));
  (*mouse.dict())["right"] = sr::Value(key.make_inst(Source::Right, listener));
}

RLVM_REGISTER(SiglusBindingRegistry, "0_input", BindInput)

}  // namespace libsiglus::binding
