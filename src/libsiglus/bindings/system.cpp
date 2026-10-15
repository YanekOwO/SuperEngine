// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
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

#include "systems/system.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/wait_helpers.hpp"
#include "log/domain_logger.hpp"
#include "srbind/srbind.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "vm/vm.hpp"

#include <ctime>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace libsiglus::binding {
namespace sb = srbind;
namespace sr = serilang;

namespace fs = std::filesystem;

static void nop() {}

void BindSystem(SiglusMachine& runtime) {
  sr::VM& vm = *runtime.vm;
  auto* sys = runtime.system.get();
  if (!sys)
    throw std::runtime_error("system bindings require a System object");

  sb::module_ m(vm, "system");
  m.def("is_debug", +[]() { return false; });
  m.def("time", +[]() { return static_cast<int>(std::time(nullptr)); });
  m.def("check_file_exist", [root = runtime.base_pth](std::string filename) {
    return fs::exists(root / filename);
  });
  m.def("check_save_file_exist",
        [root = runtime.save_pth](std::string filename) {
          return fs::exists(root / filename);
        });
  m.def("check_dummy", &nop).def("clear_dummy", &nop);
  m.def("debug_write_log",
        [logger = DomainLogger("SiglusDbg")](std::string msg) {
          logger(Severity::Info) << std::move(msg);
        });
  m.def("get_lang", +[]() { return "ja"; });

  sb::module_ gm(vm.gc_.get(), vm.globals_.get());
  gm.def(
      "wait",
      [&runtime, sys](sr::VM& vm, int msecs) -> sr::Value {
        if (!sys || msecs <= 0)
          return MakeResolvedFuture(*vm.gc_);

        auto counter = runtime.CreateCounter();
        counter->StartGame();
        auto done = [sys, counter = std::move(counter), msecs] {
          if (sys->ShouldFastForward())
            return true;

          return counter->Get() >= msecs;
        };
        return MakePollingWaitFuture(vm, std::move(done));
      },
      sb::arg("msecs") = 0);
  gm.def(
      "wait_key",
      [&runtime, sys](sr::VM& vm, int msecs) -> sr::Value {
        if (!sys)
          return MakeResolvedFuture(*vm.gc_);

        const bool has_timeout = msecs > 0;
        auto counter = has_timeout ? runtime.CreateCounter() : nullptr;
        if (counter)
          counter->StartGame();
        auto done = [sys, counter = std::move(counter), msecs] {
          if (sys->ShouldFastForward())
            return true;

          return counter && counter->Get() >= msecs;
        };
        return MakePollingWaitFuture(vm, std::move(done), true,
                                     sys->event_ptr().get());
      },
      sb::arg("msecs") = 0);
  gm.def("set_title", [sys](std::string title) {
    sys->graphics().SetWindowSubtitle(std::move(title));
  });
}

RLVM_REGISTER(SiglusBindingRegistry, "system", BindSystem)

}  // namespace libsiglus::binding
