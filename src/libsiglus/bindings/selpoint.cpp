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

#include "srbind/srbind.hpp"

namespace libsiglus::binding {

void BindSelpoint(SiglusMachine& machine) {
  auto& state = machine.host().selection_points;
  srbind::module_ m(machine.vm->gc_.get(), machine.vm->globals_.get());
  m.def("selpoint", [&state] { state.Mark(); });
  m.def("clear_selpoint", [&state] { state.Clear(); });
  m.def("check_selpoint", [&state] { return state.HasPoint() ? 1 : 0; });
  m.def("stack_selpoint", [&state] { state.Stack(); });
  m.def("drop_selpoint", [&state] { state.Drop(); });
}

RLVM_REGISTER(SiglusBindingRegistry, "selpoint", BindSelpoint)

}  // namespace libsiglus::binding
