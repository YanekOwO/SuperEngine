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
#include "long_operations/siglus_wait.hpp"
#include "srbind/srbind.hpp"
#include "vm/exception.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace libsiglus::binding {
namespace sb = srbind;
namespace sr = serilang;

struct EffectHandle {
  std::shared_ptr<Effect> effect;
};

struct EffectEventHandle {
  std::shared_ptr<Effect> effect;
  int property;
};

void BindEffect(SiglusMachine& runtime) {
  auto& vm = *runtime.vm;
  Stage* stage = runtime.stage.get();
  if (!stage)
    throw std::runtime_error("Effect requires a stage");
  sb::module_ m(vm.gc_.get(), vm.globals_.get());

  sb::class_<EffectEventHandle> event(m, "EffectEvent", false);
  for (const bool real_time : {false, true}) {
    auto make_counter = [&runtime, real_time] {
      auto counter = runtime.CreateCounter();
      if (real_time)
        counter->StartReal();
      else
        counter->StartGame();
      return counter;
    };
    event.def(
        real_time ? "set_real" : "set",
        [make_counter](EffectEventHandle* h, std::vector<sr::Value> raw) {
          const CallPacket packet = CallPacket::DecodeFrom(std::move(raw));
          if (packet.args.size() != 4)
            throw sr::RuntimeError("effect event set expects 4 arguments");
          const int value = RequireInt(packet.args[0], "effect event value");
          const int duration =
              RequireInt(packet.args[1], "effect event duration");
          const int delay = RequireInt(packet.args[2], "effect event delay");
          const int speed = RequireInt(packet.args[3], "effect event speed");
          ForEachKeywordId(packet.kwargs, [&](int id, const sr::Value& value) {
            if (id == 0)
              h->effect->Set(h->property,
                             RequireInt(value, "effect event initial value"));
          });
          const int start = h->effect->Get(h->property);
          h->effect->Set(h->property, value);
          h->effect->StartEvent(h->property,
                                MakeFrameCounter(duration, delay, start, value,
                                                 speed, make_counter()));
        },
        sb::vararg);
    auto repeating = [make_counter](InterpolationType type) {
      return [make_counter, type](EffectEventHandle* h, int start, int end,
                                  int duration, int delay, int speed) {
        h->effect->StartEvent(
            h->property, MakeRepeatingFrameCounter(type, duration, delay, start,
                                                   end, speed, make_counter()));
      };
    };
    event.def(real_time ? "loop_real" : "loop",
              repeating(InterpolationType::Loop), sb::arg("start"),
              sb::arg("end"), sb::arg("duration"), sb::arg("delay"),
              sb::arg("speed"));
    event.def(real_time ? "turn_real" : "turn",
              repeating(InterpolationType::Turn), sb::arg("start"),
              sb::arg("end"), sb::arg("duration"), sb::arg("delay"),
              sb::arg("speed"));
  }
  event.def("end",
            [](EffectEventHandle* h) { h->effect->EndEvent(h->property); });
  event.def("check", [](EffectEventHandle* h) -> int {
    return h->effect->IsEventRunning(h->property);
  });
  auto wait = [system = runtime.system.get()](bool key_skip) {
    return [system, key_skip](EffectEventHandle* h, sr::VM& vm,
                              std::vector<sr::Value>) {
      auto done = [effect = h->effect, property = h->property] {
        return !effect->IsEventRunning(property);
      };
      auto on_key = [effect = h->effect, property = h->property] {
        effect->EndEvent(property);
      };
      return lop::MakePollingWaitFuture(
          vm, std::move(done), key_skip,
          system ? system->event_ptr().get() : nullptr,
          std::chrono::milliseconds(5), std::move(on_key));
    };
  };
  event.def("wait", wait(false), sb::vararg);
  event.def("wait_key", wait(true), sb::vararg);

  sb::class_<EffectHandle> effect(m, "Effect");
  effect.add_gc_root(event);
  effect.def(sb::init([stage](int layer, int index) {
               if (index < 0)
                 throw std::out_of_range("Negative stage effect index");
               std::shared_ptr<Effect> e = stage->effects.Get(layer, index);
               return std::make_unique<EffectHandle>(e);
             }),
             sb::arg("layer") = static_cast<int>(kLayerFg),
             sb::arg("index") = 0);
  effect.def("init", [](EffectHandle* h) { h->effect->Reset(); });
  for (int i = 0; i <= 34; ++i) {
    try {
      const std::string name(Effect::GetPropertyName(i));

      // register property
      effect.prop(
          name.c_str(), [i](EffectHandle* h) { return h->effect->Get(i); },
          [i](EffectHandle* h, int value) { h->effect->Set(i, value); });

      // register eve event
      if (i < 14) {
        const std::string event_name = name + "_eve";
        effect.prop_r(event_name.c_str(), [event, i](EffectHandle* h) mutable {
          return sr::Value(event.make_inst(h->effect, i));
        });
      }
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
      [stage, effect](EffectListHandle* h, int index) mutable {
        std::shared_ptr<Effect> e = stage->effects.Get(h->layer, index);
        sr::NativeInstance* inst = effect.make_inst(e);
        return sr::Value(inst);
      },
      sb::arg("index"));
  effect_array.def("size", [stage](EffectListHandle* h) -> int {
    return stage->effects[h->layer].size();
  });
  effect_array.def(
      "resize",
      [stage](EffectListHandle* h, int size) {
        return stage->effects[h->layer].resize(size);
      },
      sb::arg("size"));

  struct ScreenEffectListHandle {};
  sb::class_<ScreenEffectListHandle> screen_effect_array(m,
                                                         "ScreenEffectArray");
  screen_effect_array.add_gc_root(effect);
  screen_effect_array.def(
      sb::init([] { return std::make_unique<ScreenEffectListHandle>(); }));
  screen_effect_array.def(
      "__getitem__",
      [stage, effect](ScreenEffectListHandle*, int index) mutable {
        return sr::Value(effect.make_inst(stage->GetScreenEffect(index)));
      },
      sb::arg("index"));
  screen_effect_array.def("size", [stage](ScreenEffectListHandle*) -> int {
    return stage->screen_effects.size();
  });
  screen_effect_array.def(
      "resize",
      [stage](ScreenEffectListHandle*, int size) {
        stage->ResizeScreenEffects(size);
      },
      sb::arg("size"));
}

RLVM_REGISTER(SiglusBindingRegistry, "0_effect", BindEffect);

}  // namespace libsiglus::binding
