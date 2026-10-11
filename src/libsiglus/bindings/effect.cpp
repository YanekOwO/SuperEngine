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

#include "core/effect.hpp"
#include "core/stage.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "srbind/srbind.hpp"
#include "vm/exception.hpp"

#include <memory>
#include <stdexcept>
#include <string>

namespace libsiglus::binding {
namespace sb = srbind;
namespace sr = serilang;

struct EffectHandle {
  std::shared_ptr<Effect> effect;
};

void BindEffect(SiglusMachine& runtime) {
  auto& vm = *runtime.vm;
  Stage* stage_state = runtime.stage.get();
  if (!stage_state)
    throw std::runtime_error("Effect requires a stage");
  sb::module_ m(vm.gc_.get(), vm.globals_.get());

  sb::class_<EffectHandle> effect(m, "Effect");
  effect.def(
      sb::init([stage_state](int layer, int index) {
        if (index < 0)
          throw std::out_of_range("Negative stage effect index");
        std::shared_ptr<Effect> e = stage_state->effects.Get(layer, index);
        return std::make_unique<EffectHandle>(e);
      }),
      sb::arg("layer") = static_cast<int>(kLayerFg), sb::arg("index") = 0);
  effect.def("init", [](EffectHandle* h) { h->effect->Reset(); });
  for (int i = 0; i <= 34; ++i) {
    try {
      const std::string name(Effect::GetPropertyName(i));
      const std::string setter_name = "set_" + name;
      effect.def(name.c_str(),
                 [i](EffectHandle* h) { return h->effect->Get(i); });
      effect.def(setter_name.c_str(),
                 [i](EffectHandle* h, int value) { h->effect->Set(i, value); });
    } catch (const std::runtime_error&) {
    }
  }

  struct EffectListHandle {
    int layer;
  };
  sb::class_<EffectListHandle> effect_array(m, "EffectArray");
  effect_array.add_gc_root(effect);
  effect_array.def(
      sb::init([](int layer) {
        if (layer != kLayerFg && layer != kLayerBg && layer != kLayerNext)
          throw sr::RuntimeError("invalid effect layer: " +
                                 std::to_string(layer));
        return std::make_unique<EffectListHandle>(layer);
      }),
      sb::arg("layer"));
  effect_array.def(
      "__getitem__",
      [stage_state, effect](EffectListHandle* h, int index) mutable {
        std::shared_ptr<Effect> e = stage_state->effects.Get(h->layer, index);
        sr::NativeInstance* inst = effect.make_inst(e);
        return sr::Value(inst);
      },
      sb::arg("index"));
  effect_array.def("size", [stage_state](EffectListHandle* h) -> int {
    return stage_state->effects[h->layer].size();
  });
  effect_array.def(
      "resize",
      [stage_state](EffectListHandle* h, int size) {
        return stage_state->effects[h->layer].resize(size);
      },
      sb::arg("size"));
}

RLVM_REGISTER(SiglusBindingRegistry, "0_effect", BindEffect);

}  // namespace libsiglus::binding
