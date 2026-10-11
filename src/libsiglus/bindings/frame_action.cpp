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

#include "libsiglus/bindings/frame_action.hpp"

#include "core/gameexe.hpp"
#include "libsiglus/bindings/loader.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "srbind/srbind.hpp"
#include "vm/exception.hpp"
#include "vm/function.hpp"
#include "vm/object.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <stdexcept>
#include <utility>

namespace libsiglus::binding {
namespace sr = serilang;
namespace sb = srbind;

namespace {

// Callbacks leave the object tree and channel lists intact during traversal.
template <typename Visitor>
void VisitActions(FrameAction& global_action,
                  FrameActionList& global_channels,
                  Stage& stage,
                  Visitor visitor) {
  auto visit_object = [&](auto&& self, std::shared_ptr<GraphicsObject> object,
                          bool background) -> void {
    if (!object)
      return;
    visitor(object->frame_action_, ActionLocation{ObjectHandle(object), {}},
            background);
    for (std::size_t i = 0; i < object->frame_action_list_.size(); ++i)
      visitor(object->frame_action_list_[i],
              ActionLocation{ObjectHandle(object), i}, background);
    for (const auto& child : object->GetChildren())
      self(self, child, background);
  };

  visitor(global_action, ActionLocation{}, false);
  for (std::size_t i = 0; i < global_channels.size(); ++i)
    visitor(global_channels[i], ActionLocation{{}, i}, false);

  for (int layer : {kLayerBg, kLayerFg, kLayerNext}) {
    for (const auto& object : stage.objects[layer])
      visit_object(visit_object, object, layer == kLayerBg);
  }
}

std::shared_ptr<GraphicsObject> ResolveOwner(const ActionOwner& owner) {
  if (!owner)
    return {};
  auto object = owner->lock();
  if (!object)
    throw sr::RuntimeError("frame-action owner has been deallocated");
  return object;
}

struct ResolvedAction {
  std::shared_ptr<GraphicsObject> owner;
  FrameAction& action;
};

ResolvedAction ResolveAction(const ActionLocation& location,
                             FrameActionManager& manager) {
  auto owner = ResolveOwner(location.owner);
  if (!location.channel) {
    auto& action = owner ? owner->frame_action_ : manager.global_action();
    return {std::move(owner), action};
  }
  auto& channels =
      owner ? owner->frame_action_list_ : manager.global_channels();
  if (*location.channel >= channels.size())
    throw sr::RuntimeError(
        std::format("frame-action index out of range: {}", *location.channel));
  auto& action = channels[*location.channel];
  return {std::move(owner), action};
}

class FrameActionHandle {
 public:
  FrameActionHandle(ActionLocation location, FrameActionManager* manager)
      : location_(std::move(location)), manager_(manager) {}

  ResolvedAction Resolve() const { return ResolveAction(location_, *manager_); }

  void Start(Loader& loader,
             sr::Fiber& fiber,
             std::vector<sr::Value> args,
             bool real_time) {
    auto action = Resolve();
    if (args.size() < 2)
      throw sr::RuntimeError("frame_action.start expects duration and command");
    const int* duration = args[0].Get_if<int>();
    const sr::String* command = args[1].Get_if<sr::String>();
    if (!duration || !command || command->str_.empty()) {
      throw sr::RuntimeError(
          "frame_action.start expects int duration and non-empty command");
    }

    const UserCommand& resolved =
        loader.ResolveUserCommand(fiber, command->str_);
    const std::size_t prefix = action.owner ? 2 : 1;
    if (resolved.args.size() != prefix + args.size() - 2 ||
        resolved.args.empty() || resolved.args[0] != Type::FrameAction ||
        (action.owner && resolved.args[1] != Type::Object)) {
      throw sr::RuntimeError(std::format(
          "frame-action command '{}' has incompatible signature: expected {} "
          "arguments beginning with {}",
          command->str_, prefix + args.size() - 2,
          action.owner ? "FrameAction, Object" : "FrameAction"));
    }

    std::vector<FrameActionArgument> arguments;
    arguments.reserve(args.size() - 2);
    for (std::size_t i = 2; i < args.size(); ++i) {
      if (const int* value = args[i].Get_if<int>();
          value && resolved.args[prefix + i - 2] == Type::Int) {
        arguments.emplace_back(*value);
      } else if (const sr::String* value = args[i].Get_if<sr::String>();
                 value && resolved.args[prefix + i - 2] == Type::String) {
        arguments.emplace_back(value->str_);
      } else {
        throw sr::RuntimeError(std::format(
            "frame-action argument {} for command '{}' has incompatible type",
            i - 1, command->str_));
      }
    }
    manager_->Start(location_, *duration, real_time, *resolved.function,
                    std::move(arguments));
  }

