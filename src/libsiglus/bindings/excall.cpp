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

#include "libsiglus/bindings/excall.hpp"
#include "libsiglus/bindings/counter.hpp"
#include "libsiglus/bindings/frame_action.hpp"
#include "libsiglus/bindings/memory.hpp"
#include "libsiglus/bindings/registry.hpp"

#include "core/memory_internal/facade.hpp"
#include "srbind/srbind.hpp"
#include "vm/exception.hpp"
#include "vm/object.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <string>

namespace libsiglus::binding {
namespace sr = serilang;
namespace sb = srbind;

void ExcallHandle::RequireExcall() const {
  if (owner != 1)
    throw sr::RuntimeError("EX-call lifecycle requires excall[1]");
}

void ExcallHandle::RequireAllocated() const {
  if (owner == 1)
    runtime->host().excall->RequireAllocated();
}

void BindExcall(SiglusMachine& runtime) {
  runtime.host().InitializeExcall();
  auto& vm = *runtime.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  sb::class_<ExcallScriptHandle> script(m, "ExcallScript", false);
  RegisterBindingClass(runtime, script);
  script.def("set_font_name", [](ExcallScriptHandle* h, std::string name) {
    h->state->config.font_name = std::move(name);
  });
  script.def("get_font_name",
             [](ExcallScriptHandle* h) { return h->state->config.font_name; });
  script.def("set_font_name_default",
             [](ExcallScriptHandle* h) { h->state->config.font_name.clear(); });
  const auto font = [&](const char* setter, const char* getter,
                        const char* reset, int LocalConfig::* field) {
    script.def(setter, [field](ExcallScriptHandle* h, int value) {
      h->state->config.*field = value;
    });
    script.def(getter, [field](ExcallScriptHandle* h) {
      return h->state->config.*field;
    });
    script.def(reset, [field](ExcallScriptHandle* h) {
      h->state->config.*field = -1;
    });
  };
  font("set_font_bold", "get_font_bold", "set_font_bold_default",
       &LocalConfig::font_futoku);
  font("set_font_shadow", "get_font_shadow", "set_font_shadow_default",
       &LocalConfig::font_shadow);

  sb::class_<ExcallHandle> root(m, "Excall", false);
  RegisterBindingClass(runtime, root);
  root.add_gc_root(script);
  root.inst("excall", &runtime, 1);
  root.def("__getitem__", [root](ExcallHandle* h, int owner) mutable {
    if (owner != 0 && owner != 1)
      throw sr::RuntimeError("excall index must be 0 or 1");
    return sr::Value(root.make_inst(h->runtime, owner));
  });
  root.def("alloc", [](ExcallHandle* h) {
    h->RequireExcall();
    auto& host = h->runtime->host();
    auto& excall = *host.excall;
    if (excall.allocated || excall.releasing)
      return;
    const auto count = [gameexe = host.gameexe.get()](const char* key,
                                                      int fallback) {
      return std::max(
          0, gameexe ? (*gameexe)(key).Int().value_or(fallback) : fallback);
    };
    excall.Alloc(
        {.flag_count = count("FLAG.CNT", 1000),
         .counter_count = count("COUNTER.CNT", 16),
         .frame_action_count = std::min(256, count("FRAME_ACTION_CH.CNT", 4)),
         .object_count = count("OBJECT.CNT", 256),
         .effect_count = count("EFFECT.CNT", 0),
         .group_count = count("BUTTON.GROUP.CNT", 16),
         .quake_count = count("QUAKE.CNT", 16),
         .window_count = count("MWND.CNT", 1)});
  });
  root.def("free", [](ExcallHandle* h) {
    h->RequireExcall();
    h->runtime->host().FreeExcall();
  });
  root.def("check_alloc", [](ExcallHandle* h) {
    h->RequireExcall();
    return h->runtime->host().excall->allocated ? 1 : 0;
  });
  root.def("is_excall", [](ExcallHandle* h) {
    return h->runtime->host().IsExcall() ? 1 : 0;
  });
  auto flags =
      [state = runtime.host().excall.get()](sr::Fiber&) -> std::vector<int>& {
    state->RequireAllocated();
    return state->flags;
  };
  // Keep one per-VM flag facade available across EX resource allocations.
  (*vm.builtins_)["%%excall_flags"] =
      sr::Value(runtime.class_registry.MakeInst<SiglusIntListFacade>(
          std::move(flags), 0));
  root.prop_r("F", [](ExcallHandle* h) -> sr::Value {
    h->RequireAllocated();
    if (h->owner == 0)
      return h->runtime->vm->globals_->at("F");
    return h->runtime->vm->builtins_->at("%%excall_flags");
  });
  root.prop_r("counter", [](ExcallHandle* h) -> sr::Value {
    h->RequireAllocated();
    return h->owner == 0
               ? h->runtime->vm->globals_->at("counter")
               : sr::Value(
                     h->runtime->class_registry.MakeInst<CounterListHandle>(
                         h->runtime->host().excall->counters, 1));
  });
  root.prop_r("frame_action", [](ExcallHandle* h) {
    auto& actions = h->runtime->host().GetFrameActions(h->owner);
    return sr::Value(h->runtime->class_registry.MakeInst<FrameActionHandle>(
        ActionReference{actions.global_action(), {}}, &actions));
  });
  root.prop_r("frame_action_ch", [](ExcallHandle* h) {
    h->RequireAllocated();
    return sr::Value(h->runtime->class_registry.MakeInst<FrameActionListHandle>(
        ActionOwner{}, &h->runtime->host().GetFrameActions(h->owner)));
  });
  root.prop_r("script", [script](ExcallHandle* h) mutable {
    h->RequireAllocated();
    return sr::Value(script.make_inst(h->runtime->host().excall.get()));
  });
  for (const char* member : {"stage", "back", "front", "next"}) {
    root.prop_r(
        member, [member = std::string(member)](ExcallHandle* h) -> sr::Value {
          h->RequireAllocated();
          auto value = h->runtime->vm->globals_->at(h->owner ? "__excall_stage"
                                                             : "stage");
          if (member == "stage")
            return value;
          return value.Get<sr::Instance*>()->fields.at(member);
        });
  }
  runtime.reset_local_memory.emplace_back(
      [&runtime] { runtime.host().ResetExcall(); });
}

RLVM_REGISTER(SiglusBindingRegistry, "3_excall", BindExcall)

}  // namespace libsiglus::binding
