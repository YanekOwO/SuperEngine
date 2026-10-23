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

#include "core/game_config.hpp"
#include "core/kidoku_table.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "long_operations/siglus_textout.hpp"
#include "long_operations/siglus_wait.hpp"
#include "srbind/module.hpp"
#include "systems/system.hpp"
#include "systems/text_page.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"
#include "vm/future.hpp"
#include "vm/vm.hpp"

#include <memory>
#include <string>
#include <utility>

namespace libsiglus::binding {
namespace sb = srbind;
namespace sr = serilang;

class SiglusTextout {
 public:
  SiglusTextout(sr::VM& vm, System* sys, LocalConfig& localcfg)
      : vm_(vm), sys_(sys), localcfg_(localcfg) {}

  lop::PendingCoroutineTasks pending_;
  sr::VM& vm_;
  System* sys_;
  LocalConfig& localcfg_;
};

void BindTextout(SiglusMachine& runtime) {
  sb::module_ m(runtime.vm->gc_.get(), runtime.vm->globals_.get());
  m.def("__builtin_name", [sys = runtime.system.get()](std::string name) {
    if (!sys || name.empty())
      return;

    TextSystem& text = sys->text();
    std::shared_ptr<TextWindow> window = text.GetCurrentMwnd();
    if (window->HasName())
      return;

    if (sys->local_config().multi_msg_mode)
      window->BeginMultiMessage();
    if (!name.empty())
      text.GetCurrentPage().Name(name, "");
  });
  m.def("__builtin_textout",
        [kt = runtime.kidoku_table,
         to = std::make_shared<SiglusTextout>(*runtime.vm, runtime.system.get(),
                                              runtime.local_config)](
            int scenario, int kidoku, std::string text) -> sr::Value {
          const bool already_read = kt->HasBeenRead(scenario, kidoku);
          kt->RecordKidoku(scenario, kidoku);

          if (!to->sys_)
            return lop::MakeResolvedFuture(*to->vm_.gc_);
          to->sys_->text().SetKidokuRead(already_read);

          if (to->localcfg_.multi_msg_mode) {
            auto window = to->sys_->text().GetCurrentMwnd();
            window->BeginMultiMessage();
            window->PrepareMessageText(
                [&text](TextPage& page) { page.Text(text); });
            return lop::MakeResolvedFuture(*to->vm_.gc_);
          }

          auto state = std::make_unique<lop::SiglusTextoutOperation>(
              to->sys_, std::move(text));
          return sr::Value(to->pending_.MakeFuture(
              to->vm_, std::move(state), to->sys_->event_ptr().get()));
        });
}

RLVM_REGISTER(SiglusBindingRegistry, "textout", BindTextout)

}  // namespace libsiglus::binding
