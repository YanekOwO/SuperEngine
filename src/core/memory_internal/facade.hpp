// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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
//
// -----------------------------------------------------------------------

#pragma once

#include "core/memory_internal/proxy.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace facade_detail {

inline std::size_t CheckIndex(int idx) {
  if (idx < 0)
    throw std::out_of_range("negative memory bank index: " +
                            std::to_string(idx));
  return static_cast<std::size_t>(idx);
}

inline std::size_t CheckSize(int size) {
  if (size < 0)
    throw std::invalid_argument("negative memory bank size: " +
                                std::to_string(size));
  return static_cast<std::size_t>(size);
}

inline int CheckedIntSize(std::size_t size) {
  if (size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::overflow_error("memory bank size exceeds script integer range");
  return static_cast<int>(size);
}

inline std::size_t CheckedEnd(std::size_t begin,
                              std::size_t count,
                              std::string_view where) {
  if (count > std::numeric_limits<std::size_t>::max() - begin)
    throw std::out_of_range(std::format("{} index overflow", where));
  return begin + count;
}

inline std::pair<std::size_t, std::size_t>
CheckFillRange(int begin, int end, std::size_t size, std::string_view where) {
  const std::size_t begin_index = CheckIndex(begin);
  const std::size_t end_index = CheckIndex(end);
  if (begin_index > end_index)
    throw std::invalid_argument(
        std::format("{} has invalid fill range", where));
  if (end_index > size) {
    throw std::out_of_range(std::format(
        "{} fill end {} out of range for size {}", where, end_index, size));
  }
  return {begin_index, end_index};
}

}  // namespace facade_detail

template <class... Ts>
class IntListFacade {
 public:
  using storage_type = std::vector<int>;
  using getter_t = std::function<storage_type&(Ts...)>;

  explicit IntListFacade(getter_t getter,
                         std::optional<int> size = std::nullopt)
      : getter_(std::move(getter)),
        default_size_(facade_detail::CheckSize(size.value_or(0))) {
    if (!getter_)
      throw std::invalid_argument("IntListFacade: getter is empty.");
  }

  int get(Ts... context, int idx) {
    auto& data = storage(std::forward<Ts>(context)...);
    return IntListProxy(data).Get(facade_detail::CheckIndex(idx));
  }

  void set(Ts... context, int idx, int value) {
    auto& data = storage(std::forward<Ts>(context)...);
    IntListProxy(data).Set(facade_detail::CheckIndex(idx), value);
  }

  void Set(Ts... context, int idx, storage_type values) {
    const std::size_t begin = facade_detail::CheckIndex(idx);
    if (values.empty())
      return;

    auto& data = storage(std::forward<Ts>(context)...);
    const std::size_t end =
        facade_detail::CheckedEnd(begin, values.size(), "integer list Set");
    if (end > data.size()) {
      throw std::out_of_range(std::format(
          "integer list Set range [{}, {}) out of range for size {}", begin,
          end, data.size()));
    }

    std::move(values.begin(), values.end(), data.begin() + begin);
  }

  void resize(Ts... context, int size) {
    storage(std::forward<Ts>(context)...)
        .resize(facade_detail::CheckSize(size));
  }

  int size(Ts... context) const {
    return facade_detail::CheckedIntSize(
        storage(std::forward<Ts>(context)...).size());
  }

  void fill(Ts... context, int begin, int end, int value) {
    auto& data = storage(std::forward<Ts>(context)...);
    const auto [begin_index, end_index] =
        facade_detail::CheckFillRange(begin, end, data.size(), "integer list");
    std::fill(data.begin() + begin_index, data.begin() + end_index, value);
  }

  void init(Ts... context) {
    storage(std::forward<Ts>(context)...).assign(default_size_, 0);
  }

