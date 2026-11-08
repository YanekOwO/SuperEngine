// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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
//
// -----------------------------------------------------------------------

#include "utilities/mapped_file.hpp"

#include <stdexcept>

MappedFile::MappedFile(const std::string& filename, std::size_t size)
    : path_(filename) {
  const bool is_readonly = size == 0;
  if (is_readonly) {
    file_.open(filename, boost::iostreams::mapped_file::mapmode::readonly);
  } else {
    file_.open(filename, boost::iostreams::mapped_file::mapmode::readwrite,
               size);
  }

  if (!file_.is_open())
    throw std::invalid_argument("Failed to open file: " + filename);
}

MappedFile::MappedFile(const std::filesystem::path& filepath, std::size_t size)
    : MappedFile(filepath.generic_string(), size) {}

MappedFile::~MappedFile() {
  if (file_.is_open())
    file_.close();
}

std::string_view MappedFile::Read(std::size_t position) const {
  if (!file_.is_open()) {
    throw std::runtime_error("Read operation failed: File is not open.");
  }
  if (position > file_.size()) {
    throw std::out_of_range(
        "Read operation failed: Position " + std::to_string(position) +
        " is out of range for file size " + std::to_string(file_.size()) + ".");
  }
  return Read(position, file_.size() - position);
}

std::string_view MappedFile::Read(std::size_t position,
                                  std::size_t length) const {
  if (!file_.is_open()) {
    throw std::runtime_error("Read operation failed: File is not open.");
  }
  if (position + length > file_.size()) {
    throw std::out_of_range("Read operation failed: Position " +
                            std::to_string(position) + " with length " +
                            std::to_string(length) + " exceeds file size " +
                            std::to_string(file_.size()) + ".");
  }

  return std::string_view(file_.const_data() + position, length);
}

void MappedFile::Write(std::size_t position, const std::string& data) {
  if (!file_.is_open()) {
    throw std::runtime_error("Write operation failed: File is not open.");
  }
  if (position + data.size() > file_.size()) {
    throw std::out_of_range(
        "Write operation failed: Position " + std::to_string(position) +
        " with data size " + std::to_string(data.size()) +
        " exceeds file size " + std::to_string(file_.size()) + ".");
  }

  char* dst = file_.data();
  if (dst == nullptr) {
    throw std::runtime_error(
        "Write operation failed: No write permission to file.");
  }

  std::copy(data.cbegin(), data.cend(), dst + position);
}
