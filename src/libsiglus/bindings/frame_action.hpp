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
#include "core/stage.hpp"
#include "vm/value.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

struct LocalConfig;

namespace serilang {
class VM;
struct Function;
struct NativeClass;
}  // namespace serilang

namespace libsiglus::binding {
class Loader;

using ActionOwner = std::shared_ptr<GraphicsObject>;

struct ActionReference {
  std::shared_ptr<FrameAction> action;
  ActionOwner owner;
};

class FrameActionManager {
 public:
  using ObjectFactory = std::function<serilang::Value(ActionOwner)>;

  FrameActionManager(serilang::VM& vm,
                     Stage& stage,
                     LocalConfig& local_config,
                     std::size_t global_channel_count);

  inline std::shared_ptr<FrameAction> global_action() {
    return TouchFrameAction(global_action_);
  }
  inline const std::shared_ptr<FrameAction>& global_action() const {
    return global_action_;
  }
  FrameActionList& global_channels() { return global_channels_; }
  const FrameActionList& global_channels() const { return global_channels_; }
  void SetBindingClasses(serilang::NativeClass* action_class,
                         serilang::NativeClass* list_class);
  void SetObjectFactory(ObjectFactory object_factory);
  serilang::Value MakeActionValue(ActionReference reference);
  serilang::Value MakeListValue(ActionOwner owner = {});

  void Start(ActionReference reference,
             int duration,
             bool real_time,
             serilang::Function& callback,
             std::vector<FrameActionArgument> arguments);
  void End(ActionReference reference);
  void StartConfiguredButton(Loader& loader,
                             ActionOwner owner,
                             const MwndConfig::CallTarget& target);
  void AdvanceAndDispatch(std::chrono::milliseconds real_delta,
                          bool fast_forward);
  void CancelGlobals();

 private:
  std::vector<serilang::Value> MakeCallbackArguments(
      const FrameAction& action,
      const ActionReference& reference);
  void RunFinal(const ActionReference& reference);

  serilang::VM& vm_;
  Stage& stage_;
  LocalConfig& local_config_;
  std::shared_ptr<FrameAction> global_action_;
  FrameActionList global_channels_;
  serilang::NativeClass* action_class_ = nullptr;
  serilang::NativeClass* list_class_ = nullptr;
  ObjectFactory object_factory_;
};

}  // namespace libsiglus::binding
