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

#include "core/quake.hpp"
#include "core/stage.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/bindings/wait_helpers.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "srbind/srbind.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"
#include "vm/exception.hpp"
#include "vm/list.hpp"
#include "vm/value.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace libsiglus::binding {
namespace sb = srbind;
namespace sr = serilang;

struct QuakeHandle {
  std::shared_ptr<QuakeState> quake;
};

static int ReadArg(const std::vector<sr::Value>& args,
                   std::size_t index,
                   int default_value,
                   std::string_view name) {
  return index < args.size() ? RequireInt(args[index], name) : default_value;
}

static QuakeState::StartParams ParseParams(std::vector<sr::Value> args,
                                           bool all_orders = true) {
  CallPacket packet = CallPacket::DecodeFrom(std::move(args));
  const sr::List* options = nullptr;
  if (!packet.args.empty()) {
    options = packet.args.back().Get_if<sr::List>();
    if (options)
      packet.args.pop_back();
  }

  QuakeState::StartParams params;
  params.type = ReadArg(packet.args, 0, 0, "quake.start type");
  params.period_ms = ReadArg(packet.args, 1, 1000, "quake.start time");
  params.count = ReadArg(packet.args, 2, 0, "quake.start count");
  params.end_count = ReadArg(packet.args, 3, 0, "quake.start end count");
  params.begin_order =
      ReadArg(packet.args, 4, all_orders ? std::numeric_limits<int>::min() : 0,
              "quake.start begin order");
  params.end_order =
      ReadArg(packet.args, 5, all_orders ? std::numeric_limits<int>::max() : 0,
              "quake.start end order");

  auto option = [options](std::size_t index) {
    if (!options || index >= options->items.size())
      return 0;
    return RequireInt(options->items[index], "quake.start option");
  };
  params.power = option(0);
  if (params.type == static_cast<int>(QuakeState::Type::Zoom)) {
    params.center_x = option(1);
    params.center_y = option(2);
  } else {
    params.direction = option(1);
  }
  return params;
}

static void RefreshQuake(System* system) {
  if (system)
    system->graphics().ForceRefresh();
}

static void StartQuake(QuakeHandle* h,
                       System* system,
                       QuakeState::StartParams params) {
  if (h->quake->Start(params))
    RefreshQuake(system);
}

static sr::Value WaitQuake(QuakeHandle* h,
                           System* system,
                           sr::VM& vm,
                           bool key_skip) {
  auto done = [quake = h->quake, system]() {
    if (system && system->ShouldFastForward()) {
      quake->End(0);
      RefreshQuake(system);
      return true;
    }
    return !quake->IsActive();
  };
  auto on_key = [quake = h->quake, system]() {
    quake->End(0);
    RefreshQuake(system);
  };
  return MakePollingWaitFuture(vm, std::move(done), key_skip,
                               system ? system->event_ptr().get() : nullptr,
                               std::chrono::milliseconds(5), std::move(on_key));
}

