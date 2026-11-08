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

#include "libsiglus/script_call.hpp"

#include "vm/gc.hpp"
#include "vm/list.hpp"
#include "vm/string.hpp"

#include <unordered_set>
#include <utility>

namespace libsiglus {
namespace sr = serilang;
namespace {
TransferValue Capture(const sr::Value& value,
                      std::unordered_set<const sr::List*>& lists) {
  if (value.Get_if<std::monostate>())
    return {};
  if (const auto* item = value.Get_if<bool>())
    return {*item};
  if (const auto* item = value.Get_if<int>())
    return {*item};
  if (const auto* item = value.Get_if<double>())
    return {*item};
  if (const auto* item = value.Get_if<sr::String>())
    return {item->str_};
  if (const auto* list = value.Get_if<sr::List>()) {
    if (!lists.insert(list).second)
      throw sr::RuntimeError("EX-call result contains a cyclic list");
    TransferValue::List result;
    result.reserve(list->items.size());
    for (const auto& item : list->items)
      result.push_back(Capture(item, lists));
    lists.erase(list);
    return {std::move(result)};
  }
  throw sr::RuntimeError("EX-call result contains an unsupported VM object");
}
}  // namespace

TransferValue TransferValue::Capture(const sr::Value& value) {
  std::unordered_set<const sr::List*> lists;
  return libsiglus::Capture(value, lists);
}

sr::Value TransferValue::Restore(sr::GarbageCollector& gc) const {
  return std::visit(
      [&gc](const auto& item) -> sr::Value {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::same_as<T, std::string>) {
          return gc.Allocate<sr::String>(item);
        } else if constexpr (std::same_as<T, List>) {
          std::vector<sr::Value> result;
          result.reserve(item.size());
          for (const auto& element : item)
            result.push_back(element.Restore(gc));
          return gc.Allocate<sr::List>(std::move(result));
        } else {
          return item;
        }
      },
      value);
}

std::vector<sr::Value> MakeScriptArguments(sr::GarbageCollector& gc,
                                           const ScriptCall& call) {
  if (!call.farcall)
    return {};
  std::vector<sr::Value> integers;
  for (int value : call.integers)
    integers.emplace_back(value);
  std::vector<sr::Value> strings;
  for (const auto& value : call.strings)
    strings.emplace_back(gc.Allocate<sr::String>(value));
  return {gc.Allocate<sr::List>(std::move(integers)),
          gc.Allocate<sr::List>(std::move(strings))};
}

}  // namespace libsiglus
