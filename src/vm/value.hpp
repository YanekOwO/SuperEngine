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

#pragma once

#include "machine/op.hpp"
#include "vm/exception.hpp"
#include "vm/objtype.hpp"
#include "vm/value_fwd.hpp"

#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <variant>
#include <vector>

namespace serilang {

struct GCVisitor;
class VM;

// -----------------------------------------------------------------------
// Value system
class Value {
 public:
  using value_t = std::variant<std::monostate,  // nil
                               bool,
                               int,
                               double,
                               IObject*  // object
                               >;

  Value(value_t x = std::monostate());
  template <typename T>
    requires std::constructible_from<value_t, T>
  Value(T&& x) : Value(value_t(std::forward<T>(x))) {}

  std::string Str() const;
  std::string Desc() const;
  bool IsTruthy() const;

  ObjType Type() const;

  void Call(VM& vm,
            Fiber& f,
            uint8_t nargs,
            uint8_t nkwargs,
            CallResultMode mode = CallResultMode::Keep);

  void GetItem(VM& vm, Fiber& f);
  void SetItem(VM& vm, Fiber& f);

  TempValue GetMember(std::string_view mem);
  void SetMember(std::string_view mem, Value value);

  void GetMember(VM& vm, Fiber& f, std::string_view mem);
  void SetMember(VM& vm, Fiber& f, std::string_view mem);

  template <typename T>
  auto Get_if() -> std::add_pointer_t<T> {
    using TPTR = std::add_pointer_t<T>;

    if constexpr (std::same_as<T, IObject>) {
      auto objptr = std::get_if<IObject*>(&val_);
      if (!objptr)
        return static_cast<TPTR>(nullptr);
      return *objptr;
    } else if constexpr (std::derived_from<T, IObject>) {
      auto baseptr = std::get_if<IObject*>(&val_);
      if (!baseptr)
        return static_cast<TPTR>(nullptr);
      auto casted = dynamic_cast<TPTR>(*baseptr);
      return casted;
    } else {
      return std::get_if<T>(&val_);
    }
  }

  template <typename T>
  auto Get_if() const -> std::add_pointer_t<std::add_const_t<T>> {
    using TPTR = std::add_pointer_t<std::add_const_t<T>>;

    if constexpr (std::same_as<T, IObject>) {
      auto objptr = std::get_if<IObject*>(&val_);
      if (!objptr)
        return static_cast<TPTR>(nullptr);
      return *objptr;
    } else if constexpr (std::derived_from<T, IObject>) {
      auto baseptr = std::get_if<IObject*>(&val_);
      if (!baseptr)
        return static_cast<TPTR>(nullptr);
      auto casted = dynamic_cast<TPTR>(*baseptr);
      return casted;
    } else {
      return std::get_if<T>(&val_);
    }
  }

  template <typename T>
  auto Get() -> T {
    if constexpr (std::is_pointer_v<T> &&
                  std::derived_from<std::remove_pointer_t<T>, IObject>) {
      auto ptr = Get_if<std::remove_pointer_t<T>>();
      if (!ptr)
        throw std::bad_variant_access();
      return ptr;
    } else {
      auto ptr = Get_if<T>();
      if (!ptr)
        throw std::bad_variant_access();
      return *ptr;
    }
  }

  template <typename T>
  auto Get() const -> T {
    if constexpr (std::is_pointer_v<T> &&
                  std::derived_from<std::remove_pointer_t<T>, IObject>) {
      auto ptr = Get_if<std::remove_pointer_t<T>>();
      if (!ptr)
        throw std::bad_variant_access();
      return const_cast<T>(ptr);
    } else {
      auto ptr = Get_if<T>();
      if (!ptr)
        throw std::bad_variant_access();
      return *ptr;
    }
  }

  template <class Visitor>
  decltype(auto) Apply(Visitor&& vis) {
    return std::visit(std::forward<Visitor>(vis), val_);
  }
  template <class Visitor>
  decltype(auto) Apply(Visitor&& vis) const {
    return std::visit(std::forward<Visitor>(vis), val_);
  }
  template <class R, class Visitor>
  R Apply(Visitor&& vis) {
    return std::visit<R>(std::forward<Visitor>(vis), val_);
  }
  template <class R, class Visitor>
  R Apply(Visitor&& vis) const {
    return std::visit<R>(std::forward<Visitor>(vis), val_);
  }

  // for testing
  operator std::string() const;
  bool operator==(const Value& rhs) const;
  auto operator<=>(const Value& rhs) const -> std::strong_ordering;
  bool operator==(std::monostate) const;
  bool operator==(int rhs) const;
  bool operator==(double rhs) const;
  bool operator==(bool rhs) const;
  bool operator==(const std::string& rhs) const;
  bool operator==(char const* s) const;
  std::size_t Hash() const;

  inline friend std::ostream& operator<<(std::ostream& os, const Value& v) {
    return os << v.Desc();
  }

 private:
  value_t val_;
};

inline const auto nil = Value(std::monostate());

std::size_t HashFor(ObjType type, std::size_t hash);

}  // namespace serilang

namespace std {

template <>
struct hash<serilang::Value> {
  std::size_t operator()(const serilang::Value& value) const {
    return value.Hash();
  }
};

}  // namespace std
