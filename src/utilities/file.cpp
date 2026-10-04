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

fs::path CorrectPathCase(fs::path Path) {
  // If the path is OK as it stands, do nothing.
  if (fs::exists(Path))
    return Path;
  // If the path doesn't seem to be OK, track backwards through it
  // looking for the point at which the problem first arises.  Path
  // will contain the parts of the path that exist on the current
  // filesystem, and pathElts will contain the parts after that point,
  // which may have incorrect case.
  std::stack<std::string> pathElts;
  while (!Path.empty() && !fs::exists(Path)) {
    pathElts.push(Path.filename().string());
    Path = Path.parent_path();
  }
  // Now proceed forwards through the possibly-incorrect elements.
  while (!pathElts.empty()) {
    // Does this element need to be a directory?
    // (If we are searching for /foo/bar/baz, and /foo contains a file
    // bar and a directory Bar, then we need to know which is the one
    // we're looking for.  This will still be unreliable if /foo
    // contains directories bar and Bar, but a full backtracking
    // search would be complicated; for now this should be adequate!)
    const bool needDir = pathElts.size() > 1;
    std::string elt(pathElts.top());
    pathElts.pop();
    // Does this element exist?
    if (exists(Path / elt) && (!needDir || is_directory(Path / elt))) {
      // If so, use it.
      Path /= elt;
    } else {
      // If not, search for a suitable candidate.
      to_upper(elt);
      fs::directory_iterator end;
      bool found = false;
      for (fs::directory_iterator dir(Path); dir != end; ++dir) {
        std::string uleaf = dir->path().filename().string();
        to_upper(uleaf);
        if (uleaf == elt && (!needDir || is_directory(*dir))) {
          Path /= dir->path().filename();
          found = true;
          break;
        }
      }
      if (!found)
        return "";
    }
  }
  return Path.string();
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
               std::span<std::uint8_t> data) {
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
