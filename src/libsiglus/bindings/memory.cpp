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

#include "core/memory_internal/facade.hpp"
#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/property.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "log/domain_logger.hpp"
#include "srbind/srbind.hpp"
#include "vm/exception.hpp"
#include "vm/gc.hpp"
#include "vm/list.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <any>
#include <cstddef>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace libsiglus::binding {

namespace sb = srbind;
using namespace serilang;

constexpr std::size_t kRootStackBankSize = 2000;
constexpr std::size_t kMinimumCallStackBankSize = 8;

static MemoryHandle& GetFiberMemory(Fiber& fiber, Memory& memory) {
  auto* handle = std::any_cast<MemoryHandle>(&fiber.usrdata);
  if (!handle) {
    fiber.usrdata = memory.CreateHandle();
    handle = std::any_cast<MemoryHandle>(&fiber.usrdata);
    handle->PushStackFrame(kRootStackBankSize, kRootStackBankSize);
  }
  return *handle;
}

struct IntBitView {
  using reader_t = int (SiglusIntListFacade::*)(Fiber&, int);
  using writer_t = void (SiglusIntListFacade::*)(Fiber&, int, int);

  SiglusIntListFacade* owner;
  reader_t reader;
  writer_t writer;
};

void BindIntBitProperties(sb::module_& m,
                          sb::class_<SiglusIntListFacade>& ilist) {
  sb::class_<IntBitView> view(m, "__SiglusIntBitView");
  view.def(
      "__getitem__",
      +[](IntBitView* self, Fiber& fiber, int index) -> int {
        return (*self->owner.*self->reader)(fiber, index);
      },
      sb::arg("idx"));
  view.def(
      "__setitem__",
      +[](IntBitView* self, Fiber& fiber, int index, int value) {
        (*self->owner.*self->writer)(fiber, index, value);
      },
      sb::arg("idx"), sb::arg("val"));

  struct BitProperty {
    const char* name;
    IntBitView::reader_t reader;
    IntBitView::writer_t writer;
  };
  constexpr BitProperty properties[] = {
      {"b1", &SiglusIntListFacade::b1, &SiglusIntListFacade::write_b1},
      {"b2", &SiglusIntListFacade::b2, &SiglusIntListFacade::write_b2},
      {"b4", &SiglusIntListFacade::b4, &SiglusIntListFacade::write_b4},
      {"b8", &SiglusIntListFacade::b8, &SiglusIntListFacade::write_b8},
      {"b16", &SiglusIntListFacade::b16, &SiglusIntListFacade::write_b16},
  };
  for (const auto& [name, reader, writer] : properties) {
    ilist.prop_r(name,
                 [view, reader, writer](SiglusIntListFacade* self) mutable {
                   NativeInstance* inst = view.make_inst(self, reader, writer);
                   return Value(inst);
                 });
  }
}

void BindIntSequenceMethods(sb::class_<SiglusIntListFacade>& ilist) {
  ilist.def("__getitem__", &SiglusIntListFacade::get, sb::arg("idx"));
  ilist.def("__setitem__", &SiglusIntListFacade::set, sb::arg("idx"),
            sb::arg("val"));
  ilist.def(
      "Set",
      +[](SiglusIntListFacade* self, Fiber& fib, int idx,
          std::vector<Value> values) {
        std::vector<int> vals;
        vals.reserve(values.size());
        for (Value v : values)
          vals.emplace_back(RequireInt(v, "integer list Set"));
        self->Set(fib, idx, std::move(vals));
      },
      sb::arg("idx"), sb::vararg);
  ilist.def("resize", &SiglusIntListFacade::resize, sb::arg("size"));
  ilist.def("size", &SiglusIntListFacade::size);
  ilist.def(
      "fill",
      +[](SiglusIntListFacade* self, Fiber& fib, int begin, int end,
          int value) {
        // Siglus includes the end index; the facade uses a half-open range.
        if (begin > end)
          return;
        const int size = self->size(fib);
        if (end >= size)
          throw std::out_of_range(std::format(
              "integer list fill end {} out of range for size {}", end, size));
        self->fill(fib, begin, end + 1, value);
      },
      sb::arg("begin"), sb::arg("end"), sb::arg("val") = 0);
}

