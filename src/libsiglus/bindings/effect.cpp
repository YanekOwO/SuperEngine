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

#include "core/stage.hpp"
#include "core/stage_effect.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "srbind/srbind.hpp"

#include <stdexcept>

namespace libsiglus::binding {
namespace sb = srbind;

using EffectHandle = StageElement<StageEffect>::Handle;
static StageEffect& ResolveEffect(const EffectHandle& handle) {
  return StageElement<StageEffect>::Resolve(handle);
}

void BindEffect(SiglusMachine& runtime) {
  auto& vm = *runtime.vm;
  Stage* stage_state = runtime.stage.get();

  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  m.def("__stage_effect_size", [stage_state](int layer) {
    return static_cast<int>(stage_state->EffectsForLayer(layer).size());
  });
  m.def("__stage_effect_resize", [stage_state](int layer, int size) {
    stage_state->ResizeEffects(layer, size);
  });

  sb::class_<EffectHandle> effect(m, "Effect");
  effect.def(sb::init([stage_state](int layer, int index) -> EffectHandle* {
               if (!stage_state)
                 throw std::runtime_error("Effect requires a stage");
               if (index < 0)
                 throw std::out_of_range("Negative stage effect index");
               return new EffectHandle(stage_state->effects.GetHandle(
                   layer, static_cast<std::size_t>(index)));
             }),
             sb::arg("layer") = static_cast<int>(kLayerFg),
             sb::arg("index") = 0);
  effect.def("init",
             [](EffectHandle* handle) { ResolveEffect(*handle).Reset(); });
  for (int i = 0; i <= 34; ++i) {
    try {
      const std::string name(StageEffect::GetPropertyName(i));
      const std::string setter_name = "set_" + name;
      effect.def(name.c_str(), [i](EffectHandle* handle) {
        return ResolveEffect(*handle).Get(i);
      });
      effect.def(setter_name.c_str(), [i](EffectHandle* handle, int value) {
        ResolveEffect(*handle).Set(i, value);
      });
    } catch (const std::runtime_error&) {
    }
  }

  std::string src = R"(
class EffectArray {
  fn __init__(self, layer) { self.layer = layer; }
  fn __getitem__(self, idx) { return Effect(self.layer, idx); }
  fn size(self) { return __stage_effect_size(self.layer); }
  fn resize(self, size) { __stage_effect_resize(self.layer, size); }
}
)";

  Execute(vm, std::move(src));
}

RLVM_REGISTER(SiglusBindingRegistry, "0_effect", BindEffect);

}  // namespace libsiglus::binding
