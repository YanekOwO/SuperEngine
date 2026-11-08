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
#include "libsiglus/bindings/memory.hpp"

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

void BindProperties(SiglusMachine& machine) {
  auto& vm = *machine.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  PropertyStorage& storage = machine.host().properties;
  m.def(
      "__builtin_load_prop",
      [&storage, &registry = machine.class_registry](sr::VM& vm, int scene,
                                                     int index) -> sr::Value {
        auto cell = storage.GetCell(scene, index);
        return std::visit(
            [&](const auto& value) -> sr::Value {
              using T = std::decay_t<decltype(value)>;
              if constexpr (std::same_as<T, std::monostate>) {
                return sr::nil;
              } else if constexpr (std::same_as<T, std::string>) {
                return vm.gc_->Allocate<sr::String>(value);
              } else if constexpr (std::same_as<T, PropertyStorage::Integers>) {
                return registry.MakeInst<IntListHandle>(cell);
              } else if constexpr (std::same_as<T, PropertyStorage::Strings>) {
                return registry.MakeInst<StrListHandle>(cell);
              } else {
                return value;
              }
            },
            cell->value);
      });
  m.def(
      "__builtin_store_prop", [&machine, &storage](sr::Fiber& fiber, int scene,
                                                   int index, sr::Value value) {
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
        else if (const auto* integers = IntListData(machine, fiber, value))
          destination = *integers;
        else if (const auto* strings = StrListData(machine, fiber, value))
          destination = *strings;
        else
          throw sr::RuntimeError("unsupported property value: " + value.Desc());
      });
}

}  // namespace libsiglus::binding
