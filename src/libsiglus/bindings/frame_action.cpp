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

#include "core/frame_action_manager.hpp"

#include "libsiglus/bindings/loader.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/trace.hpp"
#include "libsiglus/siglus_runtime.hpp"
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

namespace {

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

    manager_->Start(reference_, *duration, real_time, std::move(call));
  }

  void End() {
    (void)Resolve();
    manager_->End(reference_);
  }
  int IsEndAction() const { return Resolve().end_action ? 1 : 0; }

 private:
  ActionReference reference_;
  FrameActionManager* manager_;
};

class FrameActionListHandle {
 public:
  FrameActionListHandle(ActionOwner owner, FrameActionManager* manager)
      : owner_(std::move(owner)), manager_(manager) {}

  FrameActionManager* manager() const { return manager_; }

  ActionReference Get(int index) {
    auto& actions = Resolve();
    if (index < 0 || static_cast<std::size_t>(index) >= actions.size())
      throw sr::RuntimeError(
          std::format("frame-action index out of range: {}", index));
    return ActionReference{
        TouchFrameAction(actions[static_cast<std::size_t>(index)]), owner_};
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

void BindFrameActions(SiglusMachine& runtime) {
  if (!runtime.host().frame_action_manager || !runtime.make_counter)
    throw std::runtime_error(
        "frame-action binding requires action and counter state");
  sr::VM& vm = *runtime.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  sb::class_<FrameActionHandle> action_class(m, "FrameAction", false);
  sb::class_<FrameActionListHandle> list_class(m, "FrameActionList", false);
  runtime.host().InitializeExcall();
  auto* manager = runtime.host().frame_action_manager.get();

  runtime.make_owned_frame_action =
      [action_class, &runtime](ActionReference reference, int owner) mutable {
        (void)ResolveAction(reference);
        return sr::Value(action_class.make_inst(
            std::move(reference), &runtime.host().GetFrameActions(owner)));
      };
  runtime.make_frame_action = [&runtime](ActionReference reference) {
    return runtime.make_owned_frame_action(std::move(reference), 0);
  };
  runtime.make_owned_frame_action_list =
      [list_class, &runtime](ActionOwner object, int owner) mutable {
        return sr::Value(list_class.make_inst(
            std::move(object), &runtime.host().GetFrameActions(owner)));
      };
  runtime.make_frame_action_list = [&runtime](ActionOwner object) {
    return runtime.make_owned_frame_action_list(std::move(object), 0);
  };

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
  action_class.prop_r("counter", [make_counter = runtime.make_counter](
                                     FrameActionHandle* action) {
    return make_counter(action->Resolve().GetCounter());
  });
  list_class.def(
      "__getitem__",
      [action_class](FrameActionListHandle* list, int index) mutable {
        return sr::Value(
            action_class.make_inst(list->Get(index), list->manager()));
      },
      sb::arg("index"));
  list_class.def("size", &FrameActionListHandle::Size);
  list_class.def("resize", &FrameActionListHandle::Resize, sb::arg("size"));

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

  (*vm.builtins_)["%%siglus_frame_action_class"] = action_class.native_class();
  (*vm.builtins_)["%%siglus_frame_action_list_class"] =
      list_class.native_class();
  (*vm.globals_)["frame_action"] =
      runtime.make_frame_action(ActionReference{manager->global_action(), {}});
  (*vm.globals_)["frame_action_ch"] = runtime.make_frame_action_list({});
  runtime.reset_local_memory.emplace_back(
      [manager] { manager->CancelGlobals(); });
}

RLVM_REGISTER(SiglusBindingRegistry, "1_frame_action", BindFrameActions)

}  // namespace libsiglus::binding
