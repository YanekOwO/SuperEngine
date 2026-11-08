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

#pragma once

#include "libsiglus/siglus_runtime.hpp"
#include "utilities/static_registry.hpp"
#include "vm/vm.hpp"

#include <functional>
#include <string>

namespace libsiglus::binding {

// Native wrappers held by the registry are opaque to the VM's collector.
// Keep their classes alive independently of script-visible global names.
template <typename T>
void RegisterBindingClass(SiglusMachine& machine, srbind::class_<T> klass) {
  (*machine.vm->builtins_)[std::string("%%siglus_class:") + typeid(T).name()] =
      klass.native_class();
  machine.class_registry.Register(std::move(klass));
}

struct SiglusBindingRegistryTag;
using BindFn = std::function<void(SiglusMachine&)>;
using SiglusBindingRegistry =
    StaticRegistry<SiglusBindingRegistryTag, std::string, BindFn>;

}  // namespace libsiglus::binding
