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

#include "utilities/game_detection.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

#include "utilities/file.hpp"

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kTocEntries = 10000;
constexpr std::size_t kTocSize = kTocEntries * 8;

enum class ScenarioKind { Unknown, RealLive, Unsupported };

std::string UpperAscii(std::string text) {
  for (auto& c : text) {
    if (c >= 'a' && c <= 'z')
      c -= 'a' - 'A';
  }
  return text;
}

std::vector<fs::path> DirectoryEntries(const fs::path& directory) {
  std::vector<fs::path> entries;
  std::error_code ec;
  for (fs::directory_iterator it(directory, ec), end; !ec && it != end;
       it.increment(ec)) {
    entries.push_back(it->path());
  }
  std::sort(entries.begin(), entries.end());
  return entries;
}

fs::path FindDirectory(const fs::path& root, std::string name) {
  std::replace(name.begin(), name.end(), '\\', '/');
  const auto relative = fs::path(name).lexically_normal();
  if (relative.is_absolute() || relative.has_root_name() ||
      (name.size() >= 2 && name[1] == ':'))
    return {};

  auto current = root;
  for (const auto& component : relative) {
    if (component == ".")
      continue;
    if (component == "..")
      return {};
    fs::path match;
    for (const auto& entry : DirectoryEntries(current)) {
      std::error_code ec;
      if (UpperAscii(entry.filename().string()) !=
              UpperAscii(component.string()) ||
          !fs::is_directory(entry, ec))
        continue;
      if (match.empty() || entry.filename() == component)
        match = entry;
      if (entry.filename() == component)
        break;
    }
    if (match.empty())
      return {};
    current = match;
  }
  return current;
}

std::string ReadHead(const fs::path& path, std::size_t length) {
  std::ifstream file(path, std::ios::binary);
  std::string data(length, '\0');
  file.read(data.data(), data.size());
  data.resize(file.gcount());
  return data;
}

std::uint32_t U32At(std::string_view data, std::size_t at) {
  std::uint32_t result = 0;
  for (std::size_t i = 0; i < 4; ++i)
    result |=
        static_cast<std::uint32_t>(static_cast<unsigned char>(data[at + i]))
        << (i * 8);
  return result;
}

bool IsRealLiveHeader(std::string_view data) {
  return data.size() >= 4 &&
         (U32At(data, 0) == 0x1d0 || U32At(data, 0) == 0x1cc);
}

ScenarioKind ClassifyArchive(const fs::path& path) {
  const auto toc = ReadHead(path, kTocSize);
  if (toc.starts_with("PACL"))
    return ScenarioKind::Unsupported;
  if (toc.size() < kTocSize)
    return ScenarioKind::Unknown;

  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  if (ec)
    return ScenarioKind::Unknown;
  std::ifstream file(path, std::ios::binary);
  unsigned valid = 0;
  for (std::size_t i = 0; i < kTocEntries; ++i) {
    const auto offset = U32At(toc, i * 8);
    const auto length = U32At(toc, i * 8 + 4);
    if (offset == 0 || length < 4 ||
        static_cast<std::uint64_t>(offset) + length > size)
      continue;
    char header[4];
    file.seekg(offset);
    if (!file.read(header, sizeof(header)) ||
        !IsRealLiveHeader(std::string_view(header, sizeof(header))))
      return ScenarioKind::Unknown;
    if (++valid == 4)
      break;
  }
  return valid ? ScenarioKind::RealLive : ScenarioKind::Unknown;
}

ScenarioKind ClassifyLooseScenes(const fs::path& directory) {
  auto by_name = ScenarioKind::Unknown;
  for (const auto& path : DirectoryEntries(directory)) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec))
      continue;
    const auto name = UpperAscii(path.filename().string());
    if (!name.starts_with("SEEN") || !name.ends_with(".TXT"))
      continue;
    const auto digits = std::string_view(name).substr(4, name.size() - 8);
    if (digits.empty() ||
        !std::all_of(digits.begin(), digits.end(),
                     [](char c) { return c >= '0' && c <= '9'; }))
      continue;
    const auto head = ReadHead(path, 8);
    if (head.starts_with("TPC32"))
      return ScenarioKind::Unsupported;
    if (IsRealLiveHeader(head))
      return ScenarioKind::RealLive;
    if (by_name == ScenarioKind::Unknown) {
      if (digits.size() == 3)
        by_name = ScenarioKind::Unsupported;
      else if (digits.size() == 4)
        by_name = ScenarioKind::RealLive;
    }
  }
  return by_name;
}

