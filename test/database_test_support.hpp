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

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cstdlib>

inline void PutWord(std::string& bytes, size_t pos, uint32_t value) {
  for (size_t i = 0; i < 4; ++i)
    bytes.at(pos + i) = static_cast<char>(value >> (8 * i));
}

inline void AddWord(std::string& bytes, uint32_t value) {
  const size_t pos = bytes.size();
  bytes.resize(pos + 4);
  PutWord(bytes, pos, value);
}

inline std::string Utf16Bytes(std::u16string_view text) {
  std::string bytes;
  for (char16_t ch : text) {
    bytes += static_cast<char>(ch);
    bytes += static_cast<char>(ch >> 8);
  }
  return bytes;
}

// Explicit map rows provide a fixture oracle independent of tiled indexing.
inline std::string EncodeDatabase(std::string bytes,
                                  uint32_t selector = 1,
                                  bool backrefs = false) {
  constexpr std::string_view mask =
      "1001110011100111"
      "0011000110001100"
      "1110111101111011"
      "0010000100001000"
      "0001100011000110";
  for (size_t word = 0; word < bytes.size() / 4; ++word) {
    const uint32_t key =
        mask[word % mask.size()] == '1' ? 0x7190C70E : 0x499BF135;
    for (size_t i = 0; i < 4; ++i)
      bytes[word * 4 + i] ^= static_cast<char>(key >> (8 * i));
  }
  std::string compressed(8, '\0');
  size_t pos = 0;
  while (pos < bytes.size()) {
    const size_t flag = compressed.size();
    compressed += '\0';
    for (int bit = 0; bit < 8 && pos < bytes.size(); ++bit) {
      size_t distance = 0;
      size_t length = 0;
      if (backrefs)
        for (size_t d = 1; d <= std::min(pos, size_t{4095}); ++d) {
          size_t n = 0;
          while (n < 17 && pos + n < bytes.size() &&
                 bytes[pos + n] == bytes[pos + n - d])
            ++n;
          if (n > length) {
            distance = d;
            length = n;
          }
        }
      if (length >= 3) {
        const auto ref = (distance << 4) | (length - 2);
        compressed += static_cast<char>(ref);
        compressed += static_cast<char>(ref >> 8);
        pos += length;
      } else {
        compressed[flag] |= static_cast<char>(1 << bit);
        compressed += bytes[pos++];
      }
    }
  }
  PutWord(compressed, 0, compressed.size());
  PutWord(compressed, 4, bytes.size());
  for (size_t pos = 0; pos + 4 <= compressed.size(); pos += 4)
    for (size_t i = 0; i < 4; ++i)
      compressed[pos + i] ^= static_cast<char>(0x89F4622Du >> (8 * i));
  std::string file;
  AddWord(file, selector);
  return file + compressed;
}

struct DatabaseFixture {
  std::vector<int> rows = {42, 7, 42, 99};
  std::vector<std::pair<int, int>> columns = {{8, 'S'},
                                              {3, 'V'},
                                              {8, 'V'},
                                              {9, 'X'}};
  std::vector<uint32_t> cells = {2, uint32_t(-17), 800, 1, 14, 5,   801, 1, 2,
                                 5, 802,           1,   0, 5,  803, 1};
  // Byte offsets 0, 2, and 14 address empty, mixed-case, and Japanese strings.
  std::string strings = std::string(2, '\0') + Utf16Bytes(u"AbCde") +
                        std::string(2, '\0') + Utf16Bytes(u"日本Ä") +
                        std::string(2, '\0');

  std::string Plain() const {
    std::string bytes(28, '\0');
    PutWord(bytes, 4, rows.size());
    PutWord(bytes, 8, columns.size());
    PutWord(bytes, 12, bytes.size());
    for (int row : rows)
      AddWord(bytes, row);
    PutWord(bytes, 16, bytes.size());
    for (auto [id, type] : columns) {
      AddWord(bytes, id);
      AddWord(bytes, type);
    }
    PutWord(bytes, 20, bytes.size());
    for (auto cell : cells)
      AddWord(bytes, cell);
    PutWord(bytes, 24, bytes.size());
    bytes += strings;
    PutWord(bytes, 0, bytes.size());
    // Real assets pad the expanded buffer beyond data_size.
    bytes.resize((bytes.size() + 63) / 64 * 64, '\0');
    return bytes;
  }
  std::string File(uint32_t selector = 1, bool backrefs = false) const {
    return EncodeDatabase(Plain(), selector, backrefs);
  }
};

class DatabaseFiles {
 public:
  DatabaseFiles() {
    char pattern[] = "/tmp/rlvm-database-XXXXXX";
    const char* result = mkdtemp(pattern);
    if (!result)
      throw std::runtime_error("cannot create database test directory");
    root = result;
  }
  ~DatabaseFiles() { std::filesystem::remove_all(root); }
  void Write(const std::string& relative, const std::string& bytes) const {
    const auto path = root / relative;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), bytes.size());
    if (!stream)
      throw std::runtime_error("cannot write synthetic database fixture");
  }
  std::filesystem::path root;
};