void BindQuake(SiglusMachine& runtime) {
  auto& vm = *runtime.vm;
  if (!runtime.screen_quakes) {
    int count = runtime.gameexe
                    ? (*runtime.gameexe)("QUAKE.CNT").Int().value_or(16)
                    : 16;
    count = std::clamp(count, 0, 256);
    runtime.screen_quakes =
        std::make_shared<QuakeList>(static_cast<std::size_t>(count));
  }
  std::shared_ptr<QuakeList> screen_quakes = runtime.screen_quakes;
  Stage* stage_state = runtime.stage.get();
  System* system = runtime.system.get();
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  sb::class_<QuakeHandle> quake(m, "Quake");
  quake.def(sb::init([screen_quakes, stage_state](int layer, int index) {
              std::shared_ptr<QuakeState> q;
              if (layer == -1) {
                q = screen_quakes->GetHandle(index);
              } else {
                if (!stage_state)
                  throw std::runtime_error("Stage quake requires a stage");
                q = stage_state->GetQuake(layer, index);
              }
              return std::make_unique<QuakeHandle>(q);
            }),
            sb::arg("layer"), sb::arg("index"));
  quake.def(
      "start",
      [system](QuakeHandle* h, std::vector<sr::Value> args) {
        StartQuake(h, system, ParseParams(std::move(args), false));
      },
      sb::vararg);
  quake.def(
      "start_wait",
      [system](QuakeHandle* h, sr::VM& vm, std::vector<sr::Value> args) {
        StartQuake(h, system, ParseParams(std::move(args), false));
        return WaitQuake(h, system, vm, false);
      },
      sb::vararg);
  quake.def(
      "start_wait_key",
      [system](QuakeHandle* h, sr::VM& vm, std::vector<sr::Value> args) {
        StartQuake(h, system, ParseParams(std::move(args), false));
        return WaitQuake(h, system, vm, true);
      },
      sb::vararg);
  quake.def(
      "start_nowait",
      [system](QuakeHandle* h, std::vector<sr::Value> args) {
        StartQuake(h, system, ParseParams(std::move(args), false));
      },
      sb::vararg);
  quake.def(
      "start_all",
      [system](QuakeHandle* h, std::vector<sr::Value> args) {
        StartQuake(h, system, ParseParams(std::move(args), true));
      },
      sb::vararg);
  quake.def(
      "start_all_wait",
      [system](QuakeHandle* h, sr::VM& vm, std::vector<sr::Value> args) {
        StartQuake(h, system, ParseParams(std::move(args), true));
        return WaitQuake(h, system, vm, false);
      },
      sb::vararg);
  quake.def(
      "start_all_wait_key",
      [system](QuakeHandle* h, sr::VM& vm, std::vector<sr::Value> args) {
        StartQuake(h, system, ParseParams(std::move(args), true));
        return WaitQuake(h, system, vm, true);
      },
      sb::vararg);
  quake.def(
      "start_all_nowait",
      [system](QuakeHandle* h, std::vector<sr::Value> args) {
        StartQuake(h, system, ParseParams(std::move(args), true));
      },
      sb::vararg);
  quake.def(
      "end",
      [system](QuakeHandle* h, std::vector<sr::Value> args) {
        CallPacket packet = CallPacket::DecodeFrom(std::move(args));
        const int duration =
            packet.args.empty()
                ? 0
                : RequireInt(packet.args[0], "quake.end duration");
        h->quake->End(duration);
        RefreshQuake(system);
      },
      sb::vararg);
  quake.def(
      "wait",
      [system](QuakeHandle* h, sr::VM& vm, std::vector<sr::Value>) {
        return WaitQuake(h, system, vm, false);
      },
      sb::vararg);
  quake.def(
      "wait_key",
      [system](QuakeHandle* h, sr::VM& vm, std::vector<sr::Value>) {
        return WaitQuake(h, system, vm, true);
      },
      sb::vararg);
  quake.def(
      "check",
      [](QuakeHandle* h, std::vector<sr::Value>) { return h->quake->Check(); },
      sb::vararg);

  struct QuakeListHandle {
    int layer;
  };
  sb::class_<QuakeListHandle> quake_array(m, "QuakeArray");
  quake_array.add_gc_root(quake);
  quake_array.def(
      sb::init([](int layer) {
        if (layer != kLayerFg && layer != kLayerBg && layer != kLayerNext)
          throw sr::RuntimeError("invalid quake layer: " +
                                 std::to_string(layer));
        return std::make_unique<QuakeListHandle>(layer);
      }),
      sb::arg("layer"));
  quake_array.def(
      "__getitem__",
      [stage_state, quake](QuakeListHandle* h, int index) mutable {
        if (!stage_state)
          throw std::runtime_error("Stage quake requires a stage");
        std::shared_ptr<QuakeState> q = stage_state->GetQuake(h->layer, index);
        sr::NativeInstance* inst = quake.make_inst(q);
        return sr::Value(inst);
      },
      sb::arg("index"));

  struct ScreenQuakeListHandle {
    std::shared_ptr<QuakeList> quakes;
  };
  sb::class_<ScreenQuakeListHandle> screen_quake_array(m, "ScreenQuakeArray");
  screen_quake_array.add_gc_root(quake);
  screen_quake_array.def(sb::init([screen_quakes] {
    return std::make_unique<ScreenQuakeListHandle>(screen_quakes);
  }));
  screen_quake_array.def(
      "__getitem__",
      [quake](ScreenQuakeListHandle* h, int index) mutable {
        std::shared_ptr<QuakeState> q = h->quakes->GetHandle(index);
        sr::NativeInstance* inst = quake.make_inst(q);
        return sr::Value(inst);
      },
      sb::arg("index"));

  runtime.reset_local_memory.emplace_back(
      [screen_quakes] { screen_quakes->Reset(); });
}

RLVM_REGISTER(SiglusBindingRegistry, "0_quake", BindQuake);

}  // namespace libsiglus::binding
