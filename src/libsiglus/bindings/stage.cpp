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

#include "libsiglus/bindings/bootstrap.hpp"
#include "libsiglus/bindings/util.hpp"

namespace libsiglus::binding {
namespace sb = srbind;

void BindStage(SiglusMachine& runtime) {
  auto& vm = *runtime.vm;

  std::string src = std::format(kLazyArrayClass, "LazyArray");
  src += R"(
class StageLayer {
  fn __init__(self, layer = 0) {
    self.object = nil;
    try {
      self.object = ObjectArray(layer);
    } catch (e) {
      print("Siglus stage.object binding unavailable:", e);
    }

    self.mwnd = nil;
    try {
      self.mwnd = LazyArray(Mwnd);
    } catch (e) {
      print("Siglus stage.mwnd binding unavailable:", e);
    }

    self.objgroup = nil;
    try {
      self.objgroup = GroupArray(layer);
    } catch (e) {
      print("Siglus stage.objgroup binding unavailable:", e);
    }

    self.btnsel = nil;
    try {
      self.btnsel = LazyArray(Btnset);
    } catch (e) {
      print("Siglus stage.btnsel binding unavailable:", e);
    }

    self.world = nil;
    try {
      self.world = LazyArray(World);
    } catch (e) {
      print("Siglus stage.world binding unavailable:", e);
    }

    self.effect = EffectArray(layer);

    self.quake = nil;
    try {
      self.quake = QuakeArray(layer);
    } catch (e) {
      print("Siglus stage.quake binding unavailable:", e);
    }
  }
}

class Stage {
  fn __init__(self) {
    self.back = StageLayer(1);
    self.front = StageLayer(0);
    self.next = StageLayer(2);
    self.storage = [ self.back, self.front, self.next ];
  }
  fn __getitem__(self, idx) { return self.storage[idx]; }
}

stage = Stage();
stage_back = stage.back;
stage_front = stage.front;
stage_next = stage.next;
)";

  Execute(vm, std::move(src));
  // TODO: Implement actual Mwnd, Group, Btnsel, and World classes
}

RLVM_REGISTER(SiglusBindingRegistry, "2_stage", BindStage)

}  // namespace libsiglus::binding
