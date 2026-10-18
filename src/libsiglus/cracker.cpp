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

#include "libsiglus/cracker.hpp"

#include "core/compression.hpp"
#include "libsiglus/scene.hpp"
#include "utilities/byte_reader.hpp"

#include <algorithm>
#include <bitset>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace libsiglus {
namespace crack {

Cracker::Cracker(std::span<const std::string_view> blocks, Format format)
    : format_(format) {
  for (auto& domain : domains_)
    domain.set();
  std::array<std::array<std::size_t, 256>, 16> frequencies{};
  for (std::string_view block : blocks) {
    if (block.size() < 8 || block.size() > kMaxSceneSize)
      throw std::runtime_error("Cracker: invalid compressed size.");
    Sample sample{
        std::string(block),
        std::min<uint64_t>(kMaxSceneSize, 17ull * (block.size() - 8) / 2)};
    for (std::size_t i = 0; i < block.size(); ++i) {
      sample.data[i] ^= format_.xor_key[i & 255];
      if (i >= 8)
        ++frequencies[i & 15][static_cast<uint8_t>(sample.data[i])];
    }
    for (std::size_t i = 0; i < 4; ++i) {
      Domain size_byte;
      size_byte.set(static_cast<uint8_t>(sample.data[i]) ^
                    ((block.size() >> (8 * i)) & 255));
      domains_[i] &= size_byte;
    }

    // The original-size header is also encrypted. Bound it using LZSS's
    // maximum expansion (17 output bytes per two-byte reference)
    for (std::size_t i = 4; i < 8; ++i) {
      const auto shift = 8 * (i - 4);
      const uint64_t period = uint64_t{1} << (shift + 8);
      Domain size_byte;
      for (unsigned int value = 0; value < 256; ++value) {
        if (i == 4 && value % format_.alignment != 0)
          continue;
        uint64_t begin = uint64_t{value} << shift;
        if (begin + (uint64_t{1} << shift) - 1 < format_.min_output)
          begin += period;
        if (begin <= sample.max_output)
          size_byte.set(value ^ static_cast<uint8_t>(sample.data[i]));
      }
      domains_[i] &= size_byte;
    }
    samples_.push_back(std::move(sample));
  }
  std::stable_sort(samples_.begin(), samples_.end(),
                   [](const Sample& a, const Sample& b) {
                     return a.data.size() < b.data.size();
                   });

  for (std::size_t i = 0; i < key_.size(); ++i) {
    std::iota(order_[i].begin(), order_[i].end(), 0);
    if (format_.prefer_zeroes)
      std::stable_sort(order_[i].begin(), order_[i].end(),
                       [&](uint8_t a, uint8_t b) {
                         return frequencies[i][a] > frequencies[i][b];
                       });
    if (domains_[i].count() == 1) {
      for (unsigned int value = 0; value < 256; ++value)
        if (domains_[i][value])
          key_[i] = value;
      assigned_.set(i);
    }
  }
}

xorkey_t Cracker::Crack() {
  if (samples_.empty() ||
      std::ranges::any_of(domains_, [](const Domain& d) { return d.none(); }) ||
      !(format_.prefer_zeroes ? GuessText() : Search())) {
    if (attempts_ >= kMaxAttempts)
      throw std::runtime_error("Cracker: search limit reached.");
    throw std::runtime_error("Cracker: no valid key found.");
  }
  return key_;
}

Probe Cracker::ProbePrefix(const Sample& sample) const {
  const auto known = [&](std::size_t pos) { return assigned_[pos & 15]; };
  const auto read = [&](std::size_t pos) {
    return static_cast<uint8_t>(sample.data[pos]) ^ key_[pos & 15];
  };

  auto max_output = sample.max_output;
  if (assigned_[4] && assigned_[5] && assigned_[6] && assigned_[7]) {
    uint32_t original_size = 0;
    for (std::size_t i = 4; i < 8; ++i)
      original_size |= static_cast<uint32_t>(read(i)) << (8 * (i - 4));
    if (original_size < format_.min_output || original_size > max_output ||
        original_size % format_.alignment != 0)
      return {false};
    max_output = original_size;
  }

  std::string output;
  const auto limit = std::min(format_.prefix_size, max_output);
  output.reserve(limit);
  std::size_t pos = 8;
  while (output.size() < limit) {
    if (pos >= sample.data.size())
      return ConstrainSize(sample, output.size());
    if (!known(pos)) {
      Domain values;
      for (unsigned int flags = 0; flags < 256; ++flags) {
        // Both supported formats start with two distinct bytes, which
        // must be literals before any reference can reproduce them.
        if (!output.empty() || (flags & 3) == 3)
          values.set(flags ^ static_cast<uint8_t>(sample.data[pos]));
      }
      return {true, pos & 15, values};
    }
    const auto flags = read(pos++);
    for (int bit = 0; bit < 8 && output.size() < limit; ++bit) {
      if (pos >= sample.data.size())
        return ConstrainSize(sample, output.size());
      if (flags & (1 << bit)) {
        if (!known(pos)) {
          Domain values;
          for (unsigned int value = 0; value < 256; ++value) {
            output += static_cast<char>(value);
            if (format_.check_prefix(output, max_output))
              values.set(value ^ static_cast<uint8_t>(sample.data[pos]));
            output.pop_back();
          }
          return {true, pos & 15, values};
        }
        output += static_cast<char>(read(pos++));
        if (!format_.check_prefix(output, max_output))
          return {false};
      } else {
        if (pos + 1 >= sample.data.size())
          return {false};
        if (!known(pos) || !known(pos + 1)) {
          const auto next = known(pos) ? pos + 1 : pos;
          Domain values;
          // Only enumerate references into the decoded prefix, checking
          // each copied byte against the expected output format.
          for (std::size_t distance = 1; distance <= output.size();
               ++distance) {
            for (unsigned int length = 2; length <= 17; ++length) {
              const auto token = (distance << 4) | (length - 2);
              if ((known(pos) && read(pos) != (token & 255)) ||
                  (known(pos + 1) && read(pos + 1) != (token >> 8)))
                continue;
              const auto size = output.size();
              const bool valid =
                  CopyReference(output, distance, length, max_output, limit);
              output.resize(size);
              if (valid)
                values.set(static_cast<uint8_t>(sample.data[next]) ^
                           ((next == pos ? token : token >> 8) & 255));
            }
          }
          return {true, next & 15, values};
        }
        const auto token = read(pos) | (read(pos + 1) << 8);
        pos += 2;
        const std::size_t distance = token >> 4;
        if (distance == 0 || distance > output.size() ||
            !CopyReference(output, distance, 2 + (token & 15), max_output,
                           limit))
          return {false};
      }
    }
  }

  return {};
}

Probe Cracker::ConstrainSize(const Sample& sample, std::size_t size) const {
  if (size < format_.min_output || size > sample.max_output ||
      size % format_.alignment != 0)
    return {false};
  for (std::size_t i = 4; i < 8; ++i) {
    const auto value =
        static_cast<uint8_t>(sample.data[i]) ^ ((size >> (8 * (i - 4))) & 255);
    if (!assigned_[i]) {
      Domain values;
      values.set(value);
      return {true, i, values};
    }
    if (key_[i] != value)
      return {false};
  }
  return {};
}

bool Cracker::CopyReference(std::string& output,
                            std::size_t distance,
                            unsigned int length,
                            std::size_t max_output,
                            std::size_t limit) const {
  for (unsigned int i = 0; i < length && output.size() < limit; ++i) {
    output += output[output.size() - distance];
    if (!format_.check_prefix(output, max_output))
      return false;
  }
  return true;
}

bool Cracker::Validate() const {
  for (const auto& sample : samples_) {
    std::string data = sample.data;
    for (std::size_t i = 0; i < data.size(); ++i)
      data[i] ^= key_[i & 15];
    ByteReader reader(data);
    reader.Proceed(4);
    const auto size = reader.PopAs<uint32_t>(4);
    if (size < format_.min_output || size > sample.max_output ||
        size % format_.alignment != 0)
      return false;
    try {
      const auto output = Decompress_lzss(std::string_view(data));
      if (format_.check_result && !format_.check_result(output))
        return false;
    } catch (...) {
      return false;
    }
  }
  return true;
}

bool Cracker::GuessText() {
  const auto fixed_key = key_;
  const auto fixed_bytes = assigned_;
  // Zero bytes are frequent in UTF-16 literals and LZSS reference tokens.
  // Try the complete statistical hypothesis before exploring partial keys.
  for (std::size_t i = 0; i < key_.size(); ++i) {
    if (!assigned_[i]) {
      for (uint8_t value : order_[i]) {
        if (domains_[i][value]) {
          key_[i] = value;
          break;
        }
      }
    }
  }
  assigned_.set();
  if (Validate())
    return true;
  key_ = fixed_key;
  assigned_ = fixed_bytes;

  // Small files may not have enough data for reliable byte frequencies.
  // Their opening UTF-16 code unit is constrained by config syntax. Try
  // those possibilities fairly, rather than exhausting one ambiguous prefix.
  const std::array<char16_t, 8> openings{'\n', '#',  0xfeff, '\r',
                                         ' ',  '\t', ';',    '/'};
  std::vector<unsigned int> flags{0xf3, 0xff};
  for (unsigned int value = 3; value < 256; value += 4)
    if (value != 0xf3 && value != 0xff)
      flags.push_back(value);
  for (std::size_t budget : {1000, 10000}) {
    for (auto flag : flags) {
      for (char16_t opening : openings) {
        if (attempts_ >= kMaxAttempts)
          return false;
        key_ = fixed_key;
        assigned_ = fixed_bytes;
        const std::array<uint8_t, 3> plain{static_cast<uint8_t>(flag),
                                           static_cast<uint8_t>(opening),
                                           static_cast<uint8_t>(opening >> 8)};
        bool valid = true;
        if (samples_.front().data.size() < 11)
          return false;
        for (std::size_t j = 0; j < plain.size(); ++j) {
          const auto i = j + 8;
          const auto value =
              static_cast<uint8_t>(samples_.front().data[i]) ^ plain[j];
          if (!domains_[i][value] || (assigned_[i] && key_[i] != value)) {
            valid = false;
            break;
          }
          key_[i] = value;
          assigned_.set(i);
        }
        search_limit_ = std::min(kMaxAttempts, attempts_ + budget);
        if (valid && Search())
          return true;
      }
    }
  }
  return false;
}

bool Cracker::Search() {
  // Bound the work for unsupported layouts and ambiguous/corrupt archives.
  if (attempts_ >= search_limit_)
    return false;
  ++attempts_;
  auto domains = domains_;
  std::bitset<16> needed;
  for (const auto& sample : samples_) {
    const auto probe = ProbePrefix(sample);
    if (!probe.valid)
      return false;
    if (probe.next_byte < key_.size()) {
      needed.set(probe.next_byte);
      domains[probe.next_byte] &= probe.values;
      if (domains[probe.next_byte].none())
        return false;
    }
  }
  if (assigned_.all())
    return Validate();
  // Branch only on bytes needed by a probe, so unconstrained bytes do not
  // expand the search prematurely.
  if (needed.none())
    needed = ~assigned_;
  std::size_t next = key_.size(), count = 257;
  for (std::size_t i = 0; i < key_.size(); ++i) {
    if (needed[i] && domains[i].count() < count) {
      next = i;
      count = domains[i].count();
    }
  }
  assigned_.set(next);
  for (uint8_t value : order_[next]) {
    if (attempts_ >= search_limit_)
      break;
    if (domains[next][value]) {
      key_[next] = value;
      if (Search())
        return true;
    }
  }
  assigned_.reset(next);
  return false;
}

}  // namespace crack