std::optional<std::pair<std::string, std::string>> Foldname(
    const fs::path& gameexe) {
  std::ifstream file(gameexe, std::ios::binary);
  std::string line;
  while (std::getline(file, line)) {
    const auto start = line.find_first_not_of(" \t\r\n\f\v");
    if (start == std::string::npos ||
        !UpperAscii(line.substr(start)).starts_with("#FOLDNAME.TXT"))
      continue;
    std::vector<std::string> quoted;
    auto at = line.find('"', start);
    while (at != std::string::npos && quoted.size() < 2) {
      const auto end = line.find('"', at + 1);
      if (end == std::string::npos)
        break;
      quoted.push_back(line.substr(at + 1, end - at - 1));
      at = line.find('"', end + 1);
    }
    return std::pair{
        quoted.empty() ? "" : quoted[0],
        quoted.size() < 2 || quoted[1].empty() ? "SEEN.TXT" : quoted[1]};
  }
  return std::nullopt;
}

using Location = std::pair<fs::path, std::string>;

std::vector<Location> ScenarioLocations(const GameLayout& layout) {
  std::vector<Location> locations;
  auto add = [&](fs::path directory, std::string file) {
    Location location{std::move(directory), std::move(file)};
    if (!location.first.empty() && std::find(locations.begin(), locations.end(),
                                             location) == locations.end())
      locations.push_back(std::move(location));
  };
  if (const auto fold = Foldname(layout.gameexe_ini)) {
    add(FindDirectory(layout.root, fold->first), fold->second);
    add(layout.root, fold->second);
  }
  add(FindDirectory(layout.root, "DAT"), "SEEN.TXT");
  add(layout.root, "SEEN.TXT");
  return locations;
}

GameLayout DetectDirectory(const fs::path& root) {
  GameLayout layout;
  layout.root = root;
  layout.gameexe_ini = CorrectPath(root, "Gameexe.ini");
  for (const auto* marker : {"Scene.pck", "Gameexe.dat"}) {
    if (const auto path = CorrectPath(root, marker); !path.empty())
      layout.evidence.push_back(path.filename().string() + " present");
  }
  if (!layout.evidence.empty()) {
    layout.engine = EngineKind::Siglus;
    return layout;
  }
  if (layout.gameexe_ini.empty()) {
    layout.evidence.push_back("No Scene.pck, Gameexe.dat or Gameexe.ini found");
    return layout;
  }
  layout.evidence.push_back("Gameexe.ini present");
  for (const auto& [directory, file] : ScenarioLocations(layout)) {
    const auto archive = CorrectPath(directory, file);
    const auto archive_kind =
        archive.empty() ? ScenarioKind::Unknown : ClassifyArchive(archive);
    if (archive_kind == ScenarioKind::Unsupported) {
      layout.evidence.push_back("Unsupported AVG32 scenario archive");
      return layout;
    }
    if (archive_kind == ScenarioKind::RealLive) {
      layout.engine = EngineKind::RealLive;
      layout.seen_archive = archive;
      layout.evidence.push_back("Recognizable RealLive scenario archive");
      return layout;
    }
    const auto loose_kind = ClassifyLooseScenes(directory);
    if (loose_kind == ScenarioKind::Unsupported) {
      layout.evidence.push_back("Unsupported AVG32 loose scenarios");
      return layout;
    }
    if (loose_kind == ScenarioKind::RealLive) {
      layout.engine = EngineKind::RealLive;
      layout.seen_directory = directory;
      layout.evidence.push_back("Recognizable loose RealLive scenarios");
      return layout;
    }
  }
  layout.evidence.push_back("No recognizable RealLive scenarios found");
  return layout;
}

}  // namespace

GameLayout DetectGameRoot(const fs::path& root) {
  const auto canonical = fs::canonical(root);
  if (!fs::is_directory(canonical)) {
    throw fs::filesystem_error(
        "Game root is not a directory", root,
        std::make_error_code(std::errc::not_a_directory));
  }
  auto layout = DetectDirectory(canonical);
  if (layout.engine != EngineKind::Unknown)
    return layout;
  for (const auto* name : {"KINETICDATA", "REALLIVEDATA"}) {
    const auto directory = FindDirectory(canonical, name);
    if (directory.empty())
      continue;
    auto nested = DetectDirectory(directory);
    if (nested.engine != EngineKind::Unknown)
      return nested;
  }
  return layout;
}
