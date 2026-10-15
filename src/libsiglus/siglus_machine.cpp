// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
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
//
// -----------------------------------------------------------------------

#include "libsiglus/siglus_machine.hpp"

#include "core/input.hpp"
#include "libsiglus/bindings/frame_action.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>

namespace libsiglus {

SiglusMachine::~SiglusMachine() {
  // Native VM instances and pending coroutine frames keep non-owning pointers
  // to runtime services. Finalize them before member destruction tears those
  // services down.
  vm.reset();
}

void SiglusMachine::Update(std::chrono::milliseconds real_delta,
                           std::chrono::milliseconds game_delta) {
  using ms = std::chrono::milliseconds;
  real_delta = std::max(ms::zero(), real_delta);
  game_delta = std::max(ms::zero(), game_delta);

  const bool time_stopped = local_config.time_stop;
  if (!time_stopped && !local_config.counter_time_stop && counter_list)
    counter_list->Advance(game_delta, real_delta);

  // Advance before callbacks can create new timers. Such timers must not
  // receive the elapsed time from before their creation.
  std::erase_if(counters_,
                [real_delta, game_delta, time_stopped](const auto& ptr) {
                  if (auto counter = ptr.lock()) {
                    if (!time_stopped)
                      counter->Advance(game_delta, real_delta);
                    return false;
                  }
                  return true;
                });

  bool should_refresh = (screen_quakes && screen_quakes->IsActive()) ||
                        (stage && stage->HasActiveQuakes());
  if (!time_stopped && screen_quakes)
    screen_quakes->Advance(game_delta.count());

  if (!time_stopped && !local_config.stage_time_stop && stage)
    stage->AdvanceQuakes(game_delta.count());
  should_refresh |= (screen_quakes && screen_quakes->IsActive()) ||
                    (stage && stage->HasActiveQuakes());

  if (should_refresh)
    system->graphics().ForceRefresh();

  if (frame_action_manager)
    frame_action_manager->AdvanceAndDispatch(real_delta, game_delta);
  if (update_wipe_after_frame_actions)
    update_wipe_after_frame_actions();
}

void SiglusMachine::Run() {
  if (!vm || !system)
    throw std::logic_error("SiglusMachine::Run requires a VM and system");

  using FrameClock = std::chrono::steady_clock;
  constexpr auto frame_period =
      std::chrono::duration_cast<FrameClock::duration>(
          std::chrono::seconds(1)) /
      60;
  constexpr auto minimum_vm_budget =
      std::chrono::duration_cast<FrameClock::duration>(
          std::chrono::milliseconds(1));
  const auto frame_clock = system->event().GetClock();
  auto last_frame_tick = frame_clock->GetTicks();

  while (!vm->IsStopRequested() && !system->IsQuitRequested() &&
         vm->HasPendingWork()) {
    const auto next_frame = FrameClock::now() + frame_period;
    input_event_listener->ResetState();
    system->Run([this, &last_frame_tick, &frame_clock] {
      interaction_manager->Update();

      const auto now = frame_clock->GetTicks();
      const auto real_delta =
          std::max(now - last_frame_tick, Clock::duration_t::zero());
      last_frame_tick = now;

      const bool fast_forward = system->ShouldFastForward();
      const auto fast_forward_delta = real_delta > Clock::duration_t::max() / 32
                                          ? Clock::duration_t::max()
                                          : real_delta * 32;
      const auto game_delta = fast_forward ? fast_forward_delta : real_delta;

      Update(real_delta, game_delta);
    });
    if (vm->IsStopRequested() || system->IsQuitRequested())
      break;

    vm->Run(std::max(next_frame - FrameClock::now(), minimum_vm_budget));
    if (vm->IsStopRequested() || system->IsQuitRequested() ||
        !vm->HasPendingWork())
      break;

    std::this_thread::sleep_until(next_frame);
  }

  if (system->IsQuitRequested())
    vm->RequestStop();
}

std::shared_ptr<Counter> SiglusMachine::CreateCounter() {
  auto ret = std::make_shared<Counter>();
  counters_.emplace_back(ret);
  return ret;
}

}  // namespace libsiglus
