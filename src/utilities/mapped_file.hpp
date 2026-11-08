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

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include "boost/iostreams/device/mapped_file.hpp"

class MappedFile {
 public:
  MappedFile(const std::string& filename, std::size_t size = 0);
  MappedFile(const std::filesystem::path& filepath, std::size_t size = 0);
  ~MappedFile();

  std::string_view Read(std::size_t position = 0) const;

  std::string_view Read(std::size_t position, std::size_t length) const;

  void Write(std::size_t position, const std::string& data);

  std::size_t Size() const { return file_.size(); }
  const std::filesystem::path& path() const { return path_; }

 private:
  boost::iostreams::mapped_file file_;
  std::filesystem::path path_;
};

struct FilePos {
  std::shared_ptr<MappedFile> file_;
  int position, length;

  std::string_view Read() const {
    if (!file_)
      throw std::runtime_error("File is nullptr");
    return file_->Read(position, length);
  }
};
