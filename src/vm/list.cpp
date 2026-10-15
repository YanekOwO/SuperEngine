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

#include "vm/list.hpp"

#include "utilities/string_utilities.hpp"
#include "vm/dict.hpp"
#include "vm/exception.hpp"
#include "vm/gc.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <ranges>
#include <string>

namespace serilang {

namespace {

enum class ListMethodKind {
  Append,
  Clear,
  Copy,
  Count,
  Extend,
  Index,
  Insert,
  Len,
  Pop,
  Remove,
  Reverse,
  Sort
};

bool RelaxedEqual(const Value& lhs, const Value& rhs) {
  try {
    return lhs == rhs;
  } catch (const RuntimeError&) {
    return false;
  }
}

void AppendStringChars(VM& vm, const String& str, std::vector<Value>& out) {
  out.reserve(out.size() + str.str_.size());
  for (char ch : str.str_)
    out.emplace_back(vm.gc_->Allocate<String>(std::string(1, ch)));
}

void AppendDictKeys(const Dict& dict, std::vector<Value>& out) {
  out.reserve(out.size() + dict.map.size());
  for (const Value& key : dict.order)
    out.push_back(key);

  if (dict.order.size() == dict.map.size())
    return;

  for (const auto& [key, value] : dict.map) {
    const bool present = std::ranges::any_of(out, [&](const Value& existing) {
      return RelaxedEqual(existing, key);
    });
    if (!present)
      out.push_back(key);
  }
}

bool ExpandIterable(VM& vm,
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

class ListMethod final : public IObject {
 public:
  static constexpr inline ObjType objtype = ObjType::BoundMethod;

  ListMethod(List* self, ListMethodKind kind) : self_(self), kind_(kind) {}

  constexpr ObjType Type() const noexcept final { return objtype; }
  constexpr size_t Size() const noexcept final { return sizeof(*this); }

  void MarkRoots(GCVisitor& visitor) override { visitor.MarkSub(self_); }

  std::string Str() const override { return Desc(); }
  std::string Desc() const override {
    return std::format("<bound method list.{}>", MethodName());
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
      fail(std::format("list.{} does not accept keyword arguments",
                       MethodName()));
      return false;
    };

    auto require_nargs = [&](uint8_t expected) -> bool {
      if (nargs == expected)
        return true;
      fail(std::format("list.{} expects {} argument{}, got {}", MethodName(),
                       expected, expected == 1 ? "" : "s", nargs));
      return false;
    };

    auto read_int = [&](size_t offset,
                        std::string_view what) -> std::optional<int> {
      Value& v = f.op_stack[base + 1 + offset];
      if (auto* iv = v.Get_if<int>())
        return *iv;
      fail(std::format("{} must be integer, but got: {}", what, v.Desc()));
      return std::nullopt;
    };

    auto pop_index = [&](int raw) -> std::optional<size_t> {
      const int len = static_cast<int>(self_->items.size());
      int index = raw < 0 ? len + raw : raw;
      if (index < 0 || index >= len) {
        fail(std::format("list index '{}' out of range", raw));
        return std::nullopt;
      }
      return static_cast<size_t>(index);
    };

    auto insert_index = [&](int raw) -> size_t {
      const int len = static_cast<int>(self_->items.size());
      int index = raw < 0 ? len + raw : raw;
      if (index < 0)
        index = 0;
      if (index > len)
        index = len;
      return static_cast<size_t>(index);
    };

    switch (kind_) {
      case ListMethodKind::Append: {
        if (!require_no_kwargs() || !require_nargs(1))
          return;
        self_->items.push_back(f.op_stack[base + 1]);
        finish();
        return;
      }

      case ListMethodKind::Clear: {
        if (!require_no_kwargs() || !require_nargs(0))
          return;
        self_->items.clear();
        finish();
        return;
      }

      case ListMethodKind::Copy: {
        if (!require_no_kwargs() || !require_nargs(0))
          return;
        finish(Value(vm.gc_->Allocate<List>(self_->items)));
        return;
      }

      case ListMethodKind::Count: {
        if (!require_no_kwargs() || !require_nargs(1))
          return;
        const Value& needle = f.op_stack[base + 1];
        int count = 0;
        for (const Value& item : self_->items)
          count += RelaxedEqual(item, needle) ? 1 : 0;
        finish(Value(count));
        return;
      }

      case ListMethodKind::Extend: {
        if (!require_no_kwargs() || !require_nargs(1))
          return;
        std::vector<Value> extra;
        if (!ExpandIterable(vm, f, f.op_stack[base + 1], extra, "list.extend"))
          return;
        self_->items.insert(self_->items.end(), extra.begin(), extra.end());
        finish();
        return;
      }

      case ListMethodKind::Index: {
        if (!require_no_kwargs() || !require_nargs(1))
          return;
        const Value& needle = f.op_stack[base + 1];
        for (size_t i = 0; i < self_->items.size(); ++i) {
          if (RelaxedEqual(self_->items[i], needle)) {
            finish(Value(static_cast<int>(i)));
            return;
          }
        }
        fail("list.index: value not in list");
        return;
      }

      case ListMethodKind::Insert: {
        if (!require_no_kwargs() || !require_nargs(2))
          return;
        auto raw_index = read_int(0, "insert index");
        if (!raw_index)
          return;
        const size_t index = insert_index(*raw_index);
        self_->items.insert(self_->items.begin() + index, f.op_stack[base + 2]);
        finish();
        return;
      }

      case ListMethodKind::Len: {
        if (!require_no_kwargs() || !require_nargs(0))
          return;
        finish(Value(static_cast<int>(self_->items.size())));
        return;
      }

      case ListMethodKind::Pop: {
        if (!require_no_kwargs())
          return;
        if (nargs != 0 && nargs != 1) {
          fail(std::format("list.pop expects 0 or 1 arguments, got {}", nargs));
          return;
        }
        if (self_->items.empty()) {
          fail("pop from empty list");
          return;
        }

        size_t index = self_->items.size() - 1;
        if (nargs == 1) {
          auto raw_index = read_int(0, "pop index");
          if (!raw_index)
            return;
          auto parsed = pop_index(*raw_index);
          if (!parsed)
            return;
          index = *parsed;
        }

        Value result = self_->items[index];
        self_->items.erase(self_->items.begin() + index);
        finish(std::move(result));
        return;
      }

      case ListMethodKind::Remove: {
        if (!require_no_kwargs() || !require_nargs(1))
          return;
        const Value& needle = f.op_stack[base + 1];
        for (auto it = self_->items.begin(); it != self_->items.end(); ++it) {
          if (RelaxedEqual(*it, needle)) {
            self_->items.erase(it);
            finish();
            return;
          }
        }
        fail("list.remove: value not in list");
        return;
      }

      case ListMethodKind::Reverse: {
        if (!require_no_kwargs() || !require_nargs(0))
          return;
        std::ranges::reverse(self_->items);
        finish();
        return;
      }

      case ListMethodKind::Sort: {
        if (!require_no_kwargs() || !require_nargs(0))
          return;
        try {
          std::ranges::sort(
              self_->items, [](const Value& lhs, const Value& rhs) {
                return (lhs <=> rhs) == std::strong_ordering::less;
              });
        } catch (const RuntimeError& e) {
          fail(std::format("list.sort: {}", e.message()));
          return;
        }
        finish();
        return;
      }
    }
  }

 private:
  std::string_view MethodName() const {
    switch (kind_) {
      case ListMethodKind::Append:
        return "append";
      case ListMethodKind::Clear:
        return "clear";
      case ListMethodKind::Copy:
        return "copy";
      case ListMethodKind::Count:
        return "count";
      case ListMethodKind::Extend:
        return "extend";
      case ListMethodKind::Index:
        return "index";
      case ListMethodKind::Insert:
        return "insert";
      case ListMethodKind::Len:
        return "len";
      case ListMethodKind::Pop:
        return "pop";
      case ListMethodKind::Remove:
        return "remove";
      case ListMethodKind::Reverse:
        return "reverse";
      case ListMethodKind::Sort:
        return "sort";
    }
    return "";
  }

  List* self_;
  ListMethodKind kind_;
};

}  // namespace

TempValue List::GetMember(std::string_view mem) {
  if (mem == "append")
    return std::make_unique<ListMethod>(this, ListMethodKind::Append);
  if (mem == "clear")
    return std::make_unique<ListMethod>(this, ListMethodKind::Clear);
  if (mem == "copy")
    return std::make_unique<ListMethod>(this, ListMethodKind::Copy);
  if (mem == "count")
    return std::make_unique<ListMethod>(this, ListMethodKind::Count);
  if (mem == "extend")
    return std::make_unique<ListMethod>(this, ListMethodKind::Extend);
  if (mem == "index")
    return std::make_unique<ListMethod>(this, ListMethodKind::Index);
  if (mem == "insert")
    return std::make_unique<ListMethod>(this, ListMethodKind::Insert);
  if (mem == "len")
    return std::make_unique<ListMethod>(this, ListMethodKind::Len);
  if (mem == "pop")
    return std::make_unique<ListMethod>(this, ListMethodKind::Pop);
  if (mem == "remove")
    return std::make_unique<ListMethod>(this, ListMethodKind::Remove);
  if (mem == "reverse")
    return std::make_unique<ListMethod>(this, ListMethodKind::Reverse);
  if (mem == "sort")
    return std::make_unique<ListMethod>(this, ListMethodKind::Sort);

  return IObject::GetMember(mem);
}

std::string List::Str() const {
  return '[' +
         Join(",", std::views::all(items) |
                       std::views::transform(
                           [](const Value& v) { return v.Str(); })) +
         ']';
}
std::string List::Desc() const {
  return "<list[" + std::to_string(items.size()) + "]>";
}

void List::MarkRoots(GCVisitor& visitor) {
  for (auto& it : items)
    visitor.MarkSub(it);
}

void List::GetItem(VM& vm, Fiber& f) {
  Value idx = std::move(f.op_stack.back());
  f.op_stack.pop_back();

  auto* index_ptr = idx.Get_if<int>();
  if (!index_ptr) {
    vm.Error(f, "list index must be integer, but got: " + idx.Desc());
    return;
  }

  int index = *index_ptr < 0 ? items.size() + *index_ptr : *index_ptr;
  if (index < 0 || index >= items.size()) {
    vm.Error(f, "list index '" + idx.Str() + "' out of range");
    return;
  }

  f.op_stack.back() = items[index];
}

void List::SetItem(VM& vm, Fiber& f) {
  Value idx = std::move(f.op_stack.end()[-2]),
        val = std::move(f.op_stack.end()[-1]);
  f.op_stack.resize(f.op_stack.size() - 3);  // (list, idx, val)

  auto* index_ptr = idx.Get_if<int>();
  if (!index_ptr) {
    vm.Error(f, "list index must be integer, but got: " + idx.Desc());
    return;
  }

  int index = *index_ptr < 0 ? items.size() + *index_ptr : *index_ptr;
  if (index < 0 || index >= items.size()) {
    vm.Error(f, "list index '" + idx.Str() + "' out of range");
    return;
  }

  items[index] = std::move(val);
}

}  // namespace serilang
