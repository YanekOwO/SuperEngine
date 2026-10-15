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

#include "core/game_config.hpp"
#include "libsiglus/bindings/flow.hpp"
#include "libsiglus/bindings/loader.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "srbind/srbind.hpp"
#include "systems/text_waku.hpp"
#include "systems/text_window.hpp"
#include "vm/exception.hpp"
#include "vm/function.hpp"
#include "vm/object.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <limits>
#include <stdexcept>
#include <utility>

namespace libsiglus::binding {
namespace sr = serilang;
namespace sb = srbind;

using ms = std::chrono::milliseconds;

namespace {

// Snapshot channels and children: a callback can reset its enclosing waku.
template <typename Visitor>
void VisitActions(const std::shared_ptr<FrameAction>& global_action,
                  FrameActionList& global_channels,
                  Stage& stage,
                  Visitor visitor) {
  auto visit_action = [&](std::shared_ptr<FrameAction> action,
                          ActionOwner owner, bool background) {
    if (action)
      visitor(ActionReference{std::move(action), std::move(owner)}, background);
  };
  auto visit_object = [&](auto&& self, std::shared_ptr<GraphicsObject> object,
                          bool background) -> void {
    if (!object)
      return;
    visit_action(object->frame_action_, object, background);
    const auto channels = object->frame_action_list_;
    for (const auto& action : channels)
      visit_action(action, object, background);
    const auto children = object->GetChildren();
    for (const auto& child : children)
      self(self, child, background);
  };

  // Callbacks may replace a waku or promote stage lists. Retain every root
  // before invoking the first callback, and never iterate a destroyed waku.
  std::vector<std::pair<ActionOwner, bool>> roots;
  for (int layer : {kLayerBg, kLayerFg, kLayerNext})
    for (const auto& object : stage.objects[layer])
      roots.emplace_back(object, layer == kLayerBg);
  for (int layer : {kLayerBg, kLayerFg, kLayerNext}) {
    for (const auto& window : stage.mwnd[layer]) {
      if (!window)
        continue;
      for (auto* waku : {window->GetTextboxWaku(), window->GetNameboxWaku()}) {
        if (!waku)
          continue;
        for (const auto* list : {&waku->button_objects_, &waku->objects_})
          for (const auto& object : *list)
            roots.emplace_back(object, layer == kLayerBg);
      }
    }
  }
  const auto channels = global_channels;
  visit_action(global_action, {}, false);
  for (const auto& action : channels)
    visit_action(action, {}, false);
  for (const auto& [object, background] : roots)
    visit_object(visit_object, object, background);
}

FrameAction& ResolveAction(const ActionReference& reference) {
  if (!reference.action)
    throw sr::RuntimeError("frame-action handle is null");
  return *reference.action;
}

class FrameActionHandle {
 public:
  FrameActionHandle(ActionReference reference, FrameActionManager* manager)
      : reference_(std::move(reference)), manager_(manager) {}

  FrameAction& Resolve() const { return ResolveAction(reference_); }

  void Start(Loader& loader,
             sr::Fiber& fiber,
             std::vector<sr::Value> args,
             bool real_time) {
    (void)Resolve();
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
    const std::size_t prefix = reference_.owner ? 2 : 1;
    if (resolved.args.size() != prefix + args.size() - 2 ||
        resolved.args.empty() || resolved.args[0] != Type::FrameAction ||
        (reference_.owner && resolved.args[1] != Type::Object)) {
      throw sr::RuntimeError(std::format(
          "frame-action command '{}' has incompatible signature: expected {} "
          "arguments beginning with {}",
          command->str_, prefix + args.size() - 2,
          reference_.owner ? "FrameAction, Object" : "FrameAction"));
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
    manager_->Start(reference_, *duration, real_time, *resolved.function,
                    std::move(arguments));
  }

  void End() { manager_->End(reference_); }
  int IsEndAction() const { return Resolve().end_action ? 1 : 0; }

 private:
  ActionReference reference_;
  FrameActionManager* manager_;
};

class FrameActionListHandle {
 public:
  FrameActionListHandle(ActionOwner owner, FrameActionManager* manager)
      : owner_(std::move(owner)), manager_(manager) {}

  sr::Value Get(int index) {
    auto& actions = Resolve();
    if (index < 0 || static_cast<std::size_t>(index) >= actions.size())
      throw sr::RuntimeError(
          std::format("frame-action index out of range: {}", index));
    return manager_->MakeActionValue(ActionReference{
        TouchFrameAction(actions[static_cast<std::size_t>(index)]), owner_});
  }

  int Size() const {
    const auto size = Resolve().size();
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
      throw sr::RuntimeError("frame-action list is too large");
    return static_cast<int>(size);
  }

  void Resize(int size) {
    if (!owner_)
      throw sr::RuntimeError("global frame-action channel list is fixed-size");
    if (size < 0)
      throw sr::RuntimeError("frame-action list size cannot be negative");
    Resolve().resize(static_cast<std::size_t>(size));
  }

 private:
  FrameActionList& Resolve() const {
    return owner_ ? owner_->frame_action_list_ : manager_->global_channels();
  }

