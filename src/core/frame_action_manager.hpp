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

#include "core/frame_action.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

class GraphicsObject;
class Stage;
struct LocalConfig;

using ActionOwner = std::shared_ptr<GraphicsObject>;

struct ActionReference {
  std::shared_ptr<FrameAction> action;
  ActionOwner owner;
};

class FrameActionManager {
 public:
  // Execute synchronously using the saved target. During a final callback,
  // the action is inactive and end_action is true; arguments remain available.
  using Exec = std::function<void(const ActionReference&, FrameAction::Target)>;

  FrameActionManager(Stage& stage,
                     LocalConfig& local_config,
                     std::size_t global_channel_count);

  inline std::shared_ptr<FrameAction> global_action() {
    return TouchFrameAction(global_action_);
  }
  inline const std::shared_ptr<FrameAction>& global_action() const {
    return global_action_;
  }
  inline FrameActionList& global_channels() { return global_channels_; }
  inline const FrameActionList& global_channels() const {
    return global_channels_;
  }

  // Install execution before dispatching or ending active actions.
  void SetExec(Exec exec);

  void Start(ActionReference reference,
             int duration,
             bool real_time,
             FrameAction::Target target,
             std::vector<FrameAction::Argument> arguments);
  void End(ActionReference reference);
  void AdvanceAndDispatch(std::chrono::milliseconds real_delta,
                          std::chrono::milliseconds game_delta);
  void CancelGlobals();

 private:
  void RunFinal(const ActionReference& reference);

  Stage& stage_;
  LocalConfig&
      local_config_;  // time_stop, frame_action_time_stop, stage_time_stop
  std::shared_ptr<FrameAction> global_action_;
  FrameActionList global_channels_;
  Exec exec_;
};
