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

#include "libsiglus/bindings/util.hpp"

namespace libsiglus::binding {

void BindScreen(SiglusMachine& runtime) {
  auto& vm = *runtime.vm;

  Execute(vm, R"(
class Screen {
  fn __init__(self){
    self.quake = ScreenQuakeArray();
    self.effect = ScreenEffectArray();
  }
}

screen = Screen();
)");
}

RLVM_REGISTER(SiglusBindingRegistry, "1_screen", BindScreen)

}  // namespace libsiglus::binding
