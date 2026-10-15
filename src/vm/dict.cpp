// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
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
//
// -----------------------------------------------------------------------

#include "vm/dict.hpp"

#include "vm/exception.hpp"
#include "vm/gc.hpp"
#include "vm/list.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

namespace serilang {

namespace {

enum class DictMethodKind {
  Clear,
  Copy,
  FromKeys,
  Get,
  Items,
  Keys,
  Pop,
  PopItem,
  SetDefault,
  Update,
  Values
};

bool RelaxedEqual(const Value& lhs, const Value& rhs) {
  try {
    return lhs == rhs;
  } catch (const RuntimeError&) {
    return false;
  }
}

void SyncOrder(Dict& dict) {
  std::vector<Value> normalized;
  normalized.reserve(dict.map.size());

  for (const Value& key : dict.order) {
    auto it = dict.map.find(key);
    if (it == dict.map.end())
      continue;

    const bool present = std::ranges::any_of(
        normalized, [&](const Value& v) { return RelaxedEqual(v, key); });
    if (!present)
      normalized.push_back(key);
  }

  for (const auto& [key, value] : dict.map) {
    const bool present = std::ranges::any_of(
        normalized, [&](const Value& v) { return RelaxedEqual(v, key); });
    if (!present)
      normalized.push_back(key);
  }

  dict.order = std::move(normalized);
}

void AssignOrdered(Dict::map_t& map,
                   std::vector<Value>& order,
                   Value key,
                   Value value) {
  auto it = map.find(key);
  if (it == map.end()) {
    order.push_back(key);
    map.emplace(std::move(key), std::move(value));
    return;
  }

  it->second = std::move(value);
}

std::optional<Value> RemoveOrdered(Dict& dict, const Value& key) {
  SyncOrder(dict);

  auto it = dict.map.find(key);
  if (it == dict.map.end())
    return std::nullopt;

  Value value = std::move(it->second);
  dict.map.erase(it);

  auto order_it = std::ranges::find_if(dict.order, [&](const Value& current) {
    return RelaxedEqual(current, key);
  });
  if (order_it != dict.order.end())
    dict.order.erase(order_it);

  return value;
}

void AppendStringChars(VM& vm, const String& str, std::vector<Value>& out) {
  out.reserve(out.size() + str.str_.size());
  for (char ch : str.str_)
    out.emplace_back(vm.gc_->Allocate<String>(std::string(1, ch)));
}

void AppendDictKeys(Dict& dict, std::vector<Value>& out) {
  SyncOrder(dict);
  out.insert(out.end(), dict.order.begin(), dict.order.end());
}

bool ExpandKeys(VM& vm,
                Fiber& f,
                Value& iterable,
                std::vector<Value>& out,
                std::string_view method_name) {
  if (auto* list = iterable.Get_if<List>()) {
    out.insert(out.end(), list->items.begin(), list->items.end());
    return true;
  }

  if (auto* dict = iterable.Get_if<Dict>()) {
    AppendDictKeys(*dict, out);
    return true;
  }

  if (auto* str = iterable.Get_if<String>()) {
    AppendStringChars(vm, *str, out);
    return true;
  }

  vm.Error(
      f, std::format("{} expects a list, dict, or string iterable, but got: {}",
                     method_name, iterable.Desc()));
  return false;
}

Value MakePair(VM& vm, Value lhs, Value rhs) {
  // The VM does not have a tuple object yet, so pair-like results are
  // represented as 2-element lists.
  return Value(vm.gc_->Allocate<List>(
      std::vector<Value>{std::move(lhs), std::move(rhs)}));
}

bool UpdateFromValue(VM& vm, Fiber& f, Dict& target, Value& source) {
  try {
    SyncOrder(target);

    if (auto* dict = source.Get_if<Dict>()) {
      SyncOrder(*dict);
      for (const Value& key : dict->order) {
        auto it = dict->map.find(key);
        if (it != dict->map.end())
          AssignOrdered(target.map, target.order, key, it->second);
      }
      return true;
    }

    if (auto* list = source.Get_if<List>()) {
      for (Value& item : list->items) {
        auto* pair = item.Get_if<List>();
        if (!pair || pair->items.size() != 2) {
          vm.Error(f, "dict.update expects a list of [key, value] pairs");
          return false;
        }
        AssignOrdered(target.map, target.order, pair->items[0], pair->items[1]);
      }
      return true;
    }
  } catch (const RuntimeError& e) {
    vm.Error(f, e.message());
    return false;
  }

  vm.Error(f, "dict.update expects a dict or a list of [key, value] pairs");
  return false;
}

class DictMethod final : public IObject {
 public:
  static constexpr inline ObjType objtype = ObjType::BoundMethod;

