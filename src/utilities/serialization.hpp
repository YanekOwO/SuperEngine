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

#pragma once

#include <boost/serialization/access.hpp>
#include <boost/serialization/array.hpp>
#include <boost/serialization/base_object.hpp>
#include <boost/serialization/deque.hpp>
#include <boost/serialization/list.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/shared_ptr.hpp>
#include <boost/serialization/split_member.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/unique_ptr.hpp>
#include <boost/serialization/utility.hpp>
#include <boost/serialization/variant.hpp>
#include <boost/serialization/vector.hpp>
#include <chrono>
#include <cstddef>
#include <optional>
#include <stdexcept>

namespace boost::serialization {
// Keep this direct: split_free's unqualified load lookup also considers
// shared_ptr helpers, whose generic pointer declaration conflicts with
// constrained std::optional conversion constructors on newer compilers.
template <class Archive, class T>
void serialize(Archive& archive, std::optional<T>& value, unsigned int) {
  bool present = value.has_value();
  archive & present;
  if constexpr (Archive::is_loading::value) {
    if (present)
      value.emplace();
    else
      value.reset();
  }
  if (present)
    archive&* value;
}

template <class Archive, class Rep, class Period>
void serialize(Archive& archive,
               std::chrono::duration<Rep, Period>& duration,
               unsigned int) {
  auto count = duration.count();
  archive & count;
  if constexpr (Archive::is_loading::value)
    duration = std::chrono::duration<Rep, Period>(count);
}
}  // namespace boost::serialization

// Check a collection count before allocating. The element constructors remain
// ordinary class constructors, and element pointers retain Boost tracking.
template <class Archive, class Sequence>
void SerializeBoundedSequence(Archive& archive,
                              Sequence& values,
                              std::size_t maximum = 1000000) {
  std::size_t count = values.size();
  archive & count;
  if (count > maximum)
    throw std::runtime_error("Saved collection exceeds resource limit");
  if constexpr (Archive::is_loading::value) {
    values.clear();
    values.resize(count);
  }
  for (auto& value : values)
    archive & value;
}
