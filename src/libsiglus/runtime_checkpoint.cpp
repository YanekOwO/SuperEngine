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

#include "libsiglus/runtime_checkpoint.hpp"

#include "libsiglus/bindings/object.hpp"
#include "systems/event_system.hpp"
#include "utilities/clock.hpp"

namespace libsiglus {

SerializationContext MakeSerializationContext(SiglusRuntime& runtime) {
  SerializationContext context;
  context.system = runtime.system.get();
  context.images = runtime.images.get();
  context.captures = runtime.captures.get();
  context.clock = runtime.system ? runtime.system->event().GetClock()
                                 : std::make_shared<Clock>();
  if (runtime.presenter)
    context.backend = runtime.presenter->backend();
  context.game_root = runtime.base_pth;
  context.local_config = &runtime.local_config;
  context.global_config = &runtime.global_config;
  context.object_setter = [](const std::string& name, int repno) {
    if (repno < 0)
      return binding::RestoreObjectEvent({}, name, 0).setter;
    auto setter = binding::RestoreObjectRepnoEvent({}, name, 0).setter;
    return SerializationContext::ObjectSetter(
        [setter = std::move(setter), repno](ObjectParameter& parameter,
                                            int value) {
          setter(parameter, repno, value);
        });
  };
  return context;
}

SiglusRuntime::CheckpointState SiglusRuntime::CaptureCheckpoint() const {
  CheckpointState saved;
  if (stage)
    saved.stage = stage->CaptureCheckpoint();
  if (counter_list)
    saved.counters = *counter_list;
  for (const auto& entry : counters_)
    if (entry.persistent)
      if (auto counter = entry.counter.lock())
        saved.owned_counters.emplace_back(std::move(counter), entry.owner);
  if (screen_quakes)
    saved.quakes = *screen_quakes;
  if (mask_list)
    saved.masks = *mask_list;
  if (frame_action_manager)
    saved.actions = frame_action_manager->CaptureCheckpoint();
  if (excall && excall->allocated) {
    saved.ex_stage = excall->stage.CaptureCheckpoint();
    saved.ex_counters = *excall->counters;
    saved.ex_actions = excall->actions->CaptureCheckpoint();
  }
  if (images)
    saved.images = images->CaptureCheckpoint();
  if (system) {
    saved.text = &system->text();
    saved.sound = &system->sound();
  }
  if (message_back)
    saved.backlog = message_back->CaptureCheckpoint();
  return saved;
}

void SiglusRuntime::CheckpointState::Prepare(const SiglusRuntime& runtime) {
  if ((stage && !runtime.stage) || (ex_stage && !runtime.excall) ||
      (counters && !runtime.counter_list) ||
      (quakes && !runtime.screen_quakes) || (masks && !runtime.mask_list) ||
      (actions && !runtime.frame_action_manager) ||
      ((text || sound) && !runtime.system) || (images && !runtime.images) ||
      (backlog && !runtime.message_back))
    throw std::runtime_error(
        "Saved resources require unavailable runtime subsystem");
  if (static_cast<bool>(ex_stage) != static_cast<bool>(ex_counters) ||
      static_cast<bool>(ex_stage) != static_cast<bool>(ex_actions))
    throw std::runtime_error("Incomplete saved EX resource state");
  for (const auto& [counter, owner] : owned_counters)
    if (!counter || owner < 0 || owner > 1)
      throw std::runtime_error("Invalid saved counter owner");
  if (images)
    runtime.images->PrepareCheckpoint(*images);
  if (text && (text != &runtime.system->text() || !commit_text_))
    throw std::runtime_error(
        "Saved text has not been prepared for this runtime");
  if (sound && (sound != &runtime.system->sound() || !commit_sound_))
    throw std::runtime_error(
        "Saved sound has not been prepared for this runtime");
}

void SiglusRuntime::CheckpointState::Commit(SiglusRuntime& runtime) {
  if (stage)
    *runtime.stage = std::move(*stage);
  if (ex_stage) {
    runtime.excall->stage = std::move(*ex_stage);
    *runtime.excall->counters = std::move(*ex_counters);
    runtime.excall->actions->RestoreCheckpoint(std::move(*ex_actions));
  }
  if (counters)
    *runtime.counter_list = std::move(*counters);
  runtime.counters_.clear();
  for (auto& [counter, owner] : owned_counters)
    runtime.RegisterCounter(std::move(counter), owner);
  if (quakes)
    *runtime.screen_quakes = std::move(*quakes);
  if (masks)
    *runtime.mask_list = std::move(*masks);
  if (actions)
    runtime.frame_action_manager->RestoreCheckpoint(std::move(*actions));
  if (images)
    runtime.images->RestoreCheckpoint(std::move(*images));
  if (runtime.captures)
    runtime.captures->Reset();
  if (commit_text_)
    std::exchange(commit_text_, {})();
  if (commit_sound_)
    std::exchange(commit_sound_, {})();
  if (backlog)
    runtime.message_back->RestoreCheckpoint(std::move(*backlog));
}

}  // namespace libsiglus
