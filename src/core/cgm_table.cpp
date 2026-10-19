// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2008 Elliot Glaysher
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

#include "core/cgm_table.hpp"

#include "core/compression.hpp"
#include "core/gameexe.hpp"
#include "utilities/byte_reader.hpp"
#include "utilities/file.hpp"
#include "utilities/mapped_file.hpp"

namespace fs = std::filesystem;

static unsigned char cgm_xor_key[256] = {  // tpc
    0x8b, 0xe5, 0x5d, 0xc3, 0xa1, 0xe0, 0x30, 0x44, 0x00, 0x85, 0xc0, 0x74,
    0x09, 0x5f, 0x5e, 0x33, 0xc0, 0x5b, 0x8b, 0xe5, 0x5d, 0xc3, 0x8b, 0x45,
    0x0c, 0x85, 0xc0, 0x75, 0x14, 0x8b, 0x55, 0xec, 0x83, 0xc2, 0x20, 0x52,
    0x6a, 0x00, 0xe8, 0xf5, 0x28, 0x01, 0x00, 0x83, 0xc4, 0x08, 0x89, 0x45,
    0x0c, 0x8b, 0x45, 0xe4, 0x6a, 0x00, 0x6a, 0x00, 0x50, 0x53, 0xff, 0x15,
    0x34, 0xb1, 0x43, 0x00, 0x8b, 0x45, 0x10, 0x85, 0xc0, 0x74, 0x05, 0x8b,
    0x4d, 0xec, 0x89, 0x08, 0x8a, 0x45, 0xf0, 0x84, 0xc0, 0x75, 0x78, 0xa1,
    0xe0, 0x30, 0x44, 0x00, 0x8b, 0x7d, 0xe8, 0x8b, 0x75, 0x0c, 0x85, 0xc0,
    0x75, 0x44, 0x8b, 0x1d, 0xd0, 0xb0, 0x43, 0x00, 0x85, 0xff, 0x76, 0x37,
    0x81, 0xff, 0x00, 0x00, 0x04, 0x00, 0x6a, 0x00, 0x76, 0x43, 0x8b, 0x45,
    0xf8, 0x8d, 0x55, 0xfc, 0x52, 0x68, 0x00, 0x00, 0x04, 0x00, 0x56, 0x50,
    0xff, 0x15, 0x2c, 0xb1, 0x43, 0x00, 0x6a, 0x05, 0xff, 0xd3, 0xa1, 0xe0,
    0x30, 0x44, 0x00, 0x81, 0xef, 0x00, 0x00, 0x04, 0x00, 0x81, 0xc6, 0x00,
    0x00, 0x04, 0x00, 0x85, 0xc0, 0x74, 0xc5, 0x8b, 0x5d, 0xf8, 0x53, 0xe8,
    0xf4, 0xfb, 0xff, 0xff, 0x8b, 0x45, 0x0c, 0x83, 0xc4, 0x04, 0x5f, 0x5e,
    0x5b, 0x8b, 0xe5, 0x5d, 0xc3, 0x8b, 0x55, 0xf8, 0x8d, 0x4d, 0xfc, 0x51,
    0x57, 0x56, 0x52, 0xff, 0x15, 0x2c, 0xb1, 0x43, 0x00, 0xeb, 0xd8, 0x8b,
    0x45, 0xe8, 0x83, 0xc0, 0x20, 0x50, 0x6a, 0x00, 0xe8, 0x47, 0x28, 0x01,
    0x00, 0x8b, 0x7d, 0xe8, 0x89, 0x45, 0xf4, 0x8b, 0xf0, 0xa1, 0xe0, 0x30,
    0x44, 0x00, 0x83, 0xc4, 0x08, 0x85, 0xc0, 0x75, 0x56, 0x8b, 0x1d, 0xd0,
    0xb0, 0x43, 0x00, 0x85, 0xff, 0x76, 0x49, 0x81, 0xff, 0x00, 0x00, 0x04,
    0x00, 0x6a, 0x00, 0x76};

