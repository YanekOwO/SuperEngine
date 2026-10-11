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
#include <optional>
#include <string_view>
#include <vector>

class Gameexe;

namespace serilang {
class VM;
struct Function;
struct NativeClass;
}  // namespace serilang

namespace libsiglus::binding {

using ObjectHandle = StageElement<GraphicsObject>::Handle;
using ActionOwner = std::optional<ObjectHandle>;

struct ActionLocation {
  ActionOwner owner;
  std::optional<std::size_t> channel;
};

class FrameActionManager {
 public:
  using ObjectFactory = std::function<serilang::Value(ObjectHandle)>;

  FrameActionManager(serilang::VM& vm,
                     Stage& stage,
                     std::shared_ptr<Gameexe> local_config,
                     std::size_t global_channel_count);

  FrameAction& global_action() { return global_action_; }
  const FrameAction& global_action() const { return global_action_; }
  FrameActionList& global_channels() { return global_channels_; }
  const FrameActionList& global_channels() const { return global_channels_; }
  void SetBindingClasses(serilang::NativeClass* action_class,
                         serilang::NativeClass* list_class);
  void SetObjectFactory(ObjectFactory object_factory);
  serilang::Value MakeActionValue(ActionLocation location = {});
  serilang::Value MakeListValue(ActionOwner owner = {});

  void Start(ActionLocation location,
             int duration,
             bool real_time,
             serilang::Function& callback,
             std::vector<FrameActionArgument> arguments);
  void End(ActionLocation location);
  void AdvanceAndDispatch(std::chrono::milliseconds real_delta,
                          bool fast_forward);
  void CancelGlobals();

 private:
  std::vector<serilang::Value> MakeCallbackArguments(
      const FrameAction& action,
      const ActionLocation& location);
  void RunFinal(FrameAction& action, const ActionLocation& location);
  bool ConfigFlag(std::string_view name) const;

  serilang::VM& vm_;
  Stage& stage_;
  std::shared_ptr<Gameexe> local_config_;
  FrameAction global_action_;
  FrameActionList global_channels_;
  serilang::NativeClass* action_class_ = nullptr;
  serilang::NativeClass* list_class_ = nullptr;
  ObjectFactory object_factory_;
};

}  // namespace libsiglus::binding
