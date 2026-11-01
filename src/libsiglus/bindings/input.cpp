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
  sb::module_ m(vm, "input");

  std::shared_ptr<InputListener> listener = std::make_shared<InputListener>();
  runtime.input_event_listener = listener;
  runtime.system->event().AddListener(19, listener);

  auto script = std::make_shared<ScriptInput>();
  runtime.script_input = script;
  runtime.system->event().AddListener(19, script);
  runtime.reset_local_memory.emplace_back(
      [listener, script, events = &runtime.system->event()] {
        listener->ReleaseAll();
        script->ReleaseAll();
        events->DiscardDeferredInput();
      });

  m.def("next", [script] { script->Next(); });
  m.def("clear", [script] { script->Clear(); });

  sb::module_ mouse(vm, "mouse");
  mouse.def("next", [script] { script->NextMouse(); });
  mouse.def("clear", [script] { script->ClearMouse(); });
  mouse.def("pos_x", [script] { return script->MousePosition().x(); });
  mouse.def("pos_y", [script] { return script->MousePosition().y(); });
  mouse.def("get_pos_x", [script] { return script->MousePosition().x(); });
  mouse.def("get_pos_y", [script] { return script->MousePosition().y(); });

  // TODO: mouse.get_pos(x_ref, y_ref) needs writable output references. The
  // current argument lowering reads their values and loses the destinations.

  mouse.def("wheel", [script] { return script->MouseWheel(); });
  mouse.def("set_pos", [&runtime](int x, int y) {
    runtime.presenter->WarpMouse(Point(x, y));
  });

  using Source = ScriptInput::Source;
  struct InputButton {
    Source source;
    std::shared_ptr<ScriptInput> input;
    InputListener::State Get() { return input->Get(source); }
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

  key.inst("decide", Source::Decide, script);
  key.inst("cancel", Source::Cancel, script);
  (*mouse.dict())["left"] = sr::Value(key.make_inst(Source::Left, script));
  (*mouse.dict())["right"] = sr::Value(key.make_inst(Source::Right, script));
}

RLVM_REGISTER(SiglusBindingRegistry, "0_input", BindInput)

}  // namespace libsiglus::binding
