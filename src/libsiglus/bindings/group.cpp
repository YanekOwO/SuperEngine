// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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

#include "libsiglus/bindings/registry.hpp"

#include "core/group.hpp"
#include "core/stage.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "vm/exception.hpp"

#include <cstddef>
#include <optional>
#include <string>

namespace libsiglus::binding {
namespace sb = srbind;
namespace sr = serilang;

using GroupHandle = StageElement<Group>::Handle;

Group& ResolveGroup(const GroupHandle& handle) {
  return StageElement<Group>::Resolve(handle);
}

static std::optional<int> FirstOptionalInt(std::vector<sr::Value> raw_args) {
  CallPacket packet = CallPacket::DecodeFrom(std::move(raw_args));
  if (packet.args.empty())
    return std::nullopt;
  return AsInt(packet.args.front());
}

void BindGroup(SiglusMachine& runtime) {
  sr::VM& vm = *runtime.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  sb::class_<GroupHandle> grp(m, "Group");
  grp.def(sb::init([stg = runtime.stage.get()](int layer,
                                               int group_no) -> GroupHandle* {
            if (!stg)
              throw sr::RuntimeError("Group requires a stage");
            if (group_no < 0) {
              throw sr::RuntimeError("Invalid group number: " +
                                     std::to_string(group_no));
            }
            auto& group_layer = stg->groups[layer];
            const std::size_t index = static_cast<std::size_t>(group_no);
            if (index >= group_layer.size())
              group_layer.resize(index + 1);
            return new GroupHandle(stg->groups.GetHandle(layer, index));
          }),
          sb::arg("layer") = 0, sb::arg("group_no") = 0);
  grp.def("init", [](GroupHandle* ref) { ResolveGroup(*ref).Reset(); });
  grp.def("start", [](GroupHandle* ref) {
    Group& grp = ResolveGroup(*ref);
    grp.InitSel();
    grp.status = Group::Status::Active;
  });
  grp.def("end", [](GroupHandle* ref) {
    Group& grp = ResolveGroup(*ref);
    grp.status = Group::Status::Disabled;
    grp.cancel_enabled = false;
    grp.decided_button_no.reset();
    grp.ClearTransientInteraction();
  });
  grp.def(
      "start_cancel",
      [](GroupHandle* ref, std::vector<sr::Value> raw_args) {
        Group& grp = ResolveGroup(*ref);
        grp.InitSel();
        grp.status = Group::Status::Active;
        grp.cancel_enabled = true;
        grp.cancel_se = FirstOptionalInt(std::move(raw_args));
      },
      sb::vararg);
  grp.def(
      "sel",
      [](GroupHandle* ref, std::vector<sr::Value> raw_args) -> sr::Value {
        Group& grp = ResolveGroup(*ref);
        grp.InitSel();
        grp.status = Group::Status::Waiting;
        grp.cancel_se = FirstOptionalInt(std::move(raw_args));
        throw sr::RuntimeError("TODO: group.sel not implemented yet");
      },
      sb::vararg);
  grp.def(
      "sel_cancel",
      [](GroupHandle* ref, std::vector<sr::Value> raw_args) -> sr::Value {
        Group& grp = ResolveGroup(*ref);
        grp.InitSel();
        grp.status = Group::Status::Waiting;
        grp.cancel_enabled = true;
        grp.cancel_se = FirstOptionalInt(std::move(raw_args));
        throw sr::RuntimeError("TODO: group.sel_cancel not implemented yet");
      },
      sb::vararg);
  grp.def("get_hit_no", [](GroupHandle* ref) {
    Group& grp = ResolveGroup(*ref);
    return grp.hit_button_no.value_or(-1);
  });
  grp.def("get_pushed_no", [](GroupHandle* ref) {
    Group& grp = ResolveGroup(*ref);
    return grp.pushed_button_no.value_or(-1);
  });
  grp.def("get_decided_no", [](GroupHandle* ref) {
    Group& grp = ResolveGroup(*ref);
    return grp.decided_button_no.value_or(-2);
  });
  grp.def("get_result", [](GroupHandle* ref) {
    Group& grp = ResolveGroup(*ref);
    return static_cast<int>(grp.result);
  });
  grp.def("get_result_button_no", [](GroupHandle* ref) {
    Group& grp = ResolveGroup(*ref);
    return grp.result_button_no.value_or(0);
  });
  grp.def("order", [](GroupHandle* ref) {
    Group& grp = ResolveGroup(*ref);
    return grp.order;
  });
  grp.def("set_order", [](GroupHandle* ref, int order) {
    Group& grp = ResolveGroup(*ref);
    grp.order = order;
  });
  grp.def("layer", [](GroupHandle* ref) {
    Group& grp = ResolveGroup(*ref);
    return grp.layer;
  });
  grp.def("set_layer", [](GroupHandle* ref, int layer) {
    Group& grp = ResolveGroup(*ref);
    grp.layer = layer;
  });
  grp.def("cancel_priority", [](GroupHandle* ref) {
    Group& grp = ResolveGroup(*ref);
    return grp.cancel_priority;
  });
  grp.def("set_cancel_priority", [](GroupHandle* ref, int cancel_priority) {
    Group& grp = ResolveGroup(*ref);
    grp.cancel_priority = cancel_priority;
  });
}

RLVM_REGISTER(SiglusBindingRegistry, "1_group", BindGroup)

}  // namespace libsiglus::binding
