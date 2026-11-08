// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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

#include "core/bgm_table.hpp"

#include "core/gameexe.hpp"
#include "log/domain_logger.hpp"

#include <algorithm>
#include <boost/algorithm/string/predicate.hpp>
#include <charconv>
#include <format>
#include <stdexcept>
#include <system_error>
#include <utility>

static DomainLogger logger("SiglusBgmTable");

BgmTable::BgmTable(std::vector<std::string> names, std::vector<bool> listened)
    : names_(std::move(names)), listened_(std::move(listened)) {
  if (names_.size() != listened_.size())
    throw std::invalid_argument("BGM names and listened state size mismatch");
}

BgmTable BgmTable::CreateFromSiglus(Gameexe& gameexe) {
  constexpr int kDefaultBgmCount = 32;
  constexpr int kMaximumBgmCount = 256;

  const int count = gameexe("BGM.CNT").Int().value_or(kDefaultBgmCount);
  if (count < 0 || count > kMaximumBgmCount) {
    throw std::runtime_error(
        std::format("Siglus BGM.CNT must be between 0 and {}, got {}",
                    kMaximumBgmCount, count));
  }

  std::vector<std::string> names(count);
  for (auto entry : gameexe.Filter("BGM.")) {
    const auto key_parts = entry.GetKeyParts();
    if (key_parts.size() != 2)
      continue;

    int index = -1;
    const std::string& index_text = key_parts[1];
    const auto [end, ec] = std::from_chars(
        index_text.data(), index_text.data() + index_text.size(), index);
    if (ec != std::errc() || end != index_text.data() + index_text.size())
      continue;
    if (index < 0 || index >= count) {
      logger(Severity::Warn)
          << "Ignoring out-of-range BGM table entry " << entry.key();
      continue;
    }

    if (auto name = entry.StrAt(0))
      names[index] = std::move(name.value());
  }

  return BgmTable(std::move(names), std::vector<bool>(count, false));
}

void BgmTable::SetListen(std::string name, bool value, bool warn_if_unknown) {
  const int index = Find(name);
  if (index < 0) {
    if (warn_if_unknown) {
      logger(Severity::Warn)
          << "BGM registered name '" << name << "' was not found";
    }
    return;
  }
  listened_[index] = value;
}

void BgmTable::SetListenAll(bool value) {
  std::fill(listened_.begin(), listened_.end(), value);
}

int BgmTable::GetListen(std::string name) const {
  const int index = Find(name);
  return index < 0 ? -1 : listened_[index] ? 1 : 0;
}

std::size_t BgmTable::Count() const { return listened_.size(); }

void BgmTable::RestoreListenState(std::vector<bool> listened) {
  if (listened.size() != names_.size())
    throw std::invalid_argument("saved BGM table size does not match the game");
  listened_ = std::move(listened);
}

int BgmTable::Find(const std::string& name) const {
  for (std::size_t i = 0; i < names_.size(); ++i) {
    if (!names_[i].empty() && boost::iequals(names_[i], name))
      return static_cast<int>(i);
  }
  return -1;
}
