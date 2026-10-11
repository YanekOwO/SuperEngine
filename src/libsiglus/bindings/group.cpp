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
#include "libsiglus/siglus_runtime.hpp"
#include "vm/exception.hpp"

#include <cstddef>
#include <optional>
#include <string>

namespace libsiglus::binding {
namespace sb = srbind;
namespace sr = serilang;

struct GroupRef {
  Stage* stage;
  int layer;
  int group_no;

  Group& Get() {
    if (!stage)
      throw sr::RuntimeError("Group requires a stage");
    if (layer < kLayerFg || layer > kLayerNext)
      throw sr::RuntimeError("Invalid group layer: " + std::to_string(layer));
    if (group_no < 0) {
      throw sr::RuntimeError("Invalid group number: " +
                             std::to_string(group_no));
    }

    LazyArray<Group>& grp_arr = stage->groups[layer];
    const std::size_t index = static_cast<std::size_t>(group_no);
    if (index >= grp_arr.Size())
      grp_arr.Resize(index + 1);
    return grp_arr[index];
  }
};

static std::optional<int> FirstOptionalInt(std::vector<sr::Value> raw_args) {
  CallPacket packet = CallPacket::DecodeFrom(std::move(raw_args));
  if (packet.args.empty())
    return std::nullopt;
  return AsInt(packet.args.front());
}

void BindGroup(SiglusRuntime& runtime) {
  sr::VM& vm = *runtime.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  sb::class_<GroupRef> grp(m, "Group");
  grp.def(sb::init([stg = runtime.stage.get()](int layer,
                                               int group_no) -> GroupRef* {
            return new GroupRef(stg, layer, group_no);
          }),
          sb::arg("layer") = 0, sb::arg("group_no") = 0);
  grp.def("init", [](GroupRef* ref) { ref->Get().Reset(); });
  grp.def("start", [](GroupRef* ref) {
    Group& grp = ref->Get();
    grp.InitSel();
    grp.status = Group::Status::Active;
  });
  grp.def("end", [](GroupRef* ref) {
    Group& grp = ref->Get();
    grp.status = Group::Status::Disabled;
    grp.cancel_enabled = false;
    grp.decided_button_no.reset();
    grp.ClearTransientInteraction();
  });
  grp.def(
      "start_cancel",
      [](GroupRef* ref, std::vector<sr::Value> raw_args) {
        Group& grp = ref->Get();
        grp.InitSel();
        grp.status = Group::Status::Active;
        grp.cancel_enabled = true;
        grp.cancel_se = FirstOptionalInt(std::move(raw_args));
      },
      sb::vararg);
  grp.def(
      "sel",
      [](GroupRef* ref, std::vector<sr::Value> raw_args) -> sr::Value {
        Group& grp = ref->Get();
        grp.InitSel();
        grp.status = Group::Status::Waiting;
        grp.cancel_se = FirstOptionalInt(std::move(raw_args));
        throw sr::RuntimeError("TODO: group.sel not implemented yet");
      },
      sb::vararg);
  grp.def(
      "sel_cancel",
      [](GroupRef* ref, std::vector<sr::Value> raw_args) -> sr::Value {
        Group& grp = ref->Get();
        grp.InitSel();
        grp.status = Group::Status::Waiting;
        grp.cancel_enabled = true;
        grp.cancel_se = FirstOptionalInt(std::move(raw_args));
        throw sr::RuntimeError("TODO: group.sel_cancel not implemented yet");
      },
      sb::vararg);
  grp.def("get_hit_no", [](GroupRef* ref) {
    Group& grp = ref->Get();
    return grp.hit_button_no.value_or(-1);
  });
  grp.def("get_pushed_no", [](GroupRef* ref) {
    Group& grp = ref->Get();
    return grp.pushed_button_no.value_or(-1);
  });
  grp.def("get_decided_no", [](GroupRef* ref) {
    Group& grp = ref->Get();
    return grp.decided_button_no.value_or(-2);
  });
  grp.def("get_result", [](GroupRef* ref) {
    Group& grp = ref->Get();
    return static_cast<int>(grp.result);
  });
  grp.def("get_result_button_no", [](GroupRef* ref) {
    Group& grp = ref->Get();
    return grp.result_button_no.value_or(0);
  });
  grp.def("order", [](GroupRef* ref) {
    Group& grp = ref->Get();
    return grp.order;
  });
  grp.def("set_order", [](GroupRef* ref, int order) {
    Group& grp = ref->Get();
    grp.order = order;
  });
  grp.def("layer", [](GroupRef* ref) {
    Group& grp = ref->Get();
    return grp.layer;
  });
  grp.def("set_layer", [](GroupRef* ref, int layer) {
    Group& grp = ref->Get();
    grp.layer = layer;
  });
  grp.def("cancel_priority", [](GroupRef* ref) {
    Group& grp = ref->Get();
    return grp.cancel_priority;
  });
  grp.def("set_cancel_priority", [](GroupRef* ref, int cancel_priority) {
    Group& grp = ref->Get();
    grp.cancel_priority = cancel_priority;
  });
}

RLVM_REGISTER(SiglusBindingRegistry, "1_group", BindGroup)

}  // namespace libsiglus::binding
