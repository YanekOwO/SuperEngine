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

#include "vm/transfer_value.hpp"

#include "vm/exception.hpp"
#include "vm/gc.hpp"
#include "vm/list.hpp"
#include "vm/string.hpp"

#include <concepts>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace serilang {

TransferValue TransferValue::Capture(const Value& value) {
  std::unordered_set<const serilang::List*> lists;
  auto dfs = [&](auto&& dfs, const Value& value) -> TransferValue {
    if (value.Get_if<std::monostate>())
      return {};
    if (const auto* item = value.Get_if<bool>())
      return {*item};
    if (const auto* item = value.Get_if<int>())
      return {*item};
    if (const auto* item = value.Get_if<double>())
      return {*item};
    if (const auto* item = value.Get_if<String>())
      return {item->str_};
    if (const auto* list = value.Get_if<serilang::List>()) {
      if (!lists.insert(list).second)
        throw RuntimeError("EX-call result contains a cyclic list");
      TransferValue::List result;
      result.reserve(list->items.size());
      for (const auto& item : list->items)
        result.push_back(dfs(dfs, item));
      lists.erase(list);
      return {std::move(result)};
    }
    throw RuntimeError("EX-call result contains an unsupported VM object");
  };
  return dfs(dfs, value);
}

Value TransferValue::Restore(GarbageCollector& gc) const {
  return std::visit(
      [&gc](const auto& item) -> Value {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::same_as<T, std::string>) {
          return gc.Allocate<String>(item);
        } else if constexpr (std::same_as<T, List>) {
          std::vector<Value> result;
          result.reserve(item.size());
          for (const auto& element : item)
            result.push_back(element.Restore(gc));
          return gc.Allocate<serilang::List>(std::move(result));
        } else {
          return item;
        }
      },
      value);
}

}  // namespace serilang
