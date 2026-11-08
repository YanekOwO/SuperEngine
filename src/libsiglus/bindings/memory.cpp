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

#include "libsiglus/bindings/memory.hpp"
#include "vm/serialization.hpp"

#include "libsiglus/bindings/registry.hpp"

#include "core/memory_internal/proxy.hpp"
#include "libsiglus/bindings/properties.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/scene_entry.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "srbind/srbind.hpp"
#include "vm/exception.hpp"
#include "vm/list.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <any>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
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

static void PushBanks(MemoryHandle& handle,
                      const std::vector<int>& integers,
                      const std::vector<std::string>& strings) {
  handle.PushStackFrame(std::max(kMinimumCallStackBankSize, integers.size()),
                        std::max(kMinimumCallStackBankSize, strings.size()));
  try {
    for (std::size_t i = 0; i < integers.size(); ++i)
      handle.Write(IntBank::L, i, integers[i]);
    for (std::size_t i = 0; i < strings.size(); ++i)
      handle.Write(StrBank::K, i, strings[i]);
  } catch (...) {
    handle.PopStackFrame();
    throw;
  }
}

MemoryHandle MakeCallMemory(Memory& memory,
                            const std::vector<int>& integers,
                            const std::vector<std::string>& strings) {
  auto handle = memory.CreateHandle();
  PushBanks(handle, integers, strings);
  return handle;
}

static void PushCallFrame(Fiber& fiber,
                          Memory& memory,
                          const std::vector<Value>& integers,
                          const std::vector<Value>& strings) {
  // Convert before acquiring a frame so invalid arguments cannot leak banks.
  std::vector<int> largs;
  std::vector<std::string> kargs;
  for (const Value& value : integers)
    largs.push_back(RequireInt(value, "L frame argument"));
  for (const Value& value : strings)
    kargs.push_back(value.Str());
  PushBanks(GetFiberMemory(fiber, memory), largs, kargs);
}

std::vector<int>& IntBankHandle::Storage(Fiber& fiber) const {
  return GetFiberMemory(fiber, *memory).GetIntBankData(bank);
}
std::vector<std::string>& StrBankHandle::Storage(Fiber& fiber) const {
  return GetFiberMemory(fiber, *memory).GetStrBankData(bank);
}

