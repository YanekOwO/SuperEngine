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

#include "core/quake.hpp"
#include "libsiglus/bindings/bootstrap.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/bindings/wait_helpers.hpp"
#include "srbind/srbind.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"
#include "vm/list.hpp"
#include "vm/value.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace libsiglus::binding {
namespace sb = srbind;
namespace sr = serilang;

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

class SiglusQuake {
 public:
  SiglusQuake(std::weak_ptr<QuakeState> state, System* system)
      : state_(std::move(state)), system_(system) {}

  QuakeState& Resolve() const {
    std::shared_ptr<QuakeState> state = state_.lock();
    if (!state)
      throw std::runtime_error("Quake element has been deallocated");
    return *state;
  }

  int Check() const {
    auto state = state_.lock();
    return state ? state->Check() : 0;
  }

  void Refresh() const {
    if (system_)
      system_->graphics().ForceRefresh();
  }

  void Start(QuakeState::StartParams params) {
    if (Resolve().Start(params))
      Refresh();
  }

  sr::Value Wait(sr::VM& vm, bool key_skip) {
    auto done = [state = state_, system = system_]() {
      std::shared_ptr<QuakeState> quake = state.lock();
      if (!quake)
        return true;
      if (system && system->ShouldFastForward()) {
        quake->End(0);
        system->graphics().ForceRefresh();
        return true;
      }
      return !quake->IsActive();
    };
    auto on_key = [state = state_, system = system_]() {
      if (std::shared_ptr<QuakeState> quake = state.lock())
        quake->End(0);
      if (system)
        system->graphics().ForceRefresh();
    };
    return MakePollingWaitFuture(vm, std::move(done), key_skip,
                                 system_ ? system_->event_ptr().get() : nullptr,
                                 std::chrono::milliseconds(5),
                                 std::move(on_key));
  }

  std::weak_ptr<QuakeState> state_;
  System* system_ = nullptr;
};

void BindScreen(SiglusMachine& runtime) {
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
  Stage* stage = runtime.stage.get();
  System* system = runtime.system.get();
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  sb::class_<SiglusQuake> quake(m, "Quake");
  quake
      .def(sb::init([screen_quakes, stage, system](int layer, int index) {
        std::weak_ptr<QuakeState> state;
        if (layer == -1) {
          state = screen_quakes->GetHandle(index);
        } else {
          if (!stage)
            throw std::runtime_error("Stage quake requires a stage");
          state = stage->GetQuakeHandle(layer, index);
        }
        return new SiglusQuake(std::move(state), system);
      }))
      .def(
          "start",
          [](SiglusQuake* quake, std::vector<sr::Value> args) {
            quake->Start(ParseParams(std::move(args), false));
          },
          sb::vararg)
      .def(
          "start_wait",
          [](SiglusQuake* quake, sr::VM& vm, std::vector<sr::Value> args) {
            quake->Start(ParseParams(std::move(args), false));
            return quake->Wait(vm, false);
          },
          sb::vararg)
      .def(
          "start_wait_key",
          [](SiglusQuake* quake, sr::VM& vm, std::vector<sr::Value> args) {
            quake->Start(ParseParams(std::move(args), false));
            return quake->Wait(vm, true);
          },
          sb::vararg)
      .def(
          "start_nowait",
          [](SiglusQuake* quake, std::vector<sr::Value> args) {
            quake->Start(ParseParams(std::move(args), false));
          },
          sb::vararg)
      .def(
          "start_all",
          [](SiglusQuake* quake, std::vector<sr::Value> args) {
            quake->Start(ParseParams(std::move(args), true));
          },
          sb::vararg)
      .def(
          "start_all_wait",
          [](SiglusQuake* quake, sr::VM& vm, std::vector<sr::Value> args) {
            quake->Start(ParseParams(std::move(args), true));
            return quake->Wait(vm, false);
          },
          sb::vararg)
      .def(
          "start_all_wait_key",
          [](SiglusQuake* quake, sr::VM& vm, std::vector<sr::Value> args) {
            quake->Start(ParseParams(std::move(args), true));
            return quake->Wait(vm, true);
          },
          sb::vararg)
      .def(
          "start_all_nowait",
          [](SiglusQuake* quake, std::vector<sr::Value> args) {
            quake->Start(ParseParams(std::move(args), true));
          },
          sb::vararg)
      .def(
          "end",
          [](SiglusQuake* quake, std::vector<sr::Value> args) {
            CallPacket packet = CallPacket::DecodeFrom(std::move(args));
            const int duration =
                packet.args.empty()
                    ? 0
                    : RequireInt(packet.args[0], "quake.end duration");
            quake->Resolve().End(duration);
            quake->Refresh();
          },
          sb::vararg)
      .def(
          "wait",
          [](SiglusQuake* quake, sr::VM& vm, std::vector<sr::Value>) {
            return quake->Wait(vm, false);
          },
          sb::vararg)
      .def(
          "wait_key",
          [](SiglusQuake* quake, sr::VM& vm, std::vector<sr::Value>) {
            return quake->Wait(vm, true);
          },
          sb::vararg)
      .def(
          "check",
          [](SiglusQuake* quake, std::vector<sr::Value>) {
            return quake->Check();
          },
          sb::vararg);

  std::string src = std::format(kLazyArrayClass, "__SiglusLazyArray");
  src += R"(
class __SiglusScreenQuakeArray {
  fn __init__(self){ self.storage = []; }
  fn __getitem__(self, idx){
    while(self.storage.len() <= idx) self.storage.append(nil);
    if(self.storage[idx] == nil) self.storage[idx] = Quake(-1, idx);
    return self.storage[idx];
  }
}

class Screen {
  fn __init__(self){
    self.quake = __SiglusScreenQuakeArray();
    self.effect = nil;
    try{ self.effect = __SiglusLazyArray(Effect); }
    catch(e){ print("Siglus screen.effect binding unavailable:", e); }
  }
}

screen = Screen();
)";
  Execute(vm, std::move(src));

  runtime.reset_local_memory.emplace_back(
      [screen_quakes] { screen_quakes->Reset(); });
}

RLVM_REGISTER(SiglusBindingRegistry, "1_screen", BindScreen)

}  // namespace libsiglus::binding
