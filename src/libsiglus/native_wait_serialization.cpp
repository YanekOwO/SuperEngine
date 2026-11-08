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

#include "libsiglus/native_wait_serialization.hpp"

#include "core/counter.hpp"
#include "core/effect.hpp"
#include "core/mask.hpp"
#include "core/object.hpp"
#include "core/object_internal/drawer/movie.hpp"
#include "core/quake.hpp"
#include "libsiglus/execution_serialization.hpp"
#include "libsiglus/native_operation_serialization.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"
#include "vm/promise.hpp"
#include "vm/vm.hpp"

#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace libsiglus {
namespace {

std::shared_ptr<lop::NativeWaitProducer> PreparePolling(
    SiglusMachine& machine,
    ExecutionReader& execution,
    const NativeWaitState& record) {
  const auto& description = std::get<PollingWaitDescription>(record.value);
  auto* system = machine.host().system.get();
  std::function<bool()> done;
  std::function<void()> on_key;
  using Kind = PollingWaitDescription::Kind;
  switch (description.kind) {
    case Kind::Counter:
      if (!description.counter && description.value > 0)
        throw std::runtime_error("Saved counter wait has no counter");
      done = [description, system] {
        return (description.fast_forward && system &&
                system->ShouldFastForward()) ||
               (description.counter &&
                description.counter->Get() >= description.value);
      };
      break;
    case Kind::MaskEvent:
      if (description.parameter < 0 || description.parameter > 1 ||
          description.index < 0)
        throw std::runtime_error("Invalid saved mask wait");
      done = [&machine, description] {
        return !machine.host()
                    .mask_list->At(description.index)
                    .IsMutatorRunning(description.parameter == 0
                                          ? &Mask::Parameters::x
                                          : &Mask::Parameters::y);
      };
      break;
    case Kind::ObjectEvent:
    case Kind::ObjectEvents:
    case Kind::ObjectMovie:
      if (!description.object)
        throw std::runtime_error("Saved object wait has no object");
      done = [description] {
        if (description.kind == Kind::ObjectEvent)
          return !description.object->IsMutatorRunningMatching(
              description.index, description.name);
        if (description.kind == Kind::ObjectEvents)
          return description.object->CountMutators() == 0;
        const auto* movie = description.object->GetDrawer<ObjectMovieData>();
        return !movie || !movie->CheckMovie();
      };
      break;
    case Kind::EffectEvent:
      if (!description.effect)
        throw std::runtime_error("Saved effect wait has no effect");
      done = [description] {
        return !description.effect->IsEventRunning(description.parameter);
      };
      on_key = [description] {
        description.effect->EndEvent(description.parameter);
      };
      break;
    case Kind::Quake:
      if (!description.quake)
        throw std::runtime_error("Saved quake wait has no quake");
      done = [description, system] {
        if (system && system->ShouldFastForward()) {
          description.quake->End(0);
          return true;
        }
        return !description.quake->IsActive();
      };
      on_key = [description] { description.quake->End(0); };
      break;
    case Kind::Voice:
      done = [system] { return !system || !system->sound().KoePlaying(); };
      break;
    case Kind::SoundEffect:
      done = [system] {
        return !system || system->ShouldFastForward() ||
               system->sound().CheckSe() != SoundSystem::kSePlaying;
      };
      break;
    case Kind::Bgm:
      done = [system] {
        const bool finished = !system || system->ShouldFastForward() ||
                              !system->sound().BgmPlaying();
        return finished;
      };
      break;
    case Kind::Pcm:
      done = [system, description] {
        const bool finished =
            !system ||
            (description.fast_forward && system->ShouldFastForward()) ||
            !system->sound().WavPlaying(description.index);
        return finished;
      };
      break;
    default:
      throw std::runtime_error("Unknown saved native wait kind");
  }
  auto scope =
      DescribePollingWait(machine.WaitScope(record.owner), description);
  return lop::RestorePollingWait(*machine.vm, execution.Promise(record.promise),
                                 std::move(done), record.key_skip,
                                 system ? system->event_ptr().get() : nullptr,
                                 std::chrono::milliseconds(record.interval),
                                 std::move(on_key), std::move(scope));
}

}  // namespace

lop::NativeWaitScope DescribePollingWait(lop::NativeWaitScope scope,
                                         PollingWaitDescription description) {
  scope.polling =
      std::make_shared<PollingWaitDescription>(std::move(description));
  return scope;
}

std::vector<NativeWaitState> CaptureNativeWaits(SiglusMachine& machine,
                                                ExecutionWriter& execution) {
  std::erase_if(machine.restored_native_waits, [](const auto& producer) {
    return producer->Completion()->HasResult();
  });
  std::vector<NativeWaitState> records;
  std::set<const lop::NativeWaitProducer*> seen;
  std::erase_if(machine.native_waits,
                [](const auto& weak) { return weak.expired(); });
  for (const auto& weak : machine.native_waits) {
    const auto producer = weak.lock();
    if (producer && !producer->Completion()->HasResult() &&
        seen.insert(producer.get()).second)
      records.push_back(producer->Capture(execution));
  }
  return records;
}

void PrepareNativeWaits(SiglusMachine& machine,
                        ExecutionReader& execution,
                        const std::vector<NativeWaitState>& states) {
  for (const auto& state : states) {
    if (!state.promise || state.interval < 0)
      throw std::runtime_error("Invalid saved native producer");
    if (std::holds_alternative<PollingWaitDescription>(state.value)) {
      machine.restored_native_waits.push_back(
          PreparePolling(machine, execution, state));
    } else {
      auto operation = RestoreNativeOperation(
          machine, execution, std::get<NativeOperationState>(state.value));
      machine.restored_native_waits.push_back(
          lop::FutureBackedCoroutineTask::Restore(
              *machine.vm, execution.Promise(state.promise),
              std::move(operation),
              machine.host().system ? machine.host().system->event_ptr().get()
                                    : nullptr,
              machine.WaitScope(state.owner), state.started));
    }
  }
}

void ActivateNativeWaits(SiglusMachine& machine) {
  for (const auto& producer : machine.restored_native_waits)
    if (!producer->Completion()->HasResult())
      producer->Activate(machine.WaitScope(producer->Owner()));
}

}  // namespace libsiglus
