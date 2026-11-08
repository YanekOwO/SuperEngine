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

#include "libsiglus/bindings/effect.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "vm/serialization.hpp"

#include "core/effect.hpp"
#include "core/stage.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/native_wait_serialization.hpp"
#include "libsiglus/siglus_runtime.hpp"
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

void BindEffect(SiglusMachine& runtime) {
  auto& vm = *runtime.vm;
  Stage* stage = runtime.host().stage.get();
  if (!stage)
    throw std::runtime_error("Effect requires a stage");
  sb::module_ m(vm.gc_.get(), vm.globals_.get());

  sb::class_<EffectEventHandle> event(m, "EffectEvent", false);
  RegisterBindingClass(runtime, event);
  event.def_serialization(
      [](const EffectEventHandle& h, serilang::ObjectWriter& out) {
        out.Put(h.effect);
        out.Put(h.property);
        out.Put(h.owner);
      },
      [](serilang::ObjectReader& in) {
        auto effect = in.Get<std::shared_ptr<Effect>>();
        auto property = in.Get<int>();
        auto owner = in.Get<int>();
        return std::make_unique<EffectEventHandle>(effect, property, owner);
      });

  for (const bool real_time : {false, true}) {
    auto make_counter = [&runtime, real_time](int owner) {
      auto counter = runtime.host().CreateCounter(owner);
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
          h->effect->StartEvent(
              h->property, MakeFrameCounter(duration, delay, start, value,
                                            speed, make_counter(h->owner)));
        },
        sb::vararg);
    auto repeating = [make_counter](InterpolationType type) {
      return [make_counter, type](EffectEventHandle* h, int start, int end,
                                  int duration, int delay, int speed) {
        h->effect->StartEvent(
            h->property,
            MakeRepeatingFrameCounter(type, duration, delay, start, end, speed,
                                      make_counter(h->owner)));
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
  auto wait = [&runtime, system = runtime.host().system.get()](bool key_skip) {
    return [&runtime, system, key_skip](EffectEventHandle* h, sr::VM& vm,
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
          std::chrono::milliseconds(5), std::move(on_key),
          DescribePollingWait(
              runtime.WaitScope(h->owner),
              {.kind = PollingWaitDescription::Kind::EffectEvent,
               .effect = h->effect,
               .parameter = h->property}));
    };
  };
  event.def("wait", wait(false), sb::vararg);
  event.def("wait_key", wait(true), sb::vararg);

  sb::class_<EffectHandle> effect(m, "Effect");
  RegisterBindingClass(runtime, effect);
  effect.def_serialization(
      [](const EffectHandle& h, serilang::ObjectWriter& out) {
        out.Put(h.effect);
        out.Put(h.owner);
      },
      [](serilang::ObjectReader& in) {
        auto effect = in.Get<std::shared_ptr<Effect>>();
        auto owner = in.Get<int>();
        return std::make_unique<EffectHandle>(effect, owner);
      });

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
          return sr::Value(event.make_inst(h->effect, i, h->owner));
        });
      }
    } catch (const std::runtime_error&) {
    }
  }

  sb::class_<EffectListHandle> effect_array(m, "EffectArray");
  RegisterBindingClass(runtime, effect_array);
  effect_array.def_serialization(
      [](const EffectListHandle& h, serilang::ObjectWriter& out) {
        out.Put(h.layer);
        out.Put(h.owner);
      },
      [](serilang::ObjectReader& in) {
        auto layer = in.Get<int>();
        auto owner = in.Get<int>();
        return std::make_unique<EffectListHandle>(layer, owner);
      });

  effect_array.add_gc_root(effect);
  effect_array.def(
      sb::init([](int layer, int owner) {
        if (layer != kLayerFg && layer != kLayerBg && layer != kLayerNext)
          throw sr::RuntimeError("invalid effect layer: " +
                                 std::to_string(layer));
        return std::make_unique<EffectListHandle>(layer, owner);
      }),
      sb::arg("layer"), sb::arg("owner") = 0);
  effect_array.def(
      "__getitem__",
      [&runtime, effect](EffectListHandle* h, int index) mutable {
        std::shared_ptr<Effect> e =
            runtime.host().GetStage(h->owner).effects.Get(h->layer, index);
        sr::NativeInstance* inst = effect.make_inst(e, h->owner);
        return sr::Value(inst);
      },
      sb::arg("index"));
  effect_array.def("size", [&runtime](EffectListHandle* h) -> int {
    return runtime.host().GetStage(h->owner).effects[h->layer].size();
  });
  effect_array.def(
      "resize",
      [&runtime](EffectListHandle* h, int size) {
        return runtime.host().GetStage(h->owner).effects[h->layer].resize(size);
      },
      sb::arg("size"));

  sb::class_<ScreenEffectListHandle> screen_effect_array(m,
                                                         "ScreenEffectArray");
  RegisterBindingClass(runtime, screen_effect_array);
  screen_effect_array.def_serialization(
      [](const ScreenEffectListHandle& h, serilang::ObjectWriter& out) {},
      [](serilang::ObjectReader& in) {
        return std::make_unique<ScreenEffectListHandle>();
      });

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