  DictMethod(Dict* self, DictMethodKind kind) : self_(self), kind_(kind) {}

  constexpr ObjType Type() const noexcept final { return objtype; }
  constexpr size_t Size() const noexcept final { return sizeof(*this); }

  void MarkRoots(GCVisitor& visitor) override { visitor.MarkSub(self_); }

  std::string Str() const override { return Desc(); }
  std::string Desc() const override {
    return std::format("<bound method dict.{}>", MethodName());
  }

  void Call(VM& vm,
            Fiber& f,
            uint8_t nargs,
            uint8_t nkwargs,
            CallResultMode mode = CallResultMode::Keep) override {
    const size_t base = f.op_stack.size() - nargs - 2 * nkwargs - 1;

    auto fail = [](std::string msg) { throw RuntimeError(std::move(msg)); };

    auto finish = [&](Value result = nil) {
      if (mode == CallResultMode::Discard) {
        f.op_stack.resize(base);
        return;
      }
      f.op_stack.resize(base + 1);
      f.op_stack.back() = std::move(result);
    };

    auto require_no_kwargs = [&]() -> bool {
      if (nkwargs == 0)
        return true;
      fail(std::format("dict.{} does not accept keyword arguments",
                       MethodName()));
      return false;
    };

    auto require_nargs = [&](uint8_t expected) -> bool {
      if (nargs == expected)
        return true;
      fail(std::format("dict.{} expects {} argument{}, got {}", MethodName(),
                       expected, expected == 1 ? "" : "s", nargs));
      return false;
    };

    try {
      switch (kind_) {
        case DictMethodKind::Clear: {
          if (!require_no_kwargs() || !require_nargs(0))
            return;
          self_->map.clear();
          self_->order.clear();
          finish();
          return;
        }

        case DictMethodKind::Copy: {
          if (!require_no_kwargs() || !require_nargs(0))
            return;
          SyncOrder(*self_);
          finish(Value(vm.gc_->Allocate<Dict>(self_->map, self_->order)));
          return;
        }

        case DictMethodKind::FromKeys: {
          if (!require_no_kwargs() || (nargs != 1 && nargs != 2)) {
            if (nargs == 1 || nargs == 2)
              return;
            fail(std::format("dict.fromkeys expects 1 or 2 arguments, got {}",
                             nargs));
            return;
          }

          std::vector<Value> keys;
          if (!ExpandKeys(vm, f, f.op_stack[base + 1], keys, "dict.fromkeys"))
            return;

          Value fill = nargs == 2 ? f.op_stack[base + 2] : nil;
          Dict::map_t map;
          std::vector<Value> order;
          for (const Value& key : keys)
            AssignOrdered(map, order, key, fill);
          finish(
              Value(vm.gc_->Allocate<Dict>(std::move(map), std::move(order))));
          return;
        }

        case DictMethodKind::Get: {
          if (!require_no_kwargs() || (nargs != 1 && nargs != 2)) {
            if (nargs == 1 || nargs == 2)
              return;
            fail(std::format("dict.get expects 1 or 2 arguments, got {}",
                             nargs));
            return;
          }

          auto it = self_->map.find(f.op_stack[base + 1]);
          if (it == self_->map.end()) {
            finish(nargs == 2 ? f.op_stack[base + 2] : nil);
            return;
          }
          finish(it->second);
          return;
        }

        case DictMethodKind::Items: {
          if (!require_no_kwargs() || !require_nargs(0))
            return;
          SyncOrder(*self_);
          std::vector<Value> items;
          items.reserve(self_->order.size());
          for (const Value& key : self_->order)
            items.push_back(MakePair(vm, key, self_->map.at(key)));
          finish(Value(vm.gc_->Allocate<List>(std::move(items))));
          return;
        }

        case DictMethodKind::Keys: {
          if (!require_no_kwargs() || !require_nargs(0))
            return;
          SyncOrder(*self_);
          finish(Value(vm.gc_->Allocate<List>(self_->order)));
          return;
        }

        case DictMethodKind::Pop: {
          if (!require_no_kwargs() || (nargs != 1 && nargs != 2)) {
            if (nargs == 1 || nargs == 2)
              return;
            fail(std::format("dict.pop expects 1 or 2 arguments, got {}",
                             nargs));
            return;
          }

          auto removed = RemoveOrdered(*self_, f.op_stack[base + 1]);
          if (removed) {
            finish(std::move(*removed));
            return;
          }

          if (nargs == 2) {
            finish(f.op_stack[base + 2]);
            return;
          }

          fail(std::format("dictionary has no key: {}",
                           f.op_stack[base + 1].Str()));
          return;
        }

        case DictMethodKind::PopItem: {
          if (!require_no_kwargs() || !require_nargs(0))
            return;
          SyncOrder(*self_);
          if (self_->order.empty()) {
            fail("dict.popitem(): dictionary is empty");
            return;
          }

          Value key = self_->order.back();
          Value value = self_->map.at(key);
          std::ignore = RemoveOrdered(*self_, key);
          finish(MakePair(vm, std::move(key), std::move(value)));
          return;
        }

        case DictMethodKind::SetDefault: {
          if (!require_no_kwargs() || (nargs != 1 && nargs != 2)) {
            if (nargs == 1 || nargs == 2)
              return;
            fail(std::format("dict.setdefault expects 1 or 2 arguments, got {}",
                             nargs));
            return;
          }

          auto it = self_->map.find(f.op_stack[base + 1]);
          if (it != self_->map.end()) {
            finish(it->second);
            return;
          }

          SyncOrder(*self_);
          Value value = nargs == 2 ? f.op_stack[base + 2] : nil;
          AssignOrdered(self_->map, self_->order, f.op_stack[base + 1], value);
          finish(std::move(value));
          return;
        }

        case DictMethodKind::Update: {
          if (nargs > 1) {
            fail(std::format("dict.update expects at most 1 argument, got {}",
                             nargs));
            return;
          }

          if (nargs == 1 &&
              !UpdateFromValue(vm, f, *self_, f.op_stack[base + 1]))
            return;

          SyncOrder(*self_);
          for (uint8_t i = 0; i < nkwargs; ++i) {
            const size_t key_idx = base + 1 + nargs + 2 * i;
            AssignOrdered(self_->map, self_->order, f.op_stack[key_idx],
                          f.op_stack[key_idx + 1]);
          }
          finish();
          return;
        }

        case DictMethodKind::Values: {
          if (!require_no_kwargs() || !require_nargs(0))
            return;
          SyncOrder(*self_);
          std::vector<Value> values;
          values.reserve(self_->order.size());
          for (const Value& key : self_->order)
            values.push_back(self_->map.at(key));
          finish(Value(vm.gc_->Allocate<List>(std::move(values))));
          return;
        }
      }
    } catch (const RuntimeError& e) {
      fail(e.message());
    }
  }

