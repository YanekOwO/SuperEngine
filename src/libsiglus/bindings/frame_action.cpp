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

#include "core/counter.hpp"
#include "core/frame_action_manager.hpp"
#include "vm/serialization.hpp"

#include "core/object_internal/objdrawer.hpp"
#include "libsiglus/bindings/counter.hpp"
#include "libsiglus/bindings/frame_action.hpp"
#include "libsiglus/bindings/loader.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/trace.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "long_operations/siglus_wait.hpp"
#include "srbind/srbind.hpp"
#include "vm/exception.hpp"
#include "vm/object.hpp"
#include "vm/string.hpp"
#include "vm/value.hpp"
#include "vm/vm.hpp"

#include <format>
#include <limits>
#include <stdexcept>
#include <utility>

namespace libsiglus::binding {
namespace sr = serilang;
namespace sb = srbind;

void BindFrameActions(SiglusMachine& runtime) {
  if (!runtime.host().frame_action_manager)
    throw std::runtime_error("frame-action binding requires action manager");

  sr::VM& vm = *runtime.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  sb::class_<FrameActionHandle> action_class(m, "FrameAction", false);
  RegisterBindingClass(runtime, action_class);
  action_class.def_serialization(
      [&runtime](const FrameActionHandle& h, serilang::ObjectWriter& out) {
        out.Put(h.reference.action);
        out.Put(h.reference.owner);
        out.Put(runtime.host().excall &&
                        h.manager == runtime.host().excall->actions.get()
                    ? 1
                    : 0);
      },
      [&runtime](serilang::ObjectReader& in) {
        auto& host = runtime.host();
        auto action = in.Get<std::shared_ptr<FrameAction>>();
        auto object = in.Get<std::shared_ptr<GraphicsObject>>();
        auto owner = in.Get<int>();
        if (owner < 0 || owner > 1)
          throw std::runtime_error("invalid saved action owner");
        if (owner == 1)
          host.InitializeExcall();
        auto* manager = owner == 1 ? host.excall->actions.get()
                                   : host.frame_action_manager.get();
        if (!manager)
          throw std::runtime_error("saved action manager unavailable");
        return std::make_unique<FrameActionHandle>(
            ActionReference{action, object}, manager);
      });

  sb::class_<FrameActionListHandle> list_class(m, "FrameActionList", false);
  RegisterBindingClass(runtime, list_class);
  list_class.def_serialization(
      [&runtime](const FrameActionListHandle& h, serilang::ObjectWriter& out) {
        out.Put(h.owner);
        out.Put(runtime.host().excall &&
                        h.manager == runtime.host().excall->actions.get()
                    ? 1
                    : 0);
      },
      [&runtime](serilang::ObjectReader& in) {
        auto& host = runtime.host();
        auto object = in.Get<std::shared_ptr<GraphicsObject>>();
        auto owner = in.Get<int>();
        if (owner < 0 || owner > 1)
          throw std::runtime_error("invalid saved action owner");
        if (owner == 1)
          host.InitializeExcall();
        auto* manager = owner == 1 ? host.excall->actions.get()
                                   : host.frame_action_manager.get();
        if (!manager)
          throw std::runtime_error("saved action manager unavailable");
        return std::make_unique<FrameActionListHandle>(object, manager);
      });

  runtime.host().InitializeExcall();
  auto* manager = runtime.host().frame_action_manager.get();

  const auto resolve_action = [](FrameActionHandle* handle) -> FrameAction& {
    if (!handle->reference.action)
      throw sr::RuntimeError("frame-action handle is null");
    return *handle->reference.action;
  };
  const auto resolve_list =
      [](FrameActionListHandle* handle) -> FrameActionList& {
    return handle->owner ? handle->owner->frame_action_list_
                         : handle->manager->global_channels();
  };

  auto start = [&runtime, loader = runtime.loader.get(), resolve_action](
                   FrameActionHandle* action, sr::Fiber& fiber,
                   std::vector<sr::Value> args, bool real_time) {
    if (!loader)
      throw sr::RuntimeError("frame-action start requires a scene loader");

    (void)resolve_action(action);
    if (args.size() < 2)
      throw sr::RuntimeError("frame_action.start expects duration and command");
    const int* duration = args[0].Get_if<int>();
    const sr::String* command = args[1].Get_if<sr::String>();
    if (!duration || !command || command->str_.empty()) {
      throw sr::RuntimeError(
          "frame_action.start expects int duration and non-empty command");
    }

    const UserCommand& resolved =
        loader->ResolveUserCommand(fiber, command->str_);
    const std::size_t prefix = action->reference.owner ? 2 : 1;
    if (resolved.args.size() != prefix + args.size() - 2 ||
        resolved.args.empty() || resolved.args[0] != Type::FrameAction ||
        (action->reference.owner && resolved.args[1] != Type::Object)) {
      throw sr::RuntimeError(std::format(
          "frame-action command '{}' has incompatible signature: expected {} "
          "arguments beginning with {}",
          command->str_, prefix + args.size() - 2,
          action->reference.owner ? "FrameAction, Object" : "FrameAction"));
    }

    ScriptCall call{.target = resolved.target};
    for (std::size_t i = 2; i < args.size(); ++i) {
      if (const int* value = args[i].Get_if<int>();
          value && resolved.args[prefix + i - 2] == Type::Int) {
        call.integers.push_back(*value);
      } else if (const sr::String* value = args[i].Get_if<sr::String>();
                 value && resolved.args[prefix + i - 2] == Type::String) {
        call.strings.push_back(value->str_);
      } else {
        throw sr::RuntimeError(std::format(
            "frame-action argument {} for command '{}' has incompatible type",
            i - 1, command->str_));
      }
    }

    const int owner =
        action->manager == runtime.host().frame_action_manager.get() ? 0 : 1;
    runtime.host().GetFrameActions(owner).Start(action->reference, *duration,
                                                real_time, std::move(call));
    return lop::MakeResolvedFuture(*runtime.vm->gc_);
  };

  action_class.def(
      "start",
      [start](FrameActionHandle* action, sr::Fiber& fiber,
              std::vector<sr::Value> args) {
        return start(action, fiber, std::move(args), false);
      },
      sb::vararg);
  action_class.def(
      "start_real",
      [start](FrameActionHandle* action, sr::Fiber& fiber,
              std::vector<sr::Value> args) {
        return start(action, fiber, std::move(args), true);
      },
      sb::vararg);
  action_class.def("end", [&runtime,
                           resolve_action](FrameActionHandle* action) {
    (void)resolve_action(action);
    const int owner =
        action->manager == runtime.host().frame_action_manager.get() ? 0 : 1;
    runtime.host().GetFrameActions(owner).End(action->reference);
    return lop::MakeResolvedFuture(*runtime.vm->gc_);
  });
  action_class.def("is_end_action",
                   [resolve_action](FrameActionHandle* action) {
                     return resolve_action(action).end_action ? 1 : 0;
                   });
  action_class.prop_r("counter", [&runtime,
                                  resolve_action](FrameActionHandle* action) {
    std::shared_ptr<Counter> counter = resolve_action(action).GetCounter();
    sr::NativeInstance* inst = runtime.class_registry.MakeInst<CounterHandle>(
        std::move(counter),
        action->manager == runtime.host().frame_action_manager.get() ? 0 : 1);
    return sr::Value(inst);
  });
  list_class.def(
      "__getitem__",
      [action_class, resolve_list](FrameActionListHandle* list,
                                   int index) mutable {
        auto& actions = resolve_list(list);
        if (index < 0 || static_cast<std::size_t>(index) >= actions.size())
          throw sr::RuntimeError(
              std::format("frame-action index out of range: {}", index));
        ActionReference reference{
            TouchFrameAction(actions[static_cast<std::size_t>(index)]),
            list->owner};
        return sr::Value(
            action_class.make_inst(std::move(reference), list->manager));
      },
      sb::arg("index"));
  list_class.def("size", [resolve_list](FrameActionListHandle* list) {
    const auto size = resolve_list(list).size();
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
      throw sr::RuntimeError("frame-action list is too large");
    return static_cast<int>(size);
  });
  list_class.def(
      "resize",
      [](FrameActionListHandle* list, int size) {
        if (!list->owner)
          throw sr::RuntimeError(
              "global frame-action channel list is fixed-size");
        if (size < 0)
          throw sr::RuntimeError("frame-action list size cannot be negative");
        list->owner->frame_action_list_.resize(static_cast<std::size_t>(size));
      },
      sb::arg("size"));

  // Shared managers install one host dispatcher; callbacks get local wrappers.
  if (&runtime == &runtime.host().GameplayMachine()) {
    auto& host = runtime.host();
    manager->SetExec(
        [&host](const ActionReference& reference, ScriptCall call) {
          host.DispatchFrameAction(0, reference, std::move(call));
        });
    host.excall->actions->SetExec(
        [&host](const ActionReference& reference, ScriptCall call) {
          host.DispatchFrameAction(1, reference, std::move(call));
        });
  }

  (*vm.globals_)["frame_action"] =
      sr::Value(runtime.class_registry.MakeInst<FrameActionHandle>(
          ActionReference{manager->global_action(), {}}, manager));
  (*vm.globals_)["frame_action_ch"] =
      sr::Value(runtime.class_registry.MakeInst<FrameActionListHandle>(
          ActionOwner{}, manager));
  runtime.reset_local_memory.emplace_back(
      [manager] { manager->CancelGlobals(); });
}

RLVM_REGISTER(SiglusBindingRegistry, "1_frame_action", BindFrameActions)

}  // namespace libsiglus::binding