  void End() { manager_->End(location_); }
  int IsEndAction() const { return Resolve().action.end_action ? 1 : 0; }

 private:
  ActionLocation location_;
  FrameActionManager* manager_;
};

class FrameActionListHandle {
 public:
  FrameActionListHandle(ActionOwner owner, FrameActionManager* manager)
      : owner_(std::move(owner)), manager_(manager) {}

  sr::Value Get(int index) {
    auto resolved = Resolve();
    if (index < 0 || static_cast<std::size_t>(index) >= resolved.actions.size())
      throw sr::RuntimeError(
          std::format("frame-action index out of range: {}", index));
    return manager_->MakeActionValue(
        ActionLocation{owner_, static_cast<std::size_t>(index)});
  }

  int Size() const {
    auto resolved = Resolve();
    const auto size = resolved.actions.size();
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
      throw sr::RuntimeError("frame-action list is too large");
    return static_cast<int>(size);
  }

  void Resize(int size) {
    if (!owner_)
      throw sr::RuntimeError("global frame-action channel list is fixed-size");
    if (size < 0)
      throw sr::RuntimeError("frame-action list size cannot be negative");
    auto resolved = Resolve();
    resolved.actions.resize(static_cast<std::size_t>(size));
  }

 private:
  struct ResolvedList {
    std::shared_ptr<GraphicsObject> owner;
    FrameActionList& actions;
  };

  ResolvedList Resolve() const {
    auto owner = ResolveOwner(owner_);
    auto& actions =
        owner ? owner->frame_action_list_ : manager_->global_channels();
    return {std::move(owner), actions};
  }

