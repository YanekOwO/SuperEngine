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

#include "srbind/srbind.hpp"

#include "libsiglus/bindings/bootstrap.hpp"
#include "libsiglus/bindings/util.hpp"

namespace libsiglus::binding {

void BindStage(SiglusMachine& runtime) {
  auto& vm = *runtime.vm;

  std::string src = std::format(kLazyArrayClass, "LazyArray");
  src += R"(
class StageLayer {
  fn __init__(self, layer = 0, owner = 0) {
    self.object = ObjectArray(layer, owner);
    self.mwnd = MwndArray(layer, owner);
    self.objgroup = GroupArray(layer, owner);

    self.btnsel = nil;
    try {
      self.btnsel = LazyArray(Btnsel);
    } catch (e) {
      print("Siglus stage.btnsel binding unavailable:", e);
    }

    self.world = nil;
    try {
      self.world = LazyArray(World);
    } catch (e) {
      print("Siglus stage.world binding unavailable:", e);
    }

    self.effect = EffectArray(layer, owner);
    self.quake = QuakeArray(layer, owner);
  }
  fn create_mwnd(self, count) { self.mwnd.resize(count); }
}

class Stage {
  fn __init__(self, owner = 0) {
    self.back = StageLayer(1, owner);
    self.front = StageLayer(0, owner);
    self.next = StageLayer(2, owner);
    self.storage = [ self.back, self.front, self.next ];
  }
  fn __getitem__(self, idx) { return self.storage[idx]; }
}

stage = Stage();
__excall_stage = Stage(1);
stage_back = stage.back;
stage_front = stage.front;
stage_next = stage.next;
)";

  Execute(vm, std::move(src));
  // TODO: Implement actual Btnsel and World classes
}

RLVM_REGISTER(SiglusBindingRegistry, "2_stage", BindStage)

}  // namespace libsiglus::binding