bool CheckHeader(std::string_view output, std::size_t max_output) {
  const std::size_t field_index = (output.size() - 1) / 4;
  const std::size_t bytes = (output.size() - 1) % 4 + 1;
  const auto field = [&](std::size_t index) -> uint64_t {
    return ByteReader(std::string_view(output).substr(4 * index, 4))
        .PopAs<uint32_t>(4);
  };
  uint64_t minimum = 0, maximum = max_output;
  switch (field_index) {
    case 0:  // header_size
    case 3:  // The string index table follows the header.
      minimum = maximum = sizeof(Scene_hdr);
      break;
    case 1:  // scene_offset
      minimum = sizeof(Scene_hdr);
      break;
    case 5:  // str_list_offset
      minimum = maximum = field(3) + 8 * field(4);
      break;
    case 6:  // str_list_size
      minimum = maximum = field(4);
      break;
    case 7:  // label_list_offset
      minimum = maximum = field(1) + field(2);
      break;
    case 9:  // zlabel_list_offset
      minimum = maximum = field(7) + 4 * field(8);
      break;
    case 11:  // cmdlabel_list_offset
      minimum = maximum = field(9) + 4 * field(10);
      break;
    case 13:  // prop_offset
      minimum = maximum = field(11) + 8 * field(12);
      break;
    case 15:  // prop_nameidx_offset
      minimum = maximum = field(13) + 8 * field(14);
      break;
    case 17:  // prop_name_offset
      minimum = maximum = field(15) + 8 * field(16);
      break;
    case 21:  // cmd_nameidx_offset
      minimum = maximum = field(19) + 4 * field(20);
      break;
    case 23:  // cmd_name_offset
      minimum = maximum = field(21) + 8 * field(22);
      break;
    case 27:  // call_name_offset
      minimum = maximum = field(25) + 8 * field(26);
      break;
    case 31:  // kidoku_offset
      minimum = maximum = field(29) + 4 * field(30);
      break;
  }
  if (minimum > max_output)
    return false;
  const uint64_t value =
      ByteReader(std::string_view(output).substr(4 * field_index))
          .PopAs<uint32_t>(bytes);
  const uint64_t mask = (uint64_t{1} << (8 * bytes)) - 1;
  return value <= maximum && value + ((maximum - value) & ~mask) >= minimum;
}

