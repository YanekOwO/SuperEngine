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
#include "srbind/srbind.hpp"
#include "vm/exception.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace libsiglus::binding {
namespace sb = srbind;
namespace sr = serilang;

struct GroupHandle {
  std::shared_ptr<Group> group;
};

static std::shared_ptr<Group> GetGroup(Stage* stage_state,
                                       int layer,
                                       int index) {
  if (index < 0)
    throw std::out_of_range("Negative stage group index");
  auto& group_layer = stage_state->groups[layer];
  const std::size_t pos = static_cast<std::size_t>(index);
  if (pos >= group_layer.size())
    group_layer.resize(pos + 1);
  return stage_state->groups.Get(layer, pos);
}

static std::optional<int> FirstOptionalInt(std::vector<sr::Value> raw_args) {
  CallPacket packet = CallPacket::DecodeFrom(std::move(raw_args));
  if (packet.args.empty())
    return std::nullopt;
  return AsInt(packet.args.front());
}

void BindGroup(SiglusMachine& runtime) {
  auto& vm = *runtime.vm;
  Stage* stage_state = runtime.stage.get();
  if (!stage_state)
    throw std::runtime_error("Group requires a stage");
  sb::module_ m(vm.gc_.get(), vm.globals_.get());

  sb::class_<GroupHandle> group(m, "Group");
  group.def(sb::init([stage_state](int layer, int index) {
              std::shared_ptr<Group> g = GetGroup(stage_state, layer, index);
              return std::make_unique<GroupHandle>(g);
            }),
            sb::arg("layer") = static_cast<int>(kLayerFg),
            sb::arg("index") = 0);
  group.def("init", [](GroupHandle* h) { h->group->Reset(); });
  group.def("start", [](GroupHandle* h) {
    Group& g = *h->group;
    g.InitSel();
    g.status = Group::Status::Active;
  });
  group.def("end", [](GroupHandle* h) {
    Group& g = *h->group;
    g.status = Group::Status::Disabled;
    g.cancel_enabled = false;
    g.decided_button_no.reset();
    g.ClearTransientInteraction();
  });
  group.def(
      "start_cancel",
      [](GroupHandle* h, std::vector<sr::Value> raw_args) {
        Group& g = *h->group;
        g.InitSel();
        g.status = Group::Status::Active;
        g.cancel_enabled = true;
        g.cancel_se = FirstOptionalInt(std::move(raw_args));
      },
      sb::vararg);
  group.def(
      "sel",
      [](GroupHandle* h, std::vector<sr::Value> raw_args) -> sr::Value {
        Group& g = *h->group;
        g.InitSel();
        g.status = Group::Status::Waiting;
        g.cancel_se = FirstOptionalInt(std::move(raw_args));
        throw sr::RuntimeError("TODO: group.sel not implemented yet");
      },
      sb::vararg);
  group.def(
      "sel_cancel",
      [](GroupHandle* h, std::vector<sr::Value> raw_args) -> sr::Value {
        Group& g = *h->group;
        g.InitSel();
        g.status = Group::Status::Waiting;
        g.cancel_enabled = true;
        g.cancel_se = FirstOptionalInt(std::move(raw_args));
        throw sr::RuntimeError("TODO: group.sel_cancel not implemented yet");
      },
      sb::vararg);
  group.def("get_hit_no", [](GroupHandle* h) {
    return h->group->hit_button_no.value_or(-1);
  });
  group.def("get_pushed_no", [](GroupHandle* h) {
    return h->group->pushed_button_no.value_or(-1);
  });
  group.def("get_decided_no", [](GroupHandle* h) {
    return h->group->decided_button_no.value_or(-2);
  });
  group.def("get_result",
            [](GroupHandle* h) { return static_cast<int>(h->group->result); });
  group.def("get_result_button_no", [](GroupHandle* h) {
    return h->group->result_button_no.value_or(0);
  });
  group.def("order", [](GroupHandle* h) { return h->group->order; });
  group.def("set_order",
            [](GroupHandle* h, int order) { h->group->order = order; });
  group.def("layer", [](GroupHandle* h) { return h->group->layer; });
  group.def("set_layer",
            [](GroupHandle* h, int layer) { h->group->layer = layer; });
  group.def("cancel_priority",
            [](GroupHandle* h) { return h->group->cancel_priority; });
  group.def("set_cancel_priority", [](GroupHandle* h, int cancel_priority) {
    h->group->cancel_priority = cancel_priority;
  });

  struct GroupListHandle {
    int layer;
  };
  sb::class_<GroupListHandle> group_array(m, "GroupArray");
  group_array.add_gc_root(group);
  group_array.def(
      sb::init([](int layer) {
        if (layer != kLayerFg && layer != kLayerBg && layer != kLayerNext)
          throw sr::RuntimeError("invalid group layer: " +
                                 std::to_string(layer));
        return std::make_unique<GroupListHandle>(layer);
      }),
      sb::arg("layer"));
  group_array.def(
      "__getitem__",
      [stage_state, group](GroupListHandle* h, int index) mutable {
        std::shared_ptr<Group> g = GetGroup(stage_state, h->layer, index);
        sr::NativeInstance* inst = group.make_inst(g);
        return sr::Value(inst);
      },
      sb::arg("index"));
}

RLVM_REGISTER(SiglusBindingRegistry, "1_group", BindGroup);

}  // namespace libsiglus::binding
