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

#include "vm/value.hpp"

#include "utilities/string_utilities.hpp"
#include "vm/iobject.hpp"
#include "vm/object.hpp"
#include "vm/primops.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <bit>
#include <cmath>
#include <compare>
#include <cstdint>
#include <format>

namespace serilang {

namespace {
constexpr std::uint64_t kCanonicalNaNBits = 0x7ff8000000000000ULL;
std::uint64_t CanonicalizeDoubleBits(double value) {
  std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
  if ((bits & 0x7fffffffffffffffULL) == 0)
    return 0;
  if (std::isnan(value))
    return kCanonicalNaNBits;
  return bits;
}

std::strong_ordering CompareDouble(double lhs, double rhs) {
  if (lhs < rhs)
    return std::strong_ordering::less;
  if (lhs > rhs)
    return std::strong_ordering::greater;

  const auto lhs_bits = CanonicalizeDoubleBits(lhs);
  const auto rhs_bits = CanonicalizeDoubleBits(rhs);
  if (lhs_bits < rhs_bits)
    return std::strong_ordering::less;
  if (lhs_bits > rhs_bits)
    return std::strong_ordering::greater;
  return std::strong_ordering::equal;
}

bool EqualDouble(double lhs, double rhs) {
  return CompareDouble(lhs, rhs) == std::strong_ordering::equal;
}

std::size_t HashCombine(std::size_t seed, std::size_t value) {
  constexpr std::size_t kMul = 0x9e3779b97f4a7c15ULL;
  seed ^= value + kMul + (seed << 6) + (seed >> 2);
  return seed;
}

inline ObjType TypeOf(const Value& value) {
  return value.Apply<ObjType>([](const auto& x) {
    using T = std::decay_t<decltype(x)>;
    if constexpr (std::same_as<T, std::monostate>)
      return ObjType::Nil;
    else if constexpr (std::same_as<T, bool>)
      return ObjType::Bool;
    else if constexpr (std::same_as<T, int>)
      return ObjType::Int;
    else if constexpr (std::same_as<T, double>)
      return ObjType::Double;
    else if constexpr (std::same_as<T, IObject*>)
      return x ? x->Type() : ObjType::Other;
  });
}

const String* GetStringObject(IObject* obj) {
  if (!obj || obj->Type() != ObjType::String)
    return nullptr;
  return static_cast<const String*>(obj);
}

[[noreturn]] void ThrowTypeMismatch(const Value& lhs, const Value& rhs) {
  throw ValueError("cannot compare values of different underlying types: " +
                   lhs.Desc() + " and " + rhs.Desc());
}

std::strong_ordering CompareObjects(IObject* lhs, IObject* rhs) {
  if (lhs == rhs)
    return std::strong_ordering::equal;

  if (const String* lhs_str = GetStringObject(lhs)) {
    const auto rhs_str = static_cast<const String*>(rhs);
    if (lhs_str->str_ < rhs_str->str_)
      return std::strong_ordering::less;
    if (lhs_str->str_ > rhs_str->str_)
      return std::strong_ordering::greater;
    return std::strong_ordering::equal;
  }

  return std::compare_three_way{}(lhs, rhs);
}

bool EqualObjects(IObject* lhs, IObject* rhs) {
  if (lhs == rhs)
    return true;

  if (const String* lhs_str = GetStringObject(lhs)) {
    const auto rhs_str = static_cast<const String*>(rhs);
    return lhs_str->str_ == rhs_str->str_;
  }

  return false;
}

}  // namespace

// -----------------------------------------------------------------------
// class Value

Value::Value(value_t x) : val_(std::move(x)) {}

std::string Value::Str() const {
  return std::visit(
      [](const auto& x) -> std::string {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::same_as<T, std::monostate>)
          return "nil";
        else if constexpr (std::same_as<T, bool>)
          return x ? "true" : "false";
        else if constexpr (std::same_as<T, int>)
          return std::to_string(x);
        else if constexpr (std::same_as<T, double>)
          return std::to_string(x);
        else if constexpr (std::same_as<T, std::string>)
          return x;
        else if constexpr (std::same_as<T, IObject*>)
          return x->Str();
      },
      val_);
}