class ConfigText {
 public:
  bool Push(char16_t ch) {
    const bool first = characters_++ == 0;
    if (first && ch == 0xfeff)
      return true;
    if (lead_surrogate_) {
      lead_surrogate_ = false;
      return ch >= 0xdc00 && ch <= 0xdfff;
    }
    if (ch >= 0xd800 && ch <= 0xdbff) {
      lead_surrogate_ = IsValue();
      escaped_ = false;
      return lead_surrogate_;
    }
    if (ch == 0 || (ch >= 0xdc00 && ch <= 0xdfff))
      return false;
    if (ch == '\r' || ch == '\n') {
      if (phase_ == Phase::Quoted || phase_ == Phase::Key ||
          phase_ == Phase::AfterKey)
        return false;
      phase_ = Phase::LineStart;
      return true;
    }
    if (ch < 0x20 && ch != '\t')
      return false;
    switch (phase_) {
      case Phase::LineStart:
        if (ch == ' ' || ch == '\t')
          return true;
        if (ch == '#') {
          phase_ = Phase::Key;
          key_length_ = 0;
          return true;
        }
        if (ch == ';' || ch == '/') {
          phase_ = Phase::Comment;
          return true;
        }
        return false;
      case Phase::Key:
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == '[' ||
            ch == ']' || ch == '-') {
          ++key_length_;
          return true;
        }
        if (key_length_ == 0)
          return false;
        if (ch == ' ' || ch == '\t') {
          phase_ = Phase::AfterKey;
          return true;
        }
        [[fallthrough]];
      case Phase::AfterKey:
        if (ch == ' ' || ch == '\t')
          return true;
        if (ch != '=')
          return false;
        phase_ = Phase::Value;
        has_entry_ = true;
        return true;
      case Phase::Value:
        if (ch == '"')
          phase_ = Phase::Quoted;
        else if (ch == ';')
          phase_ = Phase::Comment;
        return true;
      case Phase::Quoted:
        if (escaped_)
          escaped_ = false;
        else if (ch == '\\')
          escaped_ = true;
        else if (ch == '"')
          phase_ = Phase::Value;
        return true;
      case Phase::Comment:
        return true;
    }
    return false;
  }

  bool AllowsLowByte(uint8_t byte) const {
    if (IsValue() || lead_surrogate_)
      return true;
    ConfigText copy = *this;
    if (copy.Push(byte))
      return true;
    copy = *this;
    return characters_ == 0 && byte == 0xff && copy.Push(0xfeff);
  }

  bool Complete() const {
    return has_entry_ && !lead_surrogate_ && phase_ != Phase::Quoted &&
           phase_ != Phase::Key && phase_ != Phase::AfterKey;
  }

 private:
  enum class Phase { LineStart, Key, AfterKey, Value, Quoted, Comment };

  bool IsValue() const {
    return phase_ == Phase::Value || phase_ == Phase::Quoted ||
           phase_ == Phase::Comment;
  }

  Phase phase_ = Phase::LineStart;
  std::size_t characters_ = 0, key_length_ = 0;
  bool lead_surrogate_ = false, escaped_ = false, has_entry_ = false;
};

