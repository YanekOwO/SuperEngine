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

#include "libsiglus/xorkey.hpp"

#include <bitset>
#include <span>
#include <string>

namespace libsiglus {
namespace crack {

using Domain = std::bitset<256>;

struct Sample {
  std::string data;
  std::size_t max_output;
};

struct Probe {
  bool valid = true;
  std::size_t next_byte = xorkey_t{}.size();
  Domain values;
};

class Cracker {
  static constexpr std::size_t kMaxAttempts = 1'000'000;
  static constexpr auto kMaxSceneSize = std::numeric_limits<uint32_t>::max();

 public:
  using PrefixCheck = bool (*)(std::string_view, std::size_t);
  using ResultCheck = bool (*)(std::string_view);

  struct Format {
    const std::array<uint8_t, 256>& xor_key;
    std::size_t min_output;
    std::size_t prefix_size;
    PrefixCheck check_prefix;
    ResultCheck check_result = nullptr;
    std::size_t alignment = 1;
    bool prefer_zeroes = false;
  };

  Cracker(std::span<const std::string_view> blocks, Format format);

  xorkey_t Crack();

 private:
  Probe ProbePrefix(const Sample& sample) const;
  Probe ConstrainSize(const Sample& sample, std::size_t size) const;
  bool CopyReference(std::string& output,
                     std::size_t distance,
                     unsigned int length,
                     std::size_t max_output,
                     std::size_t limit) const;
  bool Validate() const;
  bool GuessText();
  bool Search();

  Format format_;
  std::vector<Sample> samples_;
  std::array<Domain, 16> domains_;
  std::array<std::array<uint8_t, 256>, 16> order_;
  xorkey_t key_{};
  std::bitset<16> assigned_;
  std::size_t attempts_ = 0;
  std::size_t search_limit_ = kMaxAttempts;
};

}  // namespace crack

xorkey_t CrackScene(std::span<const std::string_view> scenes);

xorkey_t CrackGameexe(std::string_view compressed);

}  // namespace libsiglus
