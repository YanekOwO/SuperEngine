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

#include "libsiglus/bindings/database.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "vm/serialization.hpp"

#include "core/database.hpp"
#include "srbind/module.hpp"
#include "srbind/srbind.hpp"
#include "vm/vm.hpp"

namespace libsiglus::binding {

void BindDatabase(SiglusMachine& runtime) {
  if (!runtime.host().databases)
    throw std::runtime_error("database binding requires initialized databases");
  srbind::module_ m(runtime.vm->gc_.get(), runtime.vm->globals_.get());

  srbind::class_<DatabaseHandle> table(m, "Database");
  RegisterBindingClass(runtime, table);
  table.def_serialization(
      [&runtime](const DatabaseHandle& h, serilang::ObjectWriter& out) {
        if (runtime.host().databases)
          for (int i = 0; i < runtime.host().databases->size(); ++i)
            if (runtime.host().databases->At(i) == h.table) {
              out.Put(i);
              return;
            }
        throw std::runtime_error("saved database has no archive identity");
      },
      [&runtime](serilang::ObjectReader& in) {
        auto& host = runtime.host();
        if (!host.databases)
          throw std::runtime_error("saved database unavailable");
        return std::make_unique<DatabaseHandle>(
            host.databases->At(in.Get<int>()));
      });

  table.def("get_num", [](const DatabaseHandle* h, int item, int column) {
    return h->table->GetNum(item, column);
  });
  table.def("get_str", [](const DatabaseHandle* h, int item, int column) {
    return h->table->GetStr(item, column);
  });
  table.def("check_item", [](const DatabaseHandle* h, int item) {
    return h->table->CheckItem(item);
  });
  table.def("check_column", [](const DatabaseHandle* h, int column) {
    return h->table->CheckColumn(column);
  });
  table.def("find_num", [](const DatabaseHandle* h, int column, int value) {
    return h->table->FindNum(column, value);
  });
  table.def("find_str",
            [](const DatabaseHandle* h, int column, std::string value) {
              return h->table->FindStr(column, value);
            });
  table.def("find_str_real",
            [](const DatabaseHandle* h, int column, std::string value) {
              return h->table->FindStrReal(column, value);
            });

  srbind::class_<DatabaseListHandle> list_class(m, "DatabaseList");
  RegisterBindingClass(runtime, list_class);
  list_class.def_serialization(
      [](const DatabaseListHandle& h, serilang::ObjectWriter& out) {},
      [&runtime](serilang::ObjectReader& in) {
        auto& host = runtime.host();
        return std::make_unique<DatabaseListHandle>(host.databases);
      });

  auto list = list_class.inst("database", runtime.host().databases);
  list.def("size", [](const DatabaseListHandle* h) { return h->list->size(); });
  list.def("__getitem__",
           [table](const DatabaseListHandle* h, int index) mutable {
             return serilang::Value(table.make_inst(h->list->At(index)));
           });
}

RLVM_REGISTER(SiglusBindingRegistry, "1_database", BindDatabase)

}  // namespace libsiglus::binding