std::string Value::Desc() const {
  return std::visit(
      [](const auto& x) -> std::string {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::same_as<T, std::monostate>)
          return "<nil>";
        else if constexpr (std::same_as<T, bool>)
          return x ? "<bool: true>" : "<bool: false>";
        else if constexpr (std::same_as<T, int>)
          return "<int: " + std::to_string(x) + '>';
        else if constexpr (std::same_as<T, double>)
          return "<double: " + std::to_string(x) + '>';
        else if constexpr (std::same_as<T, std::string>)
          return "<str: " + x + '>';
        else if constexpr (std::same_as<T, IObject*>)
          return x->Desc();
      },
      val_);
}

bool Value::IsTruthy() const {
  return std::visit(
      [](const auto& x) -> bool {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::same_as<T, std::monostate>)
          return false;
        else if constexpr (std::same_as<T, bool>)
          return x;
        else if constexpr (std::same_as<T, int>)
          return x != 0;
        else if constexpr (std::same_as<T, double>)
          return x != 0.0;
        else if constexpr (std::same_as<T, IObject*>) {
          if (!x)
            return false;
          if (auto b = x->Bool(); b.has_value())
            return *b;
          return true;
        }
      },
      val_);
}

ObjType Value::Type() const { return TypeOf(*this); }

void Value::Call(VM& vm,
                 Fiber& f,
                 uint8_t nargs,
                 uint8_t nkwargs,
                 CallResultMode mode) {
  std::visit(
      [&](auto& x) {
        using T = std::decay_t<decltype(x)>;

        if constexpr (false)
          ;

        else if constexpr (std::same_as<T, IObject*>)
          x->Call(vm, f, nargs, nkwargs, mode);

        else
          throw RuntimeError('\'' + Desc() + "' object is not callable.");
      },
      val_);
}

TempValue Value::GetMember(std::string_view mem) {
  return std::visit(
      [&](auto& x) -> TempValue {
        using T = std::decay_t<decltype(x)>;

        if constexpr (false)
          ;

        else if constexpr (std::same_as<T, IObject*>)
          return x->GetMember(mem);

        else
          throw RuntimeError('\'' + Desc() + "' object has no member '" +
                             std::string(mem) + '\'');
      },
      val_);
}

void Value::SetMember(std::string_view mem, Value value) {
  std::visit(
      [&](auto& x) {
        using T = std::decay_t<decltype(x)>;

        if constexpr (false)
          ;

        else if constexpr (std::same_as<T, IObject*>)
          x->SetMember(mem, std::move(value));

        else
          throw RuntimeError('\'' + Desc() +
                             "' object does not support member assignment.");
      },
      val_);
}

void Value::GetMember(VM& vm, Fiber& f, std::string_view mem) {
  if (auto* object = Get_if<IObject>()) {
    object->GetMember(vm, f, mem);
    return;
  }
  throw RuntimeError('\'' + Desc() + "' object has no member '" +
                     std::string(mem) + '\'');
}

void Value::SetMember(VM& vm, Fiber& f, std::string_view mem) {
  if (auto* object = Get_if<IObject>()) {
    object->SetMember(vm, f, mem);
    return;
  }
  throw RuntimeError('\'' + Desc() +
                     "' object does not support member assignment.");
}

void Value::GetItem(VM& vm, Fiber& f) {
  std::visit(
      [&](auto& x) {
        using T = std::decay_t<decltype(x)>;

        if constexpr (false)
          ;

        else if constexpr (std::same_as<T, IObject*>)
          x->GetItem(vm, f);

        else
          vm.Error(f, std::format("'{}' object has no item '{}'", Desc(),
                                  f.op_stack.back().Str()));
      },
      val_);
}

