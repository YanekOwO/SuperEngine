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

#include "libsiglus/bindings/registry.hpp"

#include "core/database.hpp"
#include "srbind/module.hpp"
#include "srbind/srbind.hpp"
#include "vm/vm.hpp"

namespace libsiglus::binding {

void BindDatabase(SiglusMachine& runtime) {
  if (!runtime.host().databases)
    throw std::runtime_error("database binding requires initialized databases");
  srbind::module_ m(runtime.vm->gc_.get(), runtime.vm->globals_.get());
  struct Handle {
    std::shared_ptr<const Database> table;
  };
  srbind::class_<Handle> table(m, "Database");
  table.def("get_num", [](const Handle* h, int item, int column) {
    return h->table->GetNum(item, column);
  });
  table.def("get_str", [](const Handle* h, int item, int column) {
    return h->table->GetStr(item, column);
  });
  table.def("check_item", [](const Handle* h, int item) {
    return h->table->CheckItem(item);
  });
  table.def("check_column", [](const Handle* h, int column) {
    return h->table->CheckColumn(column);
  });
  table.def("find_num", [](const Handle* h, int column, int value) {
    return h->table->FindNum(column, value);
  });
  table.def("find_str", [](const Handle* h, int column, std::string value) {
    return h->table->FindStr(column, value);
  });
  table.def("find_str_real",
            [](const Handle* h, int column, std::string value) {
              return h->table->FindStrReal(column, value);
            });

  struct ListHandle {
    std::shared_ptr<const DatabaseList> list;
  };
  srbind::class_<ListHandle> list_class(m, "DatabaseList");
  auto list = list_class.inst("database", runtime.host().databases);
  list.def("size", [](const ListHandle* h) { return h->list->size(); });
  list.def("__getitem__", [table](const ListHandle* h, int index) mutable {
    return serilang::Value(table.make_inst(h->list->At(index)));
  });
}

RLVM_REGISTER(SiglusBindingRegistry, "1_database", BindDatabase)

}  // namespace libsiglus::binding
