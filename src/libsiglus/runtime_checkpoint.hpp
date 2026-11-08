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

#pragma once

#include "core/frame_action_manager.hpp"
#include "core/image_resources.hpp"
#include "core/mask.hpp"
#include "core/message_back.hpp"
#include "core/selection.hpp"
#include "core/serialization_context.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "systems/sound_system.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"
#include "utilities/serialization.hpp"

namespace libsiglus {

SerializationContext MakeSerializationContext(SiglusRuntime& runtime);

// This is a composition of owner-defined state, not an alternate archive.
// The surrounding Boost archive tracks all pointers, including VM attachments.
struct SiglusRuntime::CheckpointState {
  std::optional<Stage> stage, ex_stage;
  std::optional<CounterList> counters, ex_counters;
  std::vector<std::pair<std::shared_ptr<Counter>, int>> owned_counters;
  std::optional<QuakeList> quakes;
  std::optional<MaskList> masks;
  std::optional<FrameActionManager::CheckpointState> actions, ex_actions;
  std::optional<ImageResources::CheckpointState> images;
  // Borrowed while saving; loading retains installation until Commit().
  TextSystem* text = nullptr;
  SoundSystem* sound = nullptr;
  std::optional<MessageBack::CheckpointState> backlog;

  void Prepare(const SiglusRuntime& runtime);
  void Commit(SiglusRuntime& runtime);

 private:
  SerializationContext::Commit commit_text_, commit_sound_;
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) {
    ar & stage & ex_stage & counters & ex_counters & owned_counters & quakes &
        masks & actions & ex_actions & images;
    bool has_text = text != nullptr;
    ar & has_text;
    if constexpr (std::decay_t<decltype(ar)>::is_loading::value) {
      text = nullptr;
      commit_text_ = {};
      if (has_text) {
        auto& context = SerializationContext::Get(ar);
        if (!context.system)
          throw std::runtime_error("Saved text requires a text system");
        text = &context.system->text();
        struct ReceiverGuard {
          SerializationContext& context;
          SerializationContext::DeferCommit previous;
          ~ReceiverGuard() { context.defer_commit = std::move(previous); }
        } guard{context, std::move(context.defer_commit)};
        context.defer_commit = [this](SerializationContext::Commit commit) {
          commit_text_ = std::move(commit);
        };
        ar&(*text);
      }
    } else if (has_text) {
      ar&(*text);
    }
    bool has_sound = sound != nullptr;
    ar & has_sound;
    if constexpr (std::decay_t<decltype(ar)>::is_loading::value) {
      sound = nullptr;
      commit_sound_ = {};
      if (has_sound) {
        auto& context = SerializationContext::Get(ar);
        if (!context.system)
          throw std::runtime_error("Saved sound requires a sound system");
        sound = &context.system->sound();
        struct ReceiverGuard {
          SerializationContext& context;
          SerializationContext::DeferCommit previous;
          ~ReceiverGuard() { context.defer_commit = std::move(previous); }
        } guard{context, std::move(context.defer_commit)};
        context.defer_commit = [this](SerializationContext::Commit commit) {
          commit_sound_ = std::move(commit);
        };
        ar&(*sound);
      }
    } else if (has_sound) {
      ar&(*sound);
    }
    ar & backlog;
  }
};

}  // namespace libsiglus
