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
#include "libsiglus/bindings/wait_helpers.hpp"
#include "srbind/module.hpp"
#include "systems/system.hpp"
#include "systems/text_page.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"
#include "vm/future.hpp"
#include "vm/vm.hpp"

#include <utf8.h>

#include <algorithm>
#include <chrono>
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

  struct Textout : public CoroutineTask {
    Textout(sr::VM& vm, System* system, LocalConfig& localcfg, std::string text)
        : CoroutineTask(vm, system ? system->event_ptr().get() : nullptr),
          system_(system),
          local_config_(localcfg),
          text_(text) {}

    TaskCoroutine Run() override {
      for (std::string::const_iterator cur = text_.cbegin(), next;
           cur != text_.cend(); cur = next) {
        if (!ShouldFlushText()) {
          const int speed = system_
                                ? EffectiveMessageSpeed(
                                      local_config_, system_->global_config())
                                : 0;

          const std::chrono::milliseconds duration(std::max(1, speed));
          if ((co_await WaitFor(duration, true)) ==
              WaitOutcome::InterruptedByInput) {
            flush_ = true;
          }
        }

        next = cur;
        utf8::next(next, text_.cend());

        const std::string current(cur, next);
        const std::string rest(next, text_.cend());
        TextPage& page = system_->text().GetCurrentPage();
        page.Character(current, rest);
      }

      co_return 0;
    }

    bool ShouldFlushText() const {
      if (!system_)
        return true;
      if (flush_)
        return true;
      if (system_->ShouldFastForward())
        return true;
      if (EffectiveMessageNowait(local_config_, system_->global_config()))
        return true;
      return false;
    }

    System* system_;
    LocalConfig& local_config_;
    std::string text_;
    bool flush_ = false;
  };

  PendingCoroutineTasks pending_;
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

    name = text.InterpretName(name);
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
            return MakeResolvedFuture(*to->vm_.gc_);
          to->sys_->text().SetKidokuRead(already_read);

          auto state = std::make_unique<SiglusTextout::Textout>(
              to->vm_, to->sys_, to->localcfg_, std::move(text));
          return sr::Value(
              to->pending_.MakeFuture(*to->vm_.gc_, std::move(state)));
        });
}

RLVM_REGISTER(SiglusBindingRegistry, "textout", BindTextout)

}  // namespace libsiglus::binding