  int b1(Ts... context, int idx) {
    return read_bitfield(std::forward<Ts>(context)..., idx, 1);
  }
  void write_b1(Ts... context, int idx, int value) {
    write_bitfield(std::forward<Ts>(context)..., idx, value, 1);
  }
  int b2(Ts... context, int idx) {
    return read_bitfield(std::forward<Ts>(context)..., idx, 2);
  }
  void write_b2(Ts... context, int idx, int value) {
    write_bitfield(std::forward<Ts>(context)..., idx, value, 2);
  }
  int b4(Ts... context, int idx) {
    return read_bitfield(std::forward<Ts>(context)..., idx, 4);
  }
  void write_b4(Ts... context, int idx, int value) {
    write_bitfield(std::forward<Ts>(context)..., idx, value, 4);
  }
  int b8(Ts... context, int idx) {
    return read_bitfield(std::forward<Ts>(context)..., idx, 8);
  }
  void write_b8(Ts... context, int idx, int value) {
    write_bitfield(std::forward<Ts>(context)..., idx, value, 8);
  }
  int b16(Ts... context, int idx) {
    return read_bitfield(std::forward<Ts>(context)..., idx, 16);
  }
  void write_b16(Ts... context, int idx, int value) {
    write_bitfield(std::forward<Ts>(context)..., idx, value, 16);
  }

 private:
  storage_type& storage(Ts... context) const {
    auto& result = getter_(std::forward<Ts>(context)...);
    if (!initialized_) {
      if (default_size_ > 0)
        result.resize(default_size_);
      initialized_ = true;
    }
    return result;
  }

  int read_bitfield(Ts... context, int idx, std::uint8_t bits) {
    auto& data = storage(std::forward<Ts>(context)...);
    return IntListProxy(data).Get(facade_detail::CheckIndex(idx), bits);
  }

  void write_bitfield(Ts... context, int idx, int value, std::uint8_t bits) {
    auto& data = storage(std::forward<Ts>(context)...);
    IntListProxy(data).Set(facade_detail::CheckIndex(idx), value, bits);
  }

  getter_t getter_;
  std::size_t default_size_;
  mutable bool initialized_ = false;
};

template <class... Ts>
class StrListFacade {
 public:
  using storage_type = std::vector<std::string>;
  using getter_t = std::function<storage_type&(Ts...)>;

  explicit StrListFacade(getter_t getter,
                         std::optional<int> size = std::nullopt)
      : getter_(std::move(getter)),
        default_size_(facade_detail::CheckSize(size.value_or(0))) {
    if (!getter_)
      throw std::invalid_argument("StrListFacade: getter is empty.");
  }

  std::string get(Ts... context, int idx) {
    auto& data = storage(std::forward<Ts>(context)...);
    return StrListProxy(data).Get(facade_detail::CheckIndex(idx));
  }

  void set(Ts... context, int idx, std::string value) {
    auto& data = storage(std::forward<Ts>(context)...);
    StrListProxy(data).Set(facade_detail::CheckIndex(idx), value);
  }

  void resize(Ts... context, int size) {
    storage(std::forward<Ts>(context)...)
        .resize(facade_detail::CheckSize(size));
  }

  int size(Ts... context) const {
    return facade_detail::CheckedIntSize(
        storage(std::forward<Ts>(context)...).size());
  }

  void fill(Ts... context, int begin, int end, std::string value) {
    auto& data = storage(std::forward<Ts>(context)...);
    const auto [begin_index, end_index] =
        facade_detail::CheckFillRange(begin, end, data.size(), "string list");
    std::fill(data.begin() + begin_index, data.begin() + end_index, value);
  }

  void init(Ts... context) {
    storage(std::forward<Ts>(context)...).assign(default_size_, "");
  }

 private:
  storage_type& storage(Ts... context) const {
    auto& result = getter_(std::forward<Ts>(context)...);
    if (!initialized_) {
      if (default_size_ > 0)
        result.resize(default_size_);
      initialized_ = true;
    }
    return result;
  }

  getter_t getter_;
  std::size_t default_size_;
  mutable bool initialized_ = false;
};