void BindStrSequenceMethods(sb::class_<SiglusStrListFacade>& slist) {
  slist.def("__getitem__", &SiglusStrListFacade::get, sb::arg("idx"));
  slist.def("__setitem__", &SiglusStrListFacade::set, sb::arg("idx"),
            sb::arg("val"));
  slist.def("resize", &SiglusStrListFacade::resize, sb::arg("size"));
  slist.def("size", &SiglusStrListFacade::size);
  slist.def("fill", &SiglusStrListFacade::fill, sb::arg("begin"),
            sb::arg("end"), sb::arg("val"));
}

void BindMemory(SiglusMachine& rt) {
  VM& vm = *rt.vm;
  sb::module_ m(vm.gc_.get(), rt.vm->globals_.get());
  if (!rt.memory)
    rt.memory = std::make_unique<Memory>();
  Memory& memory = *rt.memory;

  auto ilist =
      std::make_shared<sb::class_<SiglusIntListFacade>>(m, "__SiglusIntList");
  rt.ilist_cls = ilist;
  BindIntSequenceMethods(*ilist);
  BindIntBitProperties(m, *ilist);
  ilist->def("init", &SiglusIntListFacade::init);

  auto slist =
      std::make_shared<sb::class_<SiglusStrListFacade>>(m, "__SiglusStrList");
  rt.slist_cls = slist;
  BindStrSequenceMethods(*slist);
  slist->def("init", &SiglusStrListFacade::init);

  m.def(
      "make_intlist",
      [cls = ilist](VM& vm, int size) -> Value {
        auto getter = [storage = std::vector<int>()](
                          Fiber&) mutable -> std::vector<int>& {
          return storage;
        };
        return cls->make_inst(std::move(getter), size);
      },
      sb::arg("size"));
  m.def(
      "make_strlist",
      [cls = slist](VM& vm, int size) -> Value {
        auto getter = [storage = std::vector<std::string>()](
                          Fiber&) mutable -> std::vector<std::string>& {
          return storage;
        };
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
    auto getter = [&memory, bank](Fiber& fiber) -> std::vector<int>& {
      return GetFiberMemory(fiber, memory).GetIntBankData(bank);
    };
    ilist->inst(name, std::move(getter), 0);
  };
  auto bind_str_bank = [&](std::string_view name, StrBank bank) {
    auto getter = [&memory, bank](Fiber& fiber) -> std::vector<std::string>& {
      return GetFiberMemory(fiber, memory).GetStrBankData(bank);
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

  rt.reset_local_memory.emplace_back([&memory, &vm, gameexe = rt.gameexe]() {
    memory.PartialReset(LocalMemory{});
    for (Fiber* fiber : vm.fibres_) {
      if (auto* handle = std::any_cast<MemoryHandle>(&fiber->usrdata)) {
        handle->ResetStackMemory(
            StackMemory{.L = {std::vector<int>(kRootStackBankSize)},
                        .K = {std::vector<std::string>(kRootStackBankSize)}});
      }
    }
    if (gameexe)
      memory.LoadFrom(*gameexe);
  });
  m.def(
      "__builtin_push_frame",
      [&memory](Fiber& fiber, Value newl, Value newk) {
        const List* largs = RequireList(newl, "L frame arguments");
        const List* kargs = RequireList(newk, "K frame arguments");

        MemoryHandle& handle = GetFiberMemory(fiber, memory);
        handle.PushStackFrame(
            std::max(kMinimumCallStackBankSize, largs->items.size()),
            std::max(kMinimumCallStackBankSize, kargs->items.size()));

        for (std::size_t i = 0; i < largs->items.size(); ++i)
          handle.Write(IntBank::L, i,
                       RequireInt(largs->items[i], "L frame argument"));
        for (std::size_t i = 0; i < kargs->items.size(); ++i)
          handle.Write(StrBank::K, i, kargs->items[i].Str());
      },
      sb::arg("newl"), sb::arg("newk"));

  m.def("__builtin_pop_frame", [&memory](Fiber& fiber) {
    MemoryHandle& handle = GetFiberMemory(fiber, memory);
    if (handle.StackDepth() <= 1)
      throw RuntimeError("Siglus call frame stack underflow");
    handle.PopStackFrame();
  });
}

RLVM_REGISTER(SiglusBindingRegistry, "0_memory", BindMemory)

}  // namespace libsiglus::binding