 private:
  std::string_view MethodName() const {
    switch (kind_) {
      case DictMethodKind::Clear:
        return "clear";
      case DictMethodKind::Copy:
        return "copy";
      case DictMethodKind::FromKeys:
        return "fromkeys";
      case DictMethodKind::Get:
        return "get";
      case DictMethodKind::Items:
        return "items";
      case DictMethodKind::Keys:
        return "keys";
      case DictMethodKind::Pop:
        return "pop";
      case DictMethodKind::PopItem:
        return "popitem";
      case DictMethodKind::SetDefault:
        return "setdefault";
      case DictMethodKind::Update:
        return "update";
      case DictMethodKind::Values:
        return "values";
    }
    return "";
  }

  Dict* self_;
  DictMethodKind kind_;
};

}  // namespace

Dict::Dict(map_t m, std::vector<Value> order_)
    : map(std::move(m)), order(std::move(order_)) {
  if (order.empty()) {
    order.reserve(map.size());
    for (const auto& [key, value] : map)
      order.push_back(key);
  }
}

TempValue Dict::GetMember(std::string_view mem) {
  if (mem == "clear")
    return std::make_unique<DictMethod>(this, DictMethodKind::Clear);
  if (mem == "copy")
    return std::make_unique<DictMethod>(this, DictMethodKind::Copy);
  if (mem == "fromkeys")
    return std::make_unique<DictMethod>(this, DictMethodKind::FromKeys);
  if (mem == "get")
    return std::make_unique<DictMethod>(this, DictMethodKind::Get);
  if (mem == "items")
    return std::make_unique<DictMethod>(this, DictMethodKind::Items);
  if (mem == "keys")
    return std::make_unique<DictMethod>(this, DictMethodKind::Keys);
  if (mem == "pop")
    return std::make_unique<DictMethod>(this, DictMethodKind::Pop);
  if (mem == "popitem")
    return std::make_unique<DictMethod>(this, DictMethodKind::PopItem);
  if (mem == "setdefault")
    return std::make_unique<DictMethod>(this, DictMethodKind::SetDefault);
  if (mem == "update")
    return std::make_unique<DictMethod>(this, DictMethodKind::Update);
  if (mem == "values")
    return std::make_unique<DictMethod>(this, DictMethodKind::Values);

  return IObject::GetMember(mem);
}

std::string Dict::Str() const {
  std::string repr;
  std::vector<const map_t::value_type*> items;
  items.reserve(map.size());
  for (const auto& item : map)
    items.push_back(&item);

  std::ranges::sort(
      items, [](const map_t::value_type* lhs, const map_t::value_type* rhs) {
        return lhs->first.Desc() < rhs->first.Desc();
      });

  for (const auto* item : items) {
    const auto& [k, v] = *item;
    if (!repr.empty())
      repr += ',';
    repr += k.Str() + ':' + v.Str();
  }

  return '{' + repr + '}';
}
std::string Dict::Desc() const {
  return "<dict{" + std::to_string(map.size()) + "}>";
}

void Dict::MarkRoots(GCVisitor& visitor) {
  for (auto& [k, it] : map) {
    visitor.MarkSub(const_cast<Value&>(k));
    visitor.MarkSub(it);
  }
  for (auto& key : order)
    visitor.MarkSub(key);
}

void Dict::GetItem(VM& vm, Fiber& f) {
  Value idx = std::move(f.op_stack.back());
  f.op_stack.pop_back();

  auto it = map.find(idx);
  if (it == map.cend()) {
    vm.Error(f, "dictionary has no key: " + idx.Str());
    return;
  }

  f.op_stack.back() = it->second;
}

void Dict::SetItem(VM& vm, Fiber& f) {
  Value idx = std::move(f.op_stack.end()[-2]),
        val = std::move(f.op_stack.end()[-1]);
  f.op_stack.resize(f.op_stack.size() - 3);  // (dict, idx, val)

  try {
    SyncOrder(*this);
    AssignOrdered(map, order, std::move(idx), std::move(val));
  } catch (const RuntimeError& e) {
    vm.Error(f, e.message());
  }
}

}  // namespace serilang
