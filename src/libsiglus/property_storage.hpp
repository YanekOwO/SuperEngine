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
#include <boost/serialization/split_member.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/vector.hpp>

#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace libsiglus {
class Archive;

// Property values and list backing belong to the game, independently of any VM.
class PropertyStorage {
 public:
  using Integers = std::vector<int>;
  using Strings = std::vector<std::string>;
  // Archive tags follow this order; append new alternatives to preserve tags.
  using Value = std::variant<std::monostate,
                             bool,
                             int,
                             double,
                             std::string,
                             Integers,
                             Strings>;

  struct Entry {
    Value value;
    int initial_size = 0;

   private:
    friend class boost::serialization::access;
    void save(auto& ar, unsigned int) const {
      const unsigned int tag = value.index();
      ar & initial_size & tag;
      std::visit(
          [&](const auto& item) {
            if constexpr (!std::is_same_v<std::decay_t<decltype(item)>,
                                          std::monostate>)
              ar & item;
          },
          value);
    }

    template <std::size_t I = 0>
    void LoadValue(auto& ar, unsigned int tag) {
      if constexpr (I < std::variant_size_v<Value>) {
        if (tag == I) {
          auto& item = value.emplace<I>();
          if constexpr (I != 0)
            ar & item;
        } else {
          LoadValue<I + 1>(ar, tag);
        }
      } else {
        throw std::runtime_error("invalid property value tag");
      }
    }

    void load(auto& ar, unsigned int) {
      unsigned int tag;
      ar & initial_size & tag;
      if (initial_size < 0)
        throw std::runtime_error("negative property initial size");
      LoadValue(ar, tag);
    }
    BOOST_SERIALIZATION_SPLIT_MEMBER()
  };
  using Table = std::vector<Entry>;

  void Initialize(const Archive& archive);
  Entry& Get(int scene, int index);
  const Entry& Get(int scene, int index) const;

 private:
  std::unordered_map<int, Table> tables_;

  friend class boost::serialization::access;
  template <class Archive>
  void save(Archive& ar, unsigned int) const {
    ar & tables_;
  }
  template <class Archive>
  void load(Archive& ar, unsigned int) {
    decltype(tables_) tables;
    ar & tables;
    tables_.swap(tables);
  }
  BOOST_SERIALIZATION_SPLIT_MEMBER()
};

}  // namespace libsiglus
