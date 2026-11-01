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

#include "encodings/codepage.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class Gameexe;

// Immutable game data. Identifiers refer to headers, not physical offsets.
class Database {
 public:
  static Database Decode(std::string_view file,
                         Encoding multibyte_encoding = Encoding::cp932);

  int GetNum(int item, int column) const;
  std::string GetStr(int item, int column) const;
  int CheckItem(int item) const;
  int CheckColumn(int column) const;
  int FindNum(int column, int value) const;
  int FindStr(int column, const std::string& value) const;
  int FindStrReal(int column, const std::string& value) const;

 private:
  struct Column {
    int id;
    uint32_t type;
  };
  struct Cell {
    int32_t number = 0;
    std::string text;
  };
  std::optional<size_t> ColumnIndex(int id, uint32_t type) const;
  int FindString(int column, std::string_view value, bool fold_ascii) const;

  std::vector<int> items_;
  std::vector<Column> columns_;
  std::vector<Cell> cells_;
  std::unordered_map<int, size_t> item_index_;
  std::unordered_map<int, size_t> column_index_;
};

class DatabaseList {
 public:
  static std::shared_ptr<const DatabaseList> Load(
      Gameexe& config,
      const std::filesystem::path& game_root,
      std::optional<std::string> current_append = std::nullopt);

  int size() const { return static_cast<int>(databases_.size()); }
  std::shared_ptr<const Database> At(int index) const;

 private:
  std::vector<std::shared_ptr<const Database>> databases_;
};