void Value::SetItem(VM& vm, Fiber& f) {
  std::visit(
      [&](auto& x) {
        using T = std::decay_t<decltype(x)>;

        if constexpr (false)
          ;

        else if constexpr (std::same_as<T, IObject*>)
          x->SetItem(vm, f);

        else
          vm.Error(f,
                   std::format("'{}' object does not support item assignment.",
                               Desc()));
      },
      val_);
}

Value::operator std::string() const { return this->Desc(); }

bool Value::operator==(const Value& rhs) const {
  if (TypeOf(*this) != TypeOf(rhs))
    ThrowTypeMismatch(*this, rhs);

  return std::visit(
      [](const auto& lhs, const auto& rhs) -> bool {
        using L = std::decay_t<decltype(lhs)>;
        using R = std::decay_t<decltype(rhs)>;

        if constexpr (!std::same_as<L, R>)
          throw std::logic_error("internal Value equality type mismatch");
        else if constexpr (std::same_as<L, std::monostate>)
          return true;
        else if constexpr (std::same_as<L, bool> || std::same_as<L, int>)
          return lhs == rhs;
        else if constexpr (std::same_as<L, double>)
          return EqualDouble(lhs, rhs);
        else if constexpr (std::same_as<L, IObject*>)
          return EqualObjects(lhs, rhs);
      },
      val_, rhs.val_);
}

auto Value::operator<=>(const Value& rhs) const -> std::strong_ordering {
  if (TypeOf(*this) != TypeOf(rhs))
    ThrowTypeMismatch(*this, rhs);

  return std::visit(
      [](const auto& lhs, const auto& rhs) -> std::strong_ordering {
        using L = std::decay_t<decltype(lhs)>;
        using R = std::decay_t<decltype(rhs)>;

        if constexpr (!std::same_as<L, R>)
          throw std::logic_error("internal Value ordering type mismatch");
        else if constexpr (std::same_as<L, std::monostate>)
          return std::strong_ordering::equal;
        else if constexpr (std::same_as<L, bool> || std::same_as<L, int>)
          return lhs <=> rhs;
        else if constexpr (std::same_as<L, double>)
          return CompareDouble(lhs, rhs);
        else if constexpr (std::same_as<L, IObject*>)
          return CompareObjects(lhs, rhs);
      },
      val_, rhs.val_);
}

bool Value::operator==(std::monostate) const {
  return std::holds_alternative<std::monostate>(val_);
}

bool Value::operator==(int rhs) const {
  auto ptr = std::get_if<int>(&val_);
  return ptr && *ptr == rhs;
}

bool Value::operator==(double rhs) const {
  auto ptr = std::get_if<double>(&val_);
  return ptr && *ptr == rhs;
}

bool Value::operator==(bool rhs) const {
  auto ptr = std::get_if<bool>(&val_);
  return ptr && *ptr == rhs;
}

bool Value::operator==(const std::string& rhs) const {
  auto ptr = Get_if<String>();
  return ptr && ptr->str_ == rhs;
}

bool Value::operator==(char const* s) const {
  auto ptr = Get_if<String>();
  return ptr && ptr->str_ == s;
}

std::size_t Value::Hash() const {
  std::size_t seed = static_cast<std::size_t>(TypeOf(*this));

  return std::visit(
      [seed](const auto& x) -> std::size_t {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::same_as<T, std::monostate>)
          return seed;
        else if constexpr (std::same_as<T, bool>)
          return HashCombine(seed, std::hash<bool>{}(x));
        else if constexpr (std::same_as<T, int>)
          return HashCombine(seed, std::hash<int>{}(x));
        else if constexpr (std::same_as<T, double>)
          return HashCombine(
              seed, std::hash<std::uint64_t>{}(CanonicalizeDoubleBits(x)));
        else if constexpr (std::same_as<T, IObject*>)
          return HashCombine(seed, x->Hash());
      },
      val_);
}

}  // namespace serilang
