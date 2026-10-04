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

#include "core/memory_internal/facade.hpp"
#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/property.hpp"
#include "log/domain_logger.hpp"
#include "srbind/srbind.hpp"
#include "vm/exception.hpp"
#include "vm/gc.hpp"
#include "vm/list.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace libsiglus::binding {

namespace sb = srbind;
using namespace serilang;

namespace {

void SetIntListValues(IntListFacade* self, int idx, std::vector<Value> values) {
  std::vector<int> ints;
  ints.reserve(values.size());
  for (Value& value : values)
    ints.push_back(RequireInt(value, "integer list Set"));
  self->Set(idx, std::move(ints));
}

void BindIntSequenceMethods(sb::class_<IntListFacade>& klass) {
  klass.def("__getitem__", &IntListFacade::get, sb::arg("idx"));
  klass.def("__setitem__", &IntListFacade::set, sb::arg("idx"), sb::arg("val"));
  klass.def("Set", SetIntListValues, sb::arg("idx"), sb::vararg);
  klass.def("resize", &IntListFacade::resize, sb::arg("size"));
  klass.def("size", &IntListFacade::size);
  klass.def("fill", &IntListFacade::fill, sb::arg("begin"), sb::arg("end"),
            sb::arg("val"));
  klass.def("b1", &IntListFacade::b1, sb::arg("idx"));
  klass.def("write_b1", &IntListFacade::write_b1, sb::arg("idx"),
            sb::arg("val"));
  klass.def("b2", &IntListFacade::b2, sb::arg("idx"));
  klass.def("write_b2", &IntListFacade::write_b2, sb::arg("idx"),
            sb::arg("val"));
  klass.def("b4", &IntListFacade::b4, sb::arg("idx"));
  klass.def("write_b4", &IntListFacade::write_b4, sb::arg("idx"),
            sb::arg("val"));
  klass.def("b8", &IntListFacade::b8, sb::arg("idx"));
  klass.def("write_b8", &IntListFacade::write_b8, sb::arg("idx"),
            sb::arg("val"));
  klass.def("b16", &IntListFacade::b16, sb::arg("idx"));
  klass.def("write_b16", &IntListFacade::write_b16, sb::arg("idx"),
            sb::arg("val"));
}

void BindStrSequenceMethods(sb::class_<StrListFacade>& klass) {
  klass.def("__getitem__", &StrListFacade::get, sb::arg("idx"));
  klass.def("__setitem__", &StrListFacade::set, sb::arg("idx"), sb::arg("val"));
  klass.def("resize", &StrListFacade::resize, sb::arg("size"));
  klass.def("size", &StrListFacade::size);
  klass.def("fill", &StrListFacade::fill, sb::arg("begin"), sb::arg("end"),
            sb::arg("val"));
}

}  // namespace

