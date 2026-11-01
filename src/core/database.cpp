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

#include "core/database.hpp"

#include "core/compression.hpp"
#include "core/gameexe.hpp"
#include "utilities/byte_reader.hpp"
#include "utilities/file.hpp"
#include "utilities/mapped_file.hpp"

#include <boost/locale/encoding_utf.hpp>

#include <algorithm>
#include <bit>
#include <charconv>
#include <format>
#include <fstream>
#include <stdexcept>

namespace {

uint32_t Word(std::string_view bytes, size_t offset) {
  if (offset > bytes.size() || bytes.size() - offset < 4)
    throw std::runtime_error("truncated 32-bit word");
  return ByteReader(bytes.substr(offset, 4)).PopAs<uint32_t>(4);
}

void XorWord(std::string& bytes, size_t offset, uint32_t key) {
  for (size_t i = 0; i < 4; ++i)
    bytes[offset + i] ^= static_cast<char>(key >> (8 * i));
}

void CheckExtent(size_t offset, size_t count, size_t width, size_t size) {
  if (offset > size || count > (size - offset) / width)
    throw std::runtime_error("table extent exceeds database data_size");
}

std::string ReadString(std::string_view pool,
                       size_t offset,
                       bool unicode,
                       Encoding encoding,
                       const Codepage& converter) {
  if (offset >= pool.size())
    throw std::runtime_error("string offset outside string pool");
  if (!unicode) {
    const auto end = pool.find('\0', offset);
    if (end == std::string_view::npos)
      throw std::runtime_error("unterminated multibyte string");
    std::u16string text;
    for (size_t pos = offset; pos < end; ++pos) {
      uint16_t ch = static_cast<unsigned char>(pool[pos]);
      const bool lead =
          encoding == Encoding::cp932
              ? ((ch >= 0x81 && ch <= 0x9f) || (ch >= 0xe0 && ch <= 0xfc))
              : (encoding != Encoding::cp1252 && ch >= 0x81);
      if (lead) {
        if (end - pos < 2)
          throw std::runtime_error("truncated multibyte character");
        const auto trail = static_cast<unsigned char>(pool[++pos]);
        const bool valid_trail =
            encoding == Encoding::cp949
                ? trail >= 0x41 && trail <= 0xfe
                : trail >= 0x40 && trail <= 0xfe && trail != 0x7f;
        if (!valid_trail || ch == 0xff)
          throw std::runtime_error("invalid multibyte character");
        ch = (ch << 8) | trail;
      }
      if (encoding == Encoding::cp949 && ch == 0x80)
        throw std::runtime_error("invalid multibyte character");
      const auto decoded = converter.Convert(ch);
      if (decoded == 0)
        throw std::runtime_error("invalid multibyte character");
      text.push_back(static_cast<char16_t>(decoded));
    }
    return boost::locale::conv::utf_to_utf<char>(
        text.data(), text.data() + text.size(), boost::locale::conv::stop);
  }

  std::u16string text;
  for (size_t pos = offset; pos < pool.size() && pool.size() - pos >= 2;
       pos += 2) {
    const auto ch =
        static_cast<char16_t>(static_cast<unsigned char>(pool[pos]) |
                              (static_cast<unsigned char>(pool[pos + 1]) << 8));
    if (ch == 0)
      return boost::locale::conv::utf_to_utf<char>(
          text.data(), text.data() + text.size(), boost::locale::conv::stop);
    text.push_back(ch);
  }
  throw std::runtime_error("unterminated UTF-16LE string");
}

std::string FoldAscii(std::string_view value) {
  std::string result(value);
  for (char& ch : result)
    if (ch >= 'A' && ch <= 'Z')
      ch += 'a' - 'A';
  return result;
}

// Select.ini stores ordered directory/name pairs separated by a tab.
std::vector<std::string> AppendDirectories(const std::filesystem::path& root) {
  const auto path = CorrectPath(root, "Select.ini");
  if (path.empty())
    return {""};
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("cannot read Select.ini");
  std::vector<std::string> dirs;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    const auto tab = line.find('\t');
    if (tab == std::string::npos ||
        line.find('\t', tab + 1) != std::string::npos)
      throw std::runtime_error("Select.ini expects directory/name pairs");
    auto dir = line.substr(0, tab);
    std::replace(dir.begin(), dir.end(), '\\', '/');
    dirs.push_back(std::move(dir));
  }
  if (!input.eof() || dirs.empty())
    throw std::runtime_error("empty or unreadable Select.ini");
  return dirs;
}

}  // namespace

