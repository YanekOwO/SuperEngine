// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
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
//
// -----------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Returns the correctly capitalized native path.
// Returns an empty path if the file cannot be found.
// Assumes filename is a UTF-8, root-relative file path.
std::filesystem::path CorrectPath(const std::filesystem::path& root,
                                  std::string filename);

std::vector<char> LoadFile(const std::filesystem::path& file_path);
std::string LoadFileStr(const std::filesystem::path& file_path);
// Creates any missing parent directories before writing.
void WriteFile(const std::filesystem::path& file_path,
               std::span<const std::uint8_t> data);
void WriteFile(const std::filesystem::path& file_path, std::string_view data);

// ------------------------------------------------------------------------------

class ScopedCurrentPath {
 public:
  explicit ScopedCurrentPath(const std::filesystem::path& path);
  ~ScopedCurrentPath();

 private:
  std::filesystem::path previous_;
};

// ------------------------------------------------------------------------------

// RAII class that creates a unique temporary directory
class ScopedTemporary {
 public:
  ScopedTemporary();
  ~ScopedTemporary();

  inline std::filesystem::path dir() const { return path_; }

 private:
  std::filesystem::path path_;
};

// ------------------------------------------------------------------------------

class FileTransaction {
 public:
  struct Operation {
    std::filesystem::path file_path;
    std::optional<std::string> content;  // nullopt means delete the file.
  };

  // Queue a file write or deletion for the next commit.
  // Relative paths are resolved now. Lexically equivalent destinations throw
  // std::invalid_argument without changing the staged operations.
  void Stage(std::filesystem::path file_path,
             std::optional<std::string> content);

  // Apply staged changes, attempting to restore original files on failure.
  // Successful commits clear the operations; failed commits retain them to
  // retry.
  void Commit();

 private:
  std::vector<Operation> ops_;
};