  ActionOwner owner_;
  FrameActionManager* manager_;
};

}  // namespace

FrameActionManager::FrameActionManager(sr::VM& vm,
                                       Stage& stage,
                                       std::shared_ptr<Gameexe> local_config,
                                       std::size_t global_channel_count)
    : vm_(vm),
      stage_(stage),
      local_config_(std::move(local_config)),
      global_channels_(global_channel_count) {}

void FrameActionManager::SetBindingClasses(sr::NativeClass* action_class,
                                           sr::NativeClass* list_class) {
  action_class_ = action_class;
  list_class_ = list_class;
}

void FrameActionManager::SetObjectFactory(ObjectFactory object_factory) {
  object_factory_ = std::move(object_factory);
}

sr::Value FrameActionManager::MakeActionValue(ActionLocation location) {
  auto* instance = vm_.gc_->Allocate<sr::NativeInstance>(action_class_);
  instance->SetForeign(new FrameActionHandle(std::move(location), this));
  return instance;
}

sr::Value FrameActionManager::MakeListValue(ActionOwner owner) {
  auto* instance = vm_.gc_->Allocate<sr::NativeInstance>(list_class_);
  instance->SetForeign(new FrameActionListHandle(std::move(owner), this));
  return instance;
}

std::vector<sr::Value> FrameActionManager::MakeCallbackArguments(
    const FrameAction& action,
    const ActionLocation& location) {
  std::vector<sr::Value> result;
  result.reserve(1 + (location.owner ? 1 : 0) + action.arguments.size());
  result.emplace_back(MakeActionValue(location));
  if (location.owner)
    result.emplace_back(object_factory_(*location.owner));
  for (const FrameActionArgument& argument : action.arguments) {
    if (const int* value = std::get_if<int>(&argument))
      result.emplace_back(*value);
    else
      result.emplace_back(
          vm_.gc_->Allocate<sr::String>(std::get<std::string>(argument)));
  }
  return result;
}

void FrameActionManager::RunFinal(FrameAction& action,
                                  const ActionLocation& location) {
  sr::Function* callback = action.function;
  auto arguments = MakeCallbackArguments(action, location);
  action.function = nullptr;
  action.end_action = true;
  action.GetCounter()->Set(std::max(0, action.end_time));
  // Final callbacks do not start a replacement in this slot.
  try {
    vm_.InvokeUntilBlocked(*callback, std::move(arguments));
  } catch (...) {
    action.Reset();
    throw;
  }
  action.Reset();
}

void FrameActionManager::Start(ActionLocation location,
                               int duration,
                               bool real_time,
                               sr::Function& callback,
                               std::vector<FrameActionArgument> arguments) {
  auto resolved = ResolveAction(location, *this);
  End(location);
  auto& action = resolved.action;
  action.function = &callback;
  action.arguments = std::move(arguments);
  action.end_time = duration;
  action.GetCounter()->Start(real_time);
}

void FrameActionManager::End(ActionLocation location) {
  auto resolved = ResolveAction(location, *this);
  auto& action = resolved.action;
  if (action.IsActive())
    RunFinal(action, location);
  else
    action.Reset();
}

bool FrameActionManager::ConfigFlag(std::string_view name) const {
  return local_config_ && (*local_config_)(name).Int().value_or(0) != 0;
}

void FrameActionManager::AdvanceAndDispatch(
    std::chrono::milliseconds real_delta,
    bool fast_forward) {
  const std::int64_t real_ms = std::max<std::int64_t>(0, real_delta.count());
  const std::int64_t game_ms =
      !fast_forward ? real_ms
                    : (real_ms > std::numeric_limits<std::int64_t>::max() / 32
                           ? std::numeric_limits<std::int64_t>::max()
                           : real_ms * 32);
  const bool globals_stopped =
      ConfigFlag("time_stop") || ConfigFlag("frame_action_time_stop");
  const bool objects_stopped =
      ConfigFlag("time_stop") || ConfigFlag("stage_time_stop");
  VisitActions(global_action_, global_channels_, stage_,
               [&](FrameAction& action, const ActionLocation& location, bool) {
                 if (action.counter &&
                     !(location.owner ? objects_stopped : globals_stopped))
                   action.counter->Advance(game_ms, real_ms);
               });
  VisitActions(global_action_, global_channels_, stage_,
               [&](FrameAction& action, const ActionLocation& location, bool) {
                 if (action.IsActive())
                   vm_.InvokeUntilBlocked(
                       *action.function,
                       MakeCallbackArguments(action, location));
               });
  VisitActions(global_action_, global_channels_, stage_,
               [&](FrameAction& action, const ActionLocation& location,
                   bool background) {
                 if (!background && action.IsActive() && action.end_time >= 0 &&
                     action.counter && action.counter->Get() >= action.end_time)
                   RunFinal(action, location);
               });
}

void FrameActionManager::CancelGlobals() {
  global_action_.Reset();
  for (auto& action : global_channels_)
    action.Reset();
}

void BindFrameActions(SiglusMachine& runtime) {
  if (!runtime.frame_action_manager || !runtime.make_counter)
    throw std::runtime_error(
        "frame-action binding requires action and counter state");
  sr::VM& vm = *runtime.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  sb::class_<FrameActionHandle> action_class(m, "FrameAction", false);
  sb::class_<FrameActionListHandle> list_class(m, "FrameActionList", false);
  auto* manager = runtime.frame_action_manager.get();
  auto start = [loader = runtime.loader.get()](
                   FrameActionHandle* action, sr::Fiber& fiber,
                   std::vector<sr::Value> args, bool real_time) {
    if (!loader)
      throw sr::RuntimeError("frame-action start requires a scene loader");
    action->Start(*loader, fiber, std::move(args), real_time);
  };
  action_class.def(
      "start",
      [start](FrameActionHandle* action, sr::Fiber& fiber,
              std::vector<sr::Value> args) {
        start(action, fiber, std::move(args), false);
      },
      sb::vararg);
  action_class.def(
      "start_real",
      [start](FrameActionHandle* action, sr::Fiber& fiber,
              std::vector<sr::Value> args) {
        start(action, fiber, std::move(args), true);
      },
      sb::vararg);
  action_class.def("end", &FrameActionHandle::End);
  action_class.def("is_end_action", &FrameActionHandle::IsEndAction);
  action_class.def("counter", [make_counter = runtime.make_counter](
                                  FrameActionHandle* action) {
    auto resolved = action->Resolve();
    return make_counter(resolved.action.GetCounter());
  });
  list_class.def("__getitem__", &FrameActionListHandle::Get, sb::arg("index"));
  list_class.def("size", &FrameActionListHandle::Size);
  list_class.def("resize", &FrameActionListHandle::Resize, sb::arg("size"));

  manager->SetBindingClasses(action_class.native_class(),
                             list_class.native_class());
  (*vm.builtins_)["%%siglus_frame_action_class"] = action_class.native_class();
  (*vm.builtins_)["%%siglus_frame_action_list_class"] =
      list_class.native_class();
  (*vm.globals_)["frame_action"] = manager->MakeActionValue();
  (*vm.globals_)["frame_action_ch"] = manager->MakeListValue();
  runtime.reset_local_memory.emplace_back(
      [manager] { manager->CancelGlobals(); });
}

RLVM_REGISTER(SiglusBindingRegistry, "1_frame_action", BindFrameActions)

}  // namespace libsiglus::binding