CGMTable::CGMTable() {}

CGMTable::~CGMTable() {}

CGMTable::CGMTable(std::string_view data) {
  if (data.size() < 32)
    throw std::invalid_argument(
        "data too small to contain a valid AVG cg table header");

  ByteReader reader(data.substr(0, 32));
  std::string_view magic = reader.PopAs<std::string_view>(16);
  int32_t data_count = reader.PopAs<int32_t>(4);
  [[maybe_unused]] int32_t auto_flag = reader.PopAs<int32_t>(4);
  [[maybe_unused]] int32_t unknown1 = reader.PopAs<int32_t>(4);
  [[maybe_unused]] int32_t unknown2 = reader.PopAs<int32_t>(4);

  if (magic.substr(0, 7) != "CGTABLE") {
    throw std::logic_error("Incorrect magic number in CGM table header.");
  }

  int version = (magic.substr(0, 8) == "CGTABLE2") ? 2 : 1;

  data = data.substr(32);
  std::string compressed;
  compressed.reserve(data.size());
  std::transform(
      data.cbegin(), data.cend(), std::back_inserter(compressed),
      [&](auto x) { return x ^ cgm_xor_key[compressed.size() & 0xff]; });
  std::string decompressed = Decompress_lzss(compressed);
  compressed.clear();

  reader = ByteReader(decompressed);
  auto Trim = [](std::string str) {
    while (!str.empty() && str.back() == '\0') {
      str.pop_back();
    }
    return str;
  };

  for (int i = 0; i < data_count; ++i) {
    std::string name = Trim(reader.PopAs<std::string>(32));
    std::transform(name.begin(), name.end(), name.begin(), ::toupper);
    int flag_no = reader.PopAs<int32_t>(4);

    if (version >= 2) {
      [[maybe_unused]] int32_t code[5];
      for (int i = 0; i < 5; ++i)
        code[i] = reader.PopAs<int32_t>(4);
      [[maybe_unused]] int32_t code_count = reader.PopAs<int32_t>(4);
    }

    cgm_info_.emplace(std::move(name), flag_no);
  }
}

int CGMTable::GetTotal() const { return cgm_info_.size(); }

int CGMTable::GetViewed() const { return cgm_data_.size(); }

int CGMTable::GetPercent() const {
  // Prevent divide by zero
  auto total = GetTotal();
  if (total == 0)
    return 0;

  auto viewed = GetViewed();
  auto percentage = viewed * 100 / total;
  if (percentage == 0 && viewed)
    percentage = 1;
  return percentage;
}

int CGMTable::GetFlag(const std::string& filename) const {
  const auto it = cgm_info_.find(filename);
  if (it == cgm_info_.end())
    return -1;

  return it->second;
}

int CGMTable::GetStatus(const std::string& filename) const {
  int flag = GetFlag(filename);
  if (flag == -1)
    return -1;

  if (cgm_data_.find(flag) != cgm_data_.end())
    return 1;

  return 0;
}

void CGMTable::SetViewed(const std::string& filename) {
  int flag = GetFlag(filename);

  if (flag != -1) {
    cgm_data_.insert(flag);
  }
}

CGMTable CreateCGMTable(Gameexe& gameexe) {
  GameexeInterpretObject filename_key = gameexe("CGTABLE_FILENAME");
  if (!filename_key.Exists()) {
    // It is perfectly valid not to have a CG table key. All operations in this
    // class become noops.
    return CGMTable();
  }

  std::string cgtable = filename_key.Str().value_or("");
  if (cgtable == "") {
    // It is perfectly valid not to have a CG table. All operations in this
    // class become noops.
    return CGMTable();
  }

  fs::path basepath = gameexe("__GAMEPATH").ToStr();
  fs::path filepath = CorrectPath(basepath, "dat/" + cgtable);

  MappedFile mfile(filepath);
  return CGMTable(mfile.Read());
}