Database Database::Decode(std::string_view file, Encoding multibyte_encoding) {
  const bool unicode = Word(file, 0) != 0;
  std::string compressed(file.substr(4));
  for (size_t pos = 0; pos + 4 <= compressed.size(); pos += 4)
    XorWord(compressed, pos, 0x89F4622D);
  if (compressed.size() < 8 ||
      Word(compressed, 4) > uint64_t(compressed.size() - 8) * 9)
    throw std::runtime_error("invalid LZSS expanded size");
  std::string bytes = Decompress_lzss(compressed);
  constexpr bool mask[5][5] = {{1, 0, 0, 1, 1},
                               {0, 0, 1, 1, 0},
                               {1, 1, 1, 0, 1},
                               {0, 0, 1, 0, 0},
                               {0, 0, 0, 1, 1}};
  for (size_t pos = 0; pos + 4 <= bytes.size(); pos += 4) {
    const auto word = pos / 4;
    XorWord(bytes, pos,
            mask[(word / 16) % 5][(word % 16) % 5] ? 0x7190C70E : 0x499BF135);
  }
  const auto size = Word(bytes, 0);
  if (size < 28 || size > bytes.size())
    throw std::runtime_error("invalid database data_size");
  const auto rows = Word(bytes, 4);
  const auto columns = Word(bytes, 8);
  const auto row_offset = Word(bytes, 12);
  const auto column_offset = Word(bytes, 16);
  const auto data_offset = Word(bytes, 20);
  const auto string_offset = Word(bytes, 24);
  CheckExtent(row_offset, rows, 4, size);
  CheckExtent(column_offset, columns, 8, size);
  // Divide before multiplying dimensions, including on 32-bit hosts.
  if (data_offset > size ||
      (columns != 0 && rows > (size - data_offset) / 4 / columns))
    throw std::runtime_error("cell table exceeds database data_size");
  CheckExtent(string_offset, 0, 1, size);

  if (multibyte_encoding != Encoding::cp932 &&
      multibyte_encoding != Encoding::cp936 &&
      multibyte_encoding != Encoding::cp949 &&
      multibyte_encoding != Encoding::cp1252)
    throw std::runtime_error("unsupported database multibyte encoding");
  const auto converter = Codepage::Create(multibyte_encoding);
  Database result;
  for (size_t row = 0; row < rows; ++row) {
    const auto id = std::bit_cast<int32_t>(Word(bytes, row_offset + row * 4));
    result.item_index_.try_emplace(id, row);
    result.items_.push_back(id);
  }
  for (size_t col = 0; col < columns; ++col) {
    const auto id =
        std::bit_cast<int32_t>(Word(bytes, column_offset + col * 8));
    result.column_index_.try_emplace(id, col);
    result.columns_.push_back({id, Word(bytes, column_offset + col * 8 + 4)});
  }
  const auto pool =
      std::string_view(bytes).substr(string_offset, size - string_offset);
  for (size_t row = 0; row < rows; ++row) {
    for (size_t col = 0; col < columns; ++col) {
      const auto value = Word(bytes, data_offset + (row * columns + col) * 4);
      Cell cell;
      if (result.columns_[col].type == 'S')
        cell.text =
            ReadString(pool, value, unicode, multibyte_encoding, *converter);
      else
        cell.number = std::bit_cast<int32_t>(value);
      result.cells_.push_back(std::move(cell));
    }
  }
  return result;
}

std::optional<size_t> Database::ColumnIndex(int id, uint32_t type) const {
  const auto it = column_index_.find(id);
  if (it == column_index_.end() || columns_[it->second].type != type)
    return std::nullopt;
  return it->second;
}

