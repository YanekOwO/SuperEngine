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
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace libsiglus {

SiglusMachine::~SiglusMachine() {
  // Native VM instances and pending coroutine frames keep non-owning pointers
  // to runtime services. Finalize them before member destruction tears those
  // services down.
  vm.reset();
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

      const auto flag = [this](std::string_view name) {
        return local_config && (*local_config)(name).Int().value_or(0) != 0;
      };
      const std::int64_t real_ms = real_delta.count();
      const bool fast_forward = system->ShouldFastForward();
      const std::int64_t game_ms =
          fast_forward
              ? (real_ms > std::numeric_limits<std::int64_t>::max() / 32
                     ? std::numeric_limits<std::int64_t>::max()
                     : real_ms * 32)
              : real_ms;
      if (!flag("time_stop") && !flag("counter_time_stop"))
        counter_list->Advance(game_ms, real_ms);

      const bool time_stopped = flag("time_stop");
      const bool quakes_were_active =
          (screen_quakes && screen_quakes->IsActive()) ||
          (stage && stage->HasActiveQuakes());
      if (!time_stopped && screen_quakes)
        screen_quakes->Advance(game_ms);
      if (!time_stopped && !flag("stage_time_stop") && stage)
        stage->AdvanceQuakes(game_ms);
      const bool quakes_are_active =
          (screen_quakes && screen_quakes->IsActive()) ||
          (stage && stage->HasActiveQuakes());
      if (quakes_were_active || quakes_are_active)
        system->graphics().ForceRefresh();

      frame_action_manager->AdvanceAndDispatch(real_delta, fast_forward);
      if (update_wipe_after_frame_actions)
        update_wipe_after_frame_actions();
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

}  // namespace libsiglus
