// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006 Elliot Glaysher
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

#include "utilities/file.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <filesystem>

#include <fstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <system_error>

using boost::to_upper;
namespace fs = std::filesystem;
namespace bfs = boost::filesystem;

// -----------------------------------------------------------------------

ScopedCurrentPath::ScopedCurrentPath(const fs::path& path)
    : previous_(fs::current_path()) {
  if (!path.empty())
    fs::current_path(path);
}

ScopedCurrentPath::~ScopedCurrentPath() {
  std::error_code ec;
  fs::current_path(previous_, ec);
}

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

// Returns the correctly capitalized native path.
// Returns an empty path if the file cannot be found.
// Assumes filename is a UTF-8, root-relative file path.
fs::path CorrectPath(const fs::path& root, std::string filename) {
  // Normalize Windows separators to portable separators.
  std::replace(filename.begin(), filename.end(), '\\', '/');

  auto is_windows_drive_path = [&] {
    return filename.size() >= 2 &&
           ((filename[0] >= 'A' && filename[0] <= 'Z') ||
            (filename[0] >= 'a' && filename[0] <= 'z')) &&
           filename[1] == ':';
  };
  if (is_windows_drive_path())
    return {};

  // ASCII case-insensitive comparison using native path characters.
  auto equalCaseInsensitive = [](const fs::path& a, const fs::path& b) {
    const auto& x = a.native();
    const auto& y = b.native();

    if (x.size() != y.size())
      return false;

    auto lower = [](fs::path::value_type c) {
      return (c >= 'A' && c <= 'Z')
                 ? static_cast<fs::path::value_type>(c + ('a' - 'A'))
                 : c;
    };

    for (size_t i = 0; i < x.size(); ++i) {
      if (lower(x[i]) != lower(y[i]))
        return false;
    }
    return true;
  };

  try {
    fs::path relative = fs::path(filename).lexically_normal();

    if (relative.empty() || relative.is_absolute() ||
        relative.has_root_name() || !fs::is_directory(root))
      return {};

    fs::path current = root;

    // Resolve each component against actual filesystem entries.
    for (auto it = relative.begin(); it != relative.end(); ++it) {
      const fs::path& wanted = *it;

      if (wanted == ".")
        continue;
      if (wanted == "..")
        return {};

      const bool last = std::next(it) == relative.end();
      fs::path match;

      for (const auto& entry : fs::directory_iterator(current)) {
        const fs::path actual = entry.path().filename();

        if (!equalCaseInsensitive(actual, wanted))
          continue;

        // Intermediate components must be directories;
        // the final component must be a regular file.
        if (last ? !entry.is_regular_file() : !entry.is_directory())
          continue;

        // Prefer exact capitalization if multiple names match.
        if (actual == wanted) {
          match = entry.path();
          break;
        }

        if (match.empty())
          match = entry.path();
      }

      if (match.empty())
        return {};

      current = match;
    }

    return fs::is_regular_file(current) ? current.make_preferred() : fs::path{};

  } catch (const fs::filesystem_error&) {
    return {};
  }
}

std::vector<char> LoadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);

  if (!file) {
    throw std::runtime_error("Failed to open file: " + path.string());
  }

  const std::streamsize size = file.tellg();
  if (size < 0) {
    throw std::runtime_error("Failed to get file size: " + path.string());
  }

  std::vector<char> buffer(static_cast<std::size_t>(size));
  file.seekg(0, std::ios::beg);
  if (!file.read(buffer.data(), size)) {
    throw std::runtime_error("Failed to read file: " + path.string());
  }

  return buffer;
}

std::string LoadFileStr(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);

  if (!file) {
    throw std::runtime_error("Failed to open file: " + path.string());
  }

  const std::streamsize size = file.tellg();
  if (size < 0) {
    throw std::runtime_error("Failed to get file size: " + path.string());
  }

  std::string buffer(static_cast<std::size_t>(size), '\0');
  file.seekg(0, std::ios::beg);
  if (!file.read(buffer.data(), size)) {
    throw std::runtime_error("Failed to read file: " + path.string());
  }

  return buffer;
}

void WriteFile(const std::filesystem::path& path,
               std::span<const std::uint8_t> data) {
  if (path.has_parent_path())
    fs::create_directories(path.parent_path());

  std::ofstream file(path, std::ios::binary);
  if (!file)
    throw std::runtime_error("Failed to open file: " + path.string());

  if (!file.write(reinterpret_cast<const char*>(data.data()), data.size()))
    throw std::runtime_error("Failed to write file: " + path.string());

  file.close();
  if (!file)
    throw std::runtime_error("Failed to close file: " + path.string());
}

ScopedTemporary::ScopedTemporary() {
  auto pth =
      bfs::temp_directory_path() / bfs::unique_path("rlvm-audio-%%%%-%%%%");
  path_ = pth.native();
  fs::create_directory(path_);
}

ScopedTemporary::~ScopedTemporary() {
  std::error_code ec;
  fs::remove_all(path_, ec);
}
