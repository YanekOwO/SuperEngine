// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
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

#include "core/tone_curve.hpp"

#include <sstream>
#include <string>

#include "core/gameexe.hpp"
#include "utilities/byte_reader.hpp"
#include "utilities/file.hpp"
#include "utilities/mapped_file.hpp"

namespace fs = std::filesystem;

ToneCurve::ToneCurve() {}

ToneCurve::ToneCurve(std::string_view data) : tcc_info_() {
  constexpr auto header_size = 4008;
  if (data.size() < header_size) {
    std::ostringstream oss;
    oss << "Invalid TCC file: Data too short to contain header.";
    oss << " Expected at least " << header_size << " bytes,";
    oss << " but got " << data.size() << '.';
    throw std::runtime_error(oss.str());
  }

  ByteReader reader(data.substr(0, header_size));
  int32_t magic = reader.PopAs<int32_t>(4);
  if (magic != 1000) {
    throw std::runtime_error(
        "Invalid TCC file: Expected magic number 1000, but got " +
        std::to_string(magic));
  }

  int32_t effect_count_ = reader.PopAs<int32_t>(4);
  tcc_info_.reserve(effect_count_);

  for (int i = 0; i < 1000; ++i) {
    uint32_t offset = reader.PopAs<uint32_t>(4);
    if (offset == 0)
      continue;

    if (offset >= data.size()) {
      throw std::runtime_error("Invalid offset: " + std::to_string(offset) +
                               " exceeds data size of " +
                               std::to_string(data.size()));
    }
    tcc_info_.emplace_back(ParseEffect(data.substr(offset)));
  }

  if (tcc_info_.size() != effect_count_) {
    throw std::runtime_error("Effect count mismatch: Expected " +
                             std::to_string(effect_count_) + ", but parsed " +
                             std::to_string(tcc_info_.size()));
  }
}

ToneCurveRGBMap ToneCurve::ParseEffect(std::string_view data) {
  constexpr auto header_size = 64;
  if (data.size() < header_size) {
    throw std::runtime_error(
        "Invalid effect data: Data too short to contain header. Expected at "
        "least " +
        std::to_string(header_size) + " bytes, but got " +
        std::to_string(data.size()));
  }

  ByteReader header(data.substr(0, header_size));
  int32_t type = header.PopAs<int32_t>(4);
  uint32_t data_size = header.PopAs<uint32_t>(4);
  if (data.size() < header_size + data_size) {
    throw std::runtime_error("Invalid effect data: Data size " +
                             std::to_string(data.size()) +
                             " is less than expected size " +
                             std::to_string(header_size + data_size));
  }

  switch (type) {
    case 0:
      if (data_size < 768) {
        throw std::runtime_error(
            "Invalid data size for tone curve type 0: Expected at least 768 "
            "bytes, but got " +
            std::to_string(data_size));
      }
      break;
    case 1:
      if (data_size < 772) {
        throw std::runtime_error(
            "Invalid data size for tone curve type 1: Expected at least 772 "
            "bytes, but got " +
            std::to_string(data_size));
      }
      break;
    default:
      throw std::runtime_error("Invalid tone curve type: " +
                               std::to_string(type));
  }

  ByteReader reader(data.substr(header_size, header_size + data_size));

  ToneCurveRGBMap rgb_map;
  for (int i = 0; i < 256; ++i)
    rgb_map[0][i] = reader.PopAs<uint8_t>(1);
  for (int i = 0; i < 256; ++i)
    rgb_map[1][i] = reader.PopAs<uint8_t>(1);
  for (int i = 0; i < 256; ++i)
    rgb_map[2][i] = reader.PopAs<uint8_t>(1);

  if (type == 1) {
    [[maybe_unused]] int saturation = reader.PopAs<int>(4);
  }
  return rgb_map;
}

ToneCurve::~ToneCurve() = default;

int ToneCurve::GetEffectCount() const { return tcc_info_.size(); }

ToneCurveRGBMap ToneCurve::GetEffect(int index) const {
  if (index >= GetEffectCount() || index < 0) {
    std::ostringstream oss;
    oss << "Requested tone curve index " << index
        << " exceeds the amount of effects (" << GetEffectCount() << ')'
        << " in the tone curve file.";
    throw std::out_of_range(oss.str());
  }

  return tcc_info_[index];
}

ToneCurve CreateToneCurve(Gameexe& gameexe) {
  GameexeInterpretObject filename_key = gameexe("TONECURVE_FILENAME");
  if (!filename_key.Exists()) {
    // It is perfectly valid not to have a tone curve key. All operations in
    // this class become noops.
    return ToneCurve();
  }

  std::string tonecurve = filename_key.Str().value_or("");
  if (tonecurve == "") {
    // It is perfectly valid not to have a tone curve. All operations in this
    // class become noops.
    return ToneCurve();
  }

  fs::path basename = gameexe("__GAMEPATH").ToStr();
  fs::path filename = CorrectPath(basename, "dat/" + tonecurve);

  MappedFile mfile(filename);
  return ToneCurve(mfile.Read());
}