void BindMemory(SiglusRuntime& rt) {
  VM& vm = *rt.vm;
  sb::module_ m(vm.gc_.get(), rt.vm->globals_.get());
  if (!rt.memory)
    rt.memory = std::make_unique<Memory>();
  Memory& memory = *rt.memory;

  auto ilist =
      std::make_shared<sb::class_<IntListFacade>>(m, "__SiglusIntList");
  rt.ilist_cls = ilist;
  BindIntSequenceMethods(*ilist);
  ilist->def("init", &IntListFacade::init);

  auto slist =
      std::make_shared<sb::class_<StrListFacade>>(m, "__SiglusStrList");
  rt.slist_cls = slist;
  BindStrSequenceMethods(*slist);
  slist->def("init", &StrListFacade::init);

  m.def(
      "make_intlist",
      [cls = ilist](VM& vm, int size) -> Value {
        auto getter = [storage =
                           std::vector<int>()]() mutable -> std::vector<int>& {
          return storage;
        };
        return cls->make_inst(std::move(getter), size);
      },
      sb::arg("size"));
  m.def(
      "make_strlist",
      [cls = slist](VM& vm, int size) -> Value {
        auto getter = [storage = std::vector<std::string>()]() mutable
            -> std::vector<std::string>& { return storage; };
        return cls->make_inst(std::move(getter), size);
      },
      sb::arg("size"));

  std::string src = "__globalprop = [];\n";
  // install archive-global user properties
  if (rt.archive) {
    Archive& archive = *rt.archive;
    for (size_t i = 0; i < archive.prop_.size(); ++i) {
      Property& p = archive.prop_[i];
      switch (p.form) {
        case Type::Int:
          src += "__globalprop.append(0);";
          break;
        case Type::IntList:
          src += std::format("__globalprop.append(make_intlist({}));", p.size);
          break;
        case Type::String:
          src += "__globalprop.append(\"\");";
          break;
        case Type::StrList:
          src += std::format("__globalprop.append(make_strlist({}));", p.size);
          break;

        default: {
          static DomainLogger log("Memory");
          log(Severity::Error)
              << "failed to install global property: " << p.ToDebugString();
          src += "__globalprop.append(nil);\n";
          break;
        }
      }
    }
  }
  Execute(vm, std::move(src));

  auto bind_int_bank = [&](std::string_view name, IntBank bank) {
    auto getter = [&memory, bank]() -> std::vector<int>& {
      return memory.GetIntBankData(bank);
    };
    ilist->inst(name, std::move(getter), 0);
  };
  auto bind_str_bank = [&](std::string_view name, StrBank bank) {
    auto getter = [&memory, bank]() -> std::vector<std::string>& {
      return memory.GetStrBankData(bank);
    };
    slist->inst(name, std::move(getter), 0);
  };

  bind_int_bank("A", IntBank::A);
  bind_int_bank("B", IntBank::B);
  bind_int_bank("C", IntBank::C);
  bind_int_bank("D", IntBank::D);
  bind_int_bank("E", IntBank::E);
  bind_int_bank("F", IntBank::F);
  bind_int_bank("X", IntBank::X);
  bind_int_bank("G", IntBank::G);
  bind_int_bank("Z", IntBank::Z);
  bind_int_bank("L", IntBank::L);

  bind_str_bank("S", StrBank::S);
  bind_str_bank("M", StrBank::M);
  bind_str_bank("K", StrBank::K);
  bind_str_bank("LN", StrBank::local_name);
  bind_str_bank("GN", StrBank::global_name);

  auto frame_stack = std::make_shared<std::vector<Memory::Stack>>();
  rt.reset_local_memory.emplace_back(
      [&memory, frame_stack, gameexe = rt.gameexe]() {
        memory.PartialReset(LocalMemory{});
        memory.PartialReset(Memory::Stack{.L = std::vector<int>(2000),
                                          .K = std::vector<std::string>(2000)});
        frame_stack->clear();
        if (gameexe)
          memory.LoadFrom(*gameexe);
      });
  m.def(
      "__builtin_push_frame",
      [&memory, frame_stack](Value newl, Value newk) {
        const List* largs = RequireList(newl, "L frame arguments");
        const List* kargs = RequireList(newk, "K frame arguments");

        frame_stack->push_back(memory.GetStackMemory());
        Memory::Stack stack{.L = std::vector<int>(
                                std::max<std::size_t>(8, largs->items.size())),
                            .K = std::vector<std::string>(
                                std::max<std::size_t>(8, kargs->items.size()))};
        memory.PartialReset(std::move(stack));

        for (std::size_t i = 0; i < largs->items.size(); ++i)
          memory.Write(IntBank::L, i,
                       RequireInt(largs->items[i], "L frame argument"));
        for (std::size_t i = 0; i < kargs->items.size(); ++i)
          memory.Write(StrBank::K, i, kargs->items[i].Str());
      },
      sb::arg("newl"), sb::arg("newk"));

  m.def("__builtin_pop_frame", [&memory, frame_stack]() {
    if (frame_stack->empty())
      throw RuntimeError("Siglus call frame stack underflow");
    memory.PartialReset(std::move(frame_stack->back()));
    frame_stack->pop_back();
  });
}

RLVM_REGISTER(SiglusBindingRegistry, "0_memory", BindMemory)

}  // namespace libsiglus::binding