int Database::GetNum(int item, int column) const {
  const auto row = item_index_.find(item);
  const auto col = ColumnIndex(column, 'V');
  if (row == item_index_.end() || !col)
    return 0;
  return cells_[row->second * columns_.size() + *col].number;
}

std::string Database::GetStr(int item, int column) const {
  const auto row = item_index_.find(item);
  const auto col = ColumnIndex(column, 'S');
  if (row == item_index_.end() || !col)
    return "";
  return cells_[row->second * columns_.size() + *col].text;
}

int Database::CheckItem(int item) const { return item_index_.contains(item); }

int Database::CheckColumn(int column) const {
  const auto it = column_index_.find(column);
  if (it == column_index_.end())
    return 0;
  const auto type = columns_[it->second].type;
  return type == 'V' ? 1 : type == 'S' ? 2 : 0;
}

int Database::FindNum(int column, int value) const {
  const auto col = ColumnIndex(column, 'V');
  if (col)
    for (size_t row = 0; row < items_.size(); ++row)
      if (cells_[row * columns_.size() + *col].number == value)
        return items_[row];
  return -1;
}

int Database::FindString(int column,
                         std::string_view value,
                         bool fold_ascii) const {
  const auto col = ColumnIndex(column, 'S');
  const std::string needle = fold_ascii ? FoldAscii(value) : std::string(value);
  if (col)
    for (size_t row = 0; row < items_.size(); ++row) {
      const auto& text = cells_[row * columns_.size() + *col].text;
      if ((fold_ascii ? FoldAscii(text) : text) == needle)
        return items_[row];
    }
  return -1;
}

int Database::FindStr(int column, const std::string& value) const {
  return FindString(column, value, true);
}

int Database::FindStrReal(int column, const std::string& value) const {
  return FindString(column, value, false);
}

std::shared_ptr<const DatabaseList> DatabaseList::Load(
    Gameexe& config,
    const std::filesystem::path& game_root,
    std::optional<std::string> current_append) {
  const auto count_setting = config("DATABASE.CNT");
  const int count = count_setting.Exists() ? count_setting.ToInt() : 0;
  if (count < 0 || count > 256)
    throw std::runtime_error("DATABASE.CNT must be between 0 and 256");
  std::vector<std::string> names(count);
  for (auto entry : config.Filter("DATABASE.")) {
    const auto parts = entry.GetKeyParts();
    if (parts.size() == 2 && parts[1] == "CNT")
      continue;
    int index = -1;
    if (parts.size() != 2)
      throw std::runtime_error("invalid database configuration: " +
                               entry.key());
    const auto& text = parts[1];
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), index);
    if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size() ||
        index < 0 || index >= count)
      throw std::runtime_error("database slot outside configured count: " +
                               entry.key());
    names[index] = entry.ToStr();
  }

  auto result = std::make_shared<DatabaseList>();
  const auto dirs = AppendDirectories(game_root);
  const auto begin = current_append
                         ? std::find(dirs.begin(), dirs.end(), *current_append)
                         : dirs.begin();
  if (begin == dirs.end())
    throw std::runtime_error(
        "current database append directory is not in Select.ini");
  for (int index = 0; index < count; ++index) {
    if (names[index].empty()) {
      result->databases_.push_back(std::make_shared<const Database>());
      continue;
    }
    try {
      std::filesystem::path path;
      for (auto dir = begin; dir != dirs.end(); ++dir) {
        path = CorrectPath(game_root, (std::filesystem::path(*dir) / "dat" /
                                       (names[index] + ".dbs"))
                                          .generic_string());
        if (!path.empty())
          break;
      }
      if (path.empty())
        throw std::runtime_error(
            "named .dbs asset not found in dat directories");
      MappedFile file(path);
      result->databases_.push_back(
          std::make_shared<const Database>(Database::Decode(file.Read())));
    } catch (const std::exception& error) {
      throw std::runtime_error(std::format("database[{}] '{}': {}", index,
                                           names[index], error.what()));
    }
  }
  return result;
}

std::shared_ptr<const Database> DatabaseList::At(int index) const {
  if (index < 0 || static_cast<size_t>(index) >= databases_.size())
    throw std::out_of_range(
        std::format("database index {} outside [0, {})", index, size()));
  return databases_[index];
}
