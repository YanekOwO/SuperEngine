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

#include "core/memory_internal/facade.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "srbind/srbind.hpp"
#include "vm/exception.hpp"
#include "vm/string.hpp"

#include <concepts>
#include <type_traits>
#include <utility>

namespace libsiglus::binding {
namespace sr = serilang;
namespace sb = srbind;

template <class Values>
static Values& GetList(PropertyStorage& storage, int scene, int index) {
  auto* values = std::get_if<Values>(&storage.Get(scene, index).value);
  if (!values)
    throw sr::RuntimeError(
        "property no longer contains the expected list type");
  return *values;
}

template <class Values, class Facade>
static Values CopyList(Facade& source, sr::Fiber& fiber) {
  const int size = source.size(fiber);
  Values values;
  values.reserve(size);
  for (int i = 0; i < size; ++i)
    values.push_back(source.get(fiber, i));
  return values;
}

void BindProperties(SiglusMachine& machine) {
  auto& vm = *machine.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  auto integers = machine.ilist_cls;
  auto strings = machine.slist_cls;
  // Native helper captures are opaque to the GC. Retain their list classes
  // even if script removes the public class names.
  (*vm.builtins_)["%%siglus_property_integers"] = integers->native_class();
  (*vm.builtins_)["%%siglus_property_strings"] = strings->native_class();

  PropertyStorage& storage = machine.host().properties;
  m.def(
      "__builtin_load_prop",
      [&storage, integers, strings](sr::VM& vm, int scene,
                                    int index) -> sr::Value {
        const auto& entry = storage.Get(scene, index);
        return std::visit(
            [&](const auto& value) -> sr::Value {
              using T = std::decay_t<decltype(value)>;
              if constexpr (std::same_as<T, std::monostate>) {
                return sr::nil;
              } else if constexpr (std::same_as<T, std::string>) {
                return vm.gc_->Allocate<sr::String>(value);
              } else if constexpr (std::same_as<T, PropertyStorage::Integers>) {
                auto getter = [&storage, scene, index](sr::Fiber&) -> T& {
                  return GetList<T>(storage, scene, index);
                };
                return integers->make_inst(std::move(getter),
                                           entry.initial_size,
                                           ListInitialization::Ready);
              } else if constexpr (std::same_as<T, PropertyStorage::Strings>) {
                auto getter = [&storage, scene, index](sr::Fiber&) -> T& {
                  return GetList<T>(storage, scene, index);
                };
                return strings->make_inst(std::move(getter), entry.initial_size,
                                          ListInitialization::Ready);
              } else {
                return value;
              }
            },
            entry.value);
      });
  m.def("__builtin_store_prop", [&storage, integers, strings](
                                    sr::Fiber& fiber, int scene, int index,
                                    sr::Value value) {
    auto& destination = storage.Get(scene, index).value;
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
             native->klass == integers->native_class())
      destination = CopyList<PropertyStorage::Integers>(
          *native->GetForeign<SiglusIntListFacade>(), fiber);
    else if (const auto* native = value.Get_if<sr::NativeInstance>();
             native && native->foreign &&
             native->klass == strings->native_class())
      destination = CopyList<PropertyStorage::Strings>(
          *native->GetForeign<SiglusStrListFacade>(), fiber);
    else
      throw sr::RuntimeError("unsupported property value: " + value.Desc());
  });
}

}  // namespace libsiglus::binding
