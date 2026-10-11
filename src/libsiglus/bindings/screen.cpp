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

#include "libsiglus/bindings/bootstrap.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/bindings/wait_helpers.hpp"
#include "srbind/srbind.hpp"
#include "vm/value.hpp"
#include "vm/vm.hpp"

#include <vector>

namespace libsiglus::binding {
namespace sb = srbind;
namespace sr = serilang;

namespace {

class SiglusQuake {
 public:
  void start(std::vector<sr::Value>) { running_ = false; }
  void start_nowait(std::vector<sr::Value> args) { start(std::move(args)); }
  void start_all(std::vector<sr::Value> args) { start(std::move(args)); }
  void start_all_nowait(std::vector<sr::Value> args) { start(std::move(args)); }
  void end(std::vector<sr::Value>) { running_ = false; }

  sr::Value start_wait(sr::VM& vm, std::vector<sr::Value> args) {
    start(std::move(args));
    return MakeResolvedFuture(*vm.gc_);
  }

  sr::Value start_wait_key(sr::VM& vm, std::vector<sr::Value> args) {
    start(std::move(args));
    return MakeResolvedFuture(*vm.gc_);
  }

  sr::Value start_all_wait(sr::VM& vm, std::vector<sr::Value> args) {
    start(std::move(args));
    return MakeResolvedFuture(*vm.gc_);
  }

  sr::Value start_all_wait_key(sr::VM& vm, std::vector<sr::Value> args) {
    start(std::move(args));
    return MakeResolvedFuture(*vm.gc_);
  }

  sr::Value wait(sr::VM& vm, std::vector<sr::Value>) {
    return MakeResolvedFuture(*vm.gc_);
  }

  sr::Value wait_key(sr::VM& vm, std::vector<sr::Value>) {
    return MakeResolvedFuture(*vm.gc_);
  }

  int check(std::vector<sr::Value>) const { return running_ ? 1 : 0; }

 private:
  bool running_ = false;
};

}  // namespace

void BindScreen(SiglusMachine& runtime) {
  auto& vm = *runtime.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  sb::class_<SiglusQuake> quake(m, "Quake");
  quake.def(sb::init<>())
      .def("start", &SiglusQuake::start, sb::vararg)
      .def("start_wait", &SiglusQuake::start_wait, sb::vararg)
      .def("start_wait_key", &SiglusQuake::start_wait_key, sb::vararg)
      .def("start_nowait", &SiglusQuake::start_nowait, sb::vararg)
      .def("start_all", &SiglusQuake::start_all, sb::vararg)
      .def("start_all_wait", &SiglusQuake::start_all_wait, sb::vararg)
      .def("start_all_wait_key", &SiglusQuake::start_all_wait_key, sb::vararg)
      .def("start_all_nowait", &SiglusQuake::start_all_nowait, sb::vararg)
      .def("end", &SiglusQuake::end, sb::vararg)
      .def("wait", &SiglusQuake::wait, sb::vararg)
      .def("wait_key", &SiglusQuake::wait_key, sb::vararg)
      .def("check", &SiglusQuake::check, sb::vararg);

  std::string src = std::format(kLazyArrayClass, "__SiglusLazyArray");
  src += R"(
class Screen {
  fn __init__(self){
    self.quake = __SiglusLazyArray(Quake);
    self.effect = nil;
    try{ self.effect = __SiglusLazyArray(Effect); }
    catch(e){ print("Siglus screen.effect binding unavailable:", e); }
  }
}

screen = Screen();
)";
  Execute(vm, std::move(src));
}

RLVM_REGISTER(SiglusBindingRegistry, "1_screen", BindScreen)

}  // namespace libsiglus::binding