namespace {
std::size_t CheckIndex(int index) {
  if (index < 0)
    throw std::out_of_range("negative list index");
  return static_cast<std::size_t>(index);
}
std::size_t CheckSize(int size) {
  if (size < 0)
    throw std::invalid_argument("negative list size");
  return static_cast<std::size_t>(size);
}
int IntSize(std::size_t size) {
  if (size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::overflow_error("list size exceeds script integer range");
  return static_cast<int>(size);
}

template <typename Handle>
auto* ListData(SiglusMachine& machine, Fiber& fiber, const Value& value) {
  using Storage = decltype(std::declval<Handle>().Storage(fiber));
  auto* instance = value.Get_if<NativeInstance>();
  if (instance && instance->foreign &&
      instance->klass ==
          machine.class_registry.GetClass<Handle>().native_class())
    return &instance->GetForeign<Handle>()->Storage(fiber);
  return static_cast<std::remove_reference_t<Storage>*>(nullptr);
}

template <typename Handle>
void BindSequence(sb::class_<Handle>& list) {
  using Storage =
      std::remove_reference_t<decltype(std::declval<Handle>().Storage(
          std::declval<Fiber&>()))>;
  using Item = typename Storage::value_type;
  using Proxy =
      std::conditional_t<std::is_same_v<Item, int>, IntListProxy, StrListProxy>;
  list.def(
      "__getitem__",
      [](Handle* h, Fiber& fiber, int index) {
        return Proxy(h->Storage(fiber)).Get(CheckIndex(index));
      },
      sb::arg("idx"));
  list.def(
      "__setitem__",
      [](Handle* h, Fiber& fiber, int index, Item value) {
        Proxy(h->Storage(fiber)).Set(CheckIndex(index), value);
      },
      sb::arg("idx"), sb::arg("val"));
  list.def(
      "resize",
      [](Handle* h, Fiber& fiber, int size) {
        h->Storage(fiber).resize(CheckSize(size));
      },
      sb::arg("size"));
  list.def("size", [](Handle* h, Fiber& fiber) {
    return IntSize(h->Storage(fiber).size());
  });
  list.def("init", [](Handle* h, Fiber& fiber) {
    h->Storage(fiber).assign(CheckSize(h->InitialSize()), Item{});
  });
  const auto fill = [](Handle* h, Fiber& fiber, int begin, int end,
                       Item value) {
    if constexpr (std::is_same_v<Item, int>) {
      if (begin > end)
        return;
    }
    auto& data = h->Storage(fiber);
    auto first = CheckIndex(begin);
    auto last = CheckIndex(end);
    // Siglus integer fill includes its end; string fill is half-open.
    if constexpr (std::is_same_v<Item, int>)
      ++last;
    if (first > last || last > data.size())
      throw std::out_of_range("list fill range out of bounds");
    std::fill(data.begin() + first, data.begin() + last, value);
  };
  if constexpr (std::is_same_v<Item, int>) {
    list.def("fill", fill, sb::arg("begin"), sb::arg("end"),
             sb::arg("val") = 0);
    list.def(
        "Set",
        [](Handle* h, Fiber& fiber, int index, std::vector<Value> values) {
          const auto begin = CheckIndex(index);
          if (values.empty())
            return;
          std::vector<int> integers;
          integers.reserve(values.size());
          for (const auto& value : values)
            integers.push_back(RequireInt(value, "integer list Set"));
          auto& data = h->Storage(fiber);
          if (begin > data.size() || integers.size() > data.size() - begin)
            throw std::out_of_range("integer list Set range out of bounds");
          std::copy(integers.begin(), integers.end(), data.begin() + begin);
        },
        sb::arg("idx"), sb::vararg);
  } else {
    list.def("fill", fill, sb::arg("begin"), sb::arg("end"), sb::arg("val"));
  }
}

template <typename Handle>
void BindBits(sb::class_<Handle>& list, sb::class_<IntBitView> view) {
  list.add_gc_root(view);
  for (std::uint8_t bits : {1, 2, 4, 8, 16}) {
    const auto name = "b" + std::to_string(bits);
    list.prop_r(name.c_str(), [view, bits](Handle* h) mutable {
      IntListRef owner{[handle = *h](Fiber& fiber) -> std::vector<int>& {
        return handle.Storage(fiber);
      }};
      return Value(view.make_inst(std::move(owner), bits));
    });
  }
}

template <typename Handle>
void BindOwnedList(sb::class_<Handle>& list) {
  list.def_serialization(
      [](const Handle& h, ObjectWriter& out) { out.Put(h.cell); },
      [](ObjectReader& in) {
        return std::make_unique<Handle>(in.Get<PropertyStorage::Cell>());
      });
  BindSequence(list);
}
}  // namespace

int IntBitView::Get(Fiber& fiber, int index) const {
  return IntListProxy(owner.Storage(fiber)).Get(CheckIndex(index), bits);
}
void IntBitView::Set(Fiber& fiber, int index, int value) const {
  IntListProxy(owner.Storage(fiber)).Set(CheckIndex(index), value, bits);
}

std::vector<int>* IntListData(SiglusMachine& machine,
                              Fiber& fiber,
                              const Value& value) {
  if (auto* data = ListData<IntListHandle>(machine, fiber, value))
    return data;
  if (auto* data = ListData<IntBankHandle>(machine, fiber, value))
    return data;
  return ListData<IntListRef>(machine, fiber, value);
}
std::vector<std::string>* StrListData(SiglusMachine& machine,
                                      Fiber& fiber,
                                      const Value& value) {
  if (auto* data = ListData<StrListHandle>(machine, fiber, value))
    return data;
  return ListData<StrBankHandle>(machine, fiber, value);
}

void BindMemory(SiglusMachine& rt) {
  VM& vm = *rt.vm;
  sb::module_ m(vm.gc_.get(), rt.vm->globals_.get());
  if (!rt.host().memory)
    rt.host().memory = std::make_unique<Memory>();
  Memory& memory = *rt.host().memory;

  sb::class_<IntBitView> bits(m, "__SiglusIntBitView");
  RegisterBindingClass(rt, bits);
  bits.def("__getitem__", &IntBitView::Get, sb::arg("idx"));
  bits.def("__setitem__", &IntBitView::Set, sb::arg("idx"), sb::arg("val"));

  sb::class_<IntListHandle> ilist(m, "__SiglusIntList");
  RegisterBindingClass(rt, ilist);
  BindOwnedList(ilist);
  BindBits(ilist, bits);
  sb::class_<StrListHandle> slist(m, "__SiglusStrList");
  RegisterBindingClass(rt, slist);
  BindOwnedList(slist);

  sb::class_<IntBankHandle> ibank(m, "__SiglusIntBank");
  RegisterBindingClass(rt, ibank);
  BindSequence(ibank);
  BindBits(ibank, bits);
  sb::class_<StrBankHandle> sbank(m, "__SiglusStrBank");
  RegisterBindingClass(rt, sbank);
  BindSequence(sbank);
  sb::class_<IntListRef> borrowed(m, "__SiglusIntListRef");
  RegisterBindingClass(rt, borrowed);
  BindSequence(borrowed);
  BindBits(borrowed, bits);

  m.def(
      "make_intlist",
      [&rt](int size) {
        return Value(rt.class_registry.MakeInst<IntListHandle>(size));
      },
      sb::arg("size"));
  m.def(
      "make_strlist",
      [&rt](int size) {
        return Value(rt.class_registry.MakeInst<StrListHandle>(size));
      },
      sb::arg("size"));

  BindProperties(rt);

  // Binding symbols restore these singletons against the destination memory.
  auto bind_int_bank = [&](std::string_view name, IntBank bank) {
    (*vm.globals_)[std::string(name)] = ibank.make_inst(&memory, bank);
  };
  auto bind_str_bank = [&](std::string_view name, StrBank bank) {
    (*vm.globals_)[std::string(name)] = sbank.make_inst(&memory, bank);
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

  rt.reset_local_memory.emplace_back([&vm]() {
    for (Fiber* fiber : vm.fibres_) {
      if (auto* handle = std::any_cast<MemoryHandle>(&fiber->usrdata)) {
        handle->ResetStackMemory(
            StackMemory{.L = {std::vector<int>(kRootStackBankSize)},
                        .K = {std::vector<std::string>(kRootStackBankSize)}});
      }
    }
  });
  m.def(
      "__builtin_push_frame",
      [&memory](Fiber& fiber, Value newl, Value newk) {
        const List* largs = RequireList(newl, "L frame arguments");
        const List* kargs = RequireList(newk, "K frame arguments");

        PushCallFrame(fiber, memory, largs->items, kargs->items);
      },
      sb::arg("newl"), sb::arg("newk"));

  // Bind positional arguments without changing L/K banks. Script callers
  // release banks in their emitted guards; host entries own initial banks.
  m.def("__builtin_init_call", [](Fiber& fiber, Value entry, Value arguments) {
    const auto* descriptor = RequireList(entry, "scene entry");
    const auto* args = RequireList(arguments, "scene arguments");
    if (descriptor->items.size() != 2)
      throw RuntimeError("invalid scene entry descriptor");
    const int arity = RequireInt(descriptor->items[1], "scene arity");
    if (arity < 0 || args->items.size() != static_cast<std::size_t>(arity))
      throw RuntimeError("scene entry received an incorrect argument count");
    auto& locals = fiber.frames.back().fast_locals;
    if (kSceneArgumentSlot + args->items.size() > locals.size())
      throw RuntimeError("scene arguments exceed available local slots");
    for (std::size_t i = 0; i < args->items.size(); ++i)
      locals[kSceneArgumentSlot + i] = args->items[i];
  });

  m.def("__builtin_pop_frame", [&memory](Fiber& fiber) {
    MemoryHandle& handle = GetFiberMemory(fiber, memory);
    if (handle.StackDepth() <= 1)
      throw RuntimeError("Siglus call frame stack underflow");
    handle.PopStackFrame();
  });
}

RLVM_REGISTER(SiglusBindingRegistry, "0_memory", BindMemory)

}  // namespace libsiglus::binding
