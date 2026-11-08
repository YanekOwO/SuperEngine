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

#include "libsiglus/save_load.hpp"
#include "srbind/srbind.hpp"

namespace libsiglus::binding {

void BindSelpoint(SiglusMachine& machine) {
  auto& host = machine.host();
  srbind::module_ m(machine.vm->gc_.get(), machine.vm->globals_.get());
  m.def("selpoint",
        [&host] { host.Saves().manager().selection_points().Mark(); });
  m.def("clear_selpoint",
        [&host] { host.Saves().manager().selection_points().Clear(); });
  m.def("check_selpoint", [&host] {
    return host.Saves().manager().selection_points().HasPoint() ? 1 : 0;
  });
  m.def("stack_selpoint",
        [&host] { host.Saves().manager().selection_points().Stack(); });
  m.def("drop_selpoint",
        [&host] { host.Saves().manager().selection_points().Drop(); });
}

RLVM_REGISTER(SiglusBindingRegistry, "selpoint", BindSelpoint)

}  // namespace libsiglus::binding
