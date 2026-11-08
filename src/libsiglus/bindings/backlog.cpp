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

#include "libsiglus/backlog.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "long_operations/siglus_backlog.hpp"
#include "long_operations/siglus_wait.hpp"
#include "srbind/srbind.hpp"
#include "vm/future.hpp"

namespace libsiglus::binding {

void BindBacklog(SiglusMachine& machine) {
  auto& vm = *machine.vm;
  auto* module = vm.builtins_->at("syscom").Get_if<serilang::Module>();
  srbind::module_ syscom(vm.gc_.get(), module->globals.get());
  auto pending = std::make_shared<lop::PendingCoroutineTasks>();
  syscom.def("open_msgback", [&machine, pending](serilang::VM& vm) {
    const int owner = machine.host().IsExcall() && machine.host().excall &&
                              machine.host().excall->allocated
                          ? 1
                          : 0;
    auto session = machine.host().Backlog().Open(owner);
    if (!session)
      return lop::MakeResolvedFuture(*vm.gc_);
    return serilang::Value(pending->MakeFuture(
        vm, std::make_unique<lop::SiglusBacklogWait>(std::move(session)),
        machine.host().system->event_ptr().get(), {},
        machine.WaitScope(owner)));
  });
  syscom.def("close_msgback", [&machine] { machine.host().Backlog().Close(); });
  syscom.def("is_msgback_open",
             [&machine] { return machine.host().Backlog().IsOpen() ? 1 : 0; });
  syscom.def("is_msgback_enable",
             [&machine] { return machine.host().Backlog().CanOpen() ? 1 : 0; });
}

RLVM_REGISTER(SiglusBindingRegistry, "syscom_backlog", BindBacklog)

}  // namespace libsiglus::binding
