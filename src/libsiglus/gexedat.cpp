// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
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

#include "libsiglus/gexedat.hpp"

#include "core/compression.hpp"
#include "core/gameexe.hpp"
#include "encodings/utf16.hpp"
#include "libsiglus/cracker.hpp"
#include "libsiglus/xorkey.hpp"
#include "log/domain_logger.hpp"
#include "utilities/byte_reader.hpp"
#include "utilities/mapped_file.hpp"

#include <boost/algorithm/string/trim.hpp>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace libsiglus {

static Gameexe CreateGexeImpl(std::string_view sv, const xorkey_t& key) {
  std::string data(sv.substr(8));
  [[maybe_unused]] int version = -1;
  int encryption = 0;

  {
    ByteReader reader(sv.substr(0, 8));
    version = reader.PopAs<int32_t>(4);
    encryption = reader.PopAs<int32_t>(4);
  }

  if (encryption) {
    for (size_t i = 0; i < data.size(); ++i)
      data[i] ^= key[i & 0xf];
  }

  for (size_t i = 0; i < data.size(); ++i)
    data[i] ^= gexe_key[i & 0xff];

  data = Decompress_lzss(data);
  if (data.size() >= 2 && static_cast<uint8_t>(data[0]) == 0xff &&
      static_cast<uint8_t>(data[1]) == 0xfe)
    data.erase(0, 2);

  Gameexe gexe;
  std::stringstream ss(utf16le::Decode(data));
  std::string line;
  while (std::getline(ss, line, '\n')) {
    boost::trim(line);
    if (line.empty())
      continue;

    gexe.parseLine(line);
  }

  return gexe;
}

Gameexe CreateGexe(std::string_view sv) {
  if (sv.size() < 16)
    throw std::runtime_error("CreateGexe: truncated file header.");
  for (auto it = ExekeyRegistry::cbegin(); it != ExekeyRegistry::cend(); ++it) {
    try {
      const auto& [name, key] = *it;
      static DomainLogger logger("Gameexe");
      logger(Severity::Info) << "Decrypt using exekey: " << name;
      return CreateGexeImpl(sv, key);
    } catch (...) {
    }
  }

  // fallback
  ByteReader header(sv);
  header.Proceed(4);
  if (header.PopAs<uint32_t>(4) == 0)
    return CreateGexeImpl(sv, xorkey_t{});
  const auto key = CrackGameexe(sv.substr(8));
  static DomainLogger logger("Gameexe");
  logger(Severity::Info) << "Cracked exekey: " << ToHex(key);
  return CreateGexeImpl(sv, key);
}

Gameexe CreateGexe(const std::filesystem::path& pth) {
  MappedFile f(pth);
  return CreateGexe(f.Read());
}

}  // namespace libsiglus