  ActionOwner owner_;
  FrameActionManager* manager_;
};

}  // namespace

FrameActionManager::FrameActionManager(sr::VM& vm,
                                       Stage& stage,
                                       LocalConfig& local_config,
                                       std::size_t global_channel_count)
    : vm_(vm),
      stage_(stage),
      local_config_(local_config),
      global_channels_(global_channel_count) {}

void FrameActionManager::SetBindingClasses(sr::NativeClass* action_class,
                                           sr::NativeClass* list_class) {
  action_class_ = action_class;
  list_class_ = list_class;
}

void FrameActionManager::SetObjectFactory(ObjectFactory object_factory) {
  object_factory_ = std::move(object_factory);
}

sr::Value FrameActionManager::MakeActionValue(ActionReference reference) {
  (void)ResolveAction(reference);
  auto* instance = vm_.gc_->Allocate<sr::NativeInstance>(action_class_);
  instance->SetForeign(new FrameActionHandle(std::move(reference), this));
  return instance;
}

sr::Value FrameActionManager::MakeListValue(ActionOwner owner) {
  auto* instance = vm_.gc_->Allocate<sr::NativeInstance>(list_class_);
  instance->SetForeign(new FrameActionListHandle(std::move(owner), this));
  return instance;
}

std::vector<sr::Value> FrameActionManager::MakeCallbackArguments(
    const FrameAction& action,
    const ActionReference& reference) {
  std::vector<sr::Value> result;
  result.reserve(1 + (reference.owner ? 1 : 0) + action.arguments.size());
  result.emplace_back(MakeActionValue(reference));
  if (reference.owner)
    result.emplace_back(object_factory_(reference.owner));
  for (const FrameActionArgument& argument : action.arguments) {
    if (const int* value = std::get_if<int>(&argument))
      result.emplace_back(*value);
    else
      result.emplace_back(
          vm_.gc_->Allocate<sr::String>(std::get<std::string>(argument)));
  }
  return result;
}

void FrameActionManager::RunFinal(const ActionReference& reference) {
  auto& action = ResolveAction(reference);
  sr::Function* callback = action.function;
  auto arguments = MakeCallbackArguments(action, reference);
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

void FrameActionManager::Start(ActionReference reference,
                               int duration,
                               bool real_time,
                               sr::Function& callback,
                               std::vector<FrameActionArgument> arguments) {
  auto& action = ResolveAction(reference);
  End(reference);
  action.function = &callback;
  action.arguments = std::move(arguments);
  action.end_time = duration;
  std::shared_ptr<Counter> counter = action.GetCounter();
  if (real_time)
    counter->StartReal();
  else
    counter->StartGame();
}

void FrameActionManager::End(ActionReference reference) {
  auto& action = ResolveAction(reference);
  if (action.IsActive())
    RunFinal(reference);
  else
    action.Reset();
}

void FrameActionManager::StartConfiguredButton(
    Loader& loader,
    ActionOwner owner,
    const MwndConfig::CallTarget& target) {
  if (!target.command || target.command->empty())
    throw sr::RuntimeError(
        "WAKU.BTN.FRAME_ACTION requires a scene and command");
  const auto& command =
      ResolveSceneUserCommand(loader, target.scene, *target.command);
  if (command.args != std::vector<Type>{Type::FrameAction, Type::Object})
    throw sr::RuntimeError(
        "WAKU.BTN.FRAME_ACTION requires a command taking FrameAction, Object");
  Start({owner->GetFrameAction(), owner}, -1, false, *command.function, {});
}

void FrameActionManager::AdvanceAndDispatch(ms real_delta, ms game_delta) {
  const auto real_time = std::max(ms::zero(), real_delta);
  const auto game_time = std::max(ms::zero(), game_delta);

  const bool globals_stopped =
      local_config_.time_stop || local_config_.frame_action_time_stop;
  const bool objects_stopped =
      local_config_.time_stop || local_config_.stage_time_stop;
  VisitActions(global_action_, global_channels_, stage_,
               [&](const ActionReference& reference, bool) {
                 auto& action = *reference.action;
                 if (action.counter &&
                     !(reference.owner ? objects_stopped : globals_stopped))
                   action.counter->Advance(game_time, real_time);
               });
  VisitActions(global_action_, global_channels_, stage_,
               [&](const ActionReference& reference, bool) {
                 auto& action = *reference.action;
                 if (action.IsActive())
                   vm_.InvokeUntilBlocked(
                       *action.function,
                       MakeCallbackArguments(action, reference));
               });
  VisitActions(global_action_, global_channels_, stage_,
               [&](const ActionReference& reference, bool background) {
                 auto& action = *reference.action;
                 if (!background && action.IsActive() && action.end_time >= 0 &&
                     action.counter && action.counter->Get() >= action.end_time)
                   RunFinal(reference);
               });
}

void FrameActionManager::CancelGlobals() {
  if (global_action_)
    global_action_->Reset();
  for (auto& action : global_channels_)
    if (action)
      action->Reset();
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
    return make_counter(action->Resolve().GetCounter());
  });
  list_class.def("__getitem__", &FrameActionListHandle::Get, sb::arg("index"));
  list_class.def("size", &FrameActionListHandle::Size);
  list_class.def("resize", &FrameActionListHandle::Resize, sb::arg("size"));

  manager->SetBindingClasses(action_class.native_class(),
                             list_class.native_class());
  (*vm.builtins_)["%%siglus_frame_action_class"] = action_class.native_class();
  (*vm.builtins_)["%%siglus_frame_action_list_class"] =
      list_class.native_class();
  (*vm.globals_)["frame_action"] =
      manager->MakeActionValue(ActionReference{manager->global_action(), {}});
  (*vm.globals_)["frame_action_ch"] = manager->MakeListValue();
  runtime.reset_local_memory.emplace_back(
      [manager] { manager->CancelGlobals(); });
}

RLVM_REGISTER(SiglusBindingRegistry, "1_frame_action", BindFrameActions)

}  // namespace libsiglus::binding
