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

#include "libsiglus/bindings/properties.hpp"

#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "srbind/srbind.hpp"
#include "vm/exception.hpp"
#include "vm/string.hpp"

#include <type_traits>

namespace libsiglus::binding {
namespace sr = serilang;
namespace sb = srbind;
namespace {
struct PropertyTable {
  std::shared_ptr<PropertyStorage::Table> values;
};
}  // namespace

void BindProperties(SiglusMachine& machine) {
  auto& vm = *machine.vm;
  sb::module_ module(vm.gc_.get(), vm.globals_.get());
  sb::class_<PropertyTable> table(module, "__SiglusPropertyTable");
  table.add_gc_root(*machine.ilist_cls);
  table.add_gc_root(*machine.slist_cls);
  auto integers = *machine.ilist_cls;
  auto strings = *machine.slist_cls;
  table.def(
      "__getitem__",
      [integers, strings](PropertyTable* self, sr::VM& vm,
                          int index) mutable -> sr::Value {
        if (index < 0 ||
            static_cast<std::size_t>(index) >= self->values->size())
          throw sr::RuntimeError("property index out of range");
        return std::visit(
            [&](const auto& value) -> sr::Value {
              using T = std::decay_t<decltype(value)>;
              if constexpr (std::same_as<T, std::monostate>)
                return sr::nil;
              else if constexpr (std::same_as<T, int> ||
                                 std::same_as<T, bool> ||
                                 std::same_as<T, double>)
                return value;
              else if constexpr (std::same_as<T, std::string>)
                return vm.gc_->Allocate<sr::String>(value);
              else if constexpr (std::same_as<T, PropertyStorage::Integers>)
                return integers.make_inst(*value);
              else
                return strings.make_inst(*value);
            },
            self->values->at(index));
      });
  table.def("__setitem__", [integers, strings](PropertyTable* self, int index,
                                               sr::Value value) {
    if (index < 0 || static_cast<std::size_t>(index) >= self->values->size())
      throw sr::RuntimeError("property index out of range");
    auto& destination = self->values->at(index);
    if (const auto* boolean = value.Get_if<bool>())
      destination = *boolean;
    else if (const auto* real = value.Get_if<double>())
      destination = *real;
    else if (const auto* integer = value.Get_if<int>())
      destination = *integer;
    else if (const auto* string = value.Get_if<sr::String>())
      destination = string->str_;
    else if (value.Get_if<std::monostate>())
      destination = std::monostate{};
    else if (const auto* native = value.Get_if<sr::NativeInstance>();
             native && native->foreign &&
             native->klass == integers.native_class())
      destination = std::make_shared<SiglusIntListFacade>(
          *native->GetForeign<SiglusIntListFacade>());
    else if (const auto* native = value.Get_if<sr::NativeInstance>();
             native && native->foreign &&
             native->klass == strings.native_class())
      destination = std::make_shared<SiglusStrListFacade>(
          *native->GetForeign<SiglusStrListFacade>());
    else
      throw sr::RuntimeError("unsupported shared property value: " +
                             value.Desc());
  });
  table.def("len", [](PropertyTable* self) {
    return facade_detail::CheckedIntSize(self->values->size());
  });
  // Loader callbacks are C++ objects, so explicitly retain their VM class
  // roots.
  (*vm.builtins_)["%%siglus_property_class"] = table.native_class();
  auto make_table =
      [&machine, table](
          int scene,
          std::span<const Property> properties) mutable -> sr::Value {
    return table.make_inst(machine.host().properties.Get(scene, properties));
  };
  if (machine.loader)
    machine.loader->SetPropertyFactory(make_table);
  (*vm.globals_)["__globalprop"] = make_table(
      -1, machine.host().archive
              ? std::span<const Property>(machine.host().archive->prop_)
              : std::span<const Property>{});
}

}  // namespace libsiglus::binding