bool CheckConfig(std::string_view output, bool complete) {
  ConfigText text;
  std::size_t i = 0;
  for (; i + 1 < output.size(); i += 2) {
    const char16_t ch = static_cast<uint8_t>(output[i]) |
                        (static_cast<uint8_t>(output[i + 1]) << 8);
    if (!text.Push(ch))
      return false;
  }
  if (i != output.size())
    return !complete && text.AllowsLowByte(static_cast<uint8_t>(output[i]));
  return !complete || text.Complete();
}

bool CheckConfigPrefix(std::string_view output, std::size_t) {
  return CheckConfig(output, false);
}

bool CheckConfigResult(std::string_view output) {
  return CheckConfig(output, true);
}

xorkey_t CrackScene(std::span<const std::string_view> scenes) {
  return crack::Cracker(scenes, {.xor_key = easykey,
                                 .min_output = sizeof(Scene_hdr),
                                 .prefix_size = sizeof(Scene_hdr),
                                 .check_prefix = CheckHeader})
      .Crack();
}

xorkey_t CrackGameexe(std::string_view compressed) {
  const std::array samples{compressed};
  return crack::Cracker(samples, {.xor_key = gexe_key,
                                  .min_output = 2,
                                  .prefix_size = 256,
                                  .check_prefix = CheckConfigPrefix,
                                  .check_result = CheckConfigResult,
                                  .alignment = 2,
                                  .prefer_zeroes = true})
      .Crack();
}

}  // namespace libsiglus
