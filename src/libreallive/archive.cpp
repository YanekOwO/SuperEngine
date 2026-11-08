// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of libreallive, a dependency of RLVM.
//
// -----------------------------------------------------------------------
//
// Copyright (c) 2006, 2007 Peter Jolly
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// -----------------------------------------------------------------------

#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "encodings/codepage.hpp"
#include "libreallive/archive.hpp"
#include "libreallive/xorkey.hpp"
#include "utilities/byte_reader.hpp"
#include "utilities/mapped_file.hpp"

using boost::iends_with;
using boost::istarts_with;
namespace fs = std::filesystem;

namespace libreallive {

Archive::Archive(const fs::path& filepath, const std::string& regname)
    : regname_(Codepage::Create(Encoding::cp932)->ConvertTo_utf8(regname)),
      second_level_xor_key_(NULL) {
  if (fs::is_directory(filepath)) {
    ReadOverrides(filepath);
  } else {
    ReadTOC(filepath);
    ReadOverrides(filepath.parent_path().empty() ? fs::path(".")
                                                 : filepath.parent_path());
  }
  FindXorKey();
}

Archive::~Archive() {}

Scenario* Archive::GetScenario(int index) {
  auto at = scenario_.find(index);
  if (at != scenario_.end())
    return at->second.get();
  else {  // make the scenario on demand
    toc_t::const_iterator st = toc_.find(index);
    if (st != toc_.end()) {
      auto scene_ptr =
          ParseScenario(st->second, index, regname_, second_level_xor_key_);
      auto raw_ptr = scene_ptr.get();
      scenario_[index] = std::move(scene_ptr);
      return raw_ptr;
    }
    return nullptr;
  }
}

int Archive::GetFirstScenarioID() const {
  if (toc_.empty())
    throw std::runtime_error("No RealLive scenarios found");
  return toc_.cbegin()->first;
}

int Archive::GetProbableEncodingType() const {
  // Directly create Header objects instead of Scenarios. We don't want to
  // parse the entire SEEN file here.
  for (auto it = toc_.cbegin(); it != toc_.cend(); ++it) {
    FilePos filepos = it->second;
    Header header(filepos.Read());
    if (header.rldev_metadata_.text_encoding() != 0)
      return header.rldev_metadata_.text_encoding();
  }

  return 0;
}

void Archive::ReadTOC(const fs::path& filepath) {
  std::shared_ptr<MappedFile> header = std::make_shared<MappedFile>(filepath);
  static constexpr int TOC_COUNT = 10000;
  static constexpr std::size_t TOC_SIZE = 8;

  ByteReader reader(header->Read(0, TOC_COUNT * TOC_SIZE));
  for (int i = 0; i < TOC_COUNT; ++i) {
    const int offset = reader.PopAs<int>(4);
    const int length = reader.PopAs<int>(4);

    if (offset)
      toc_[i] = FilePos{.file_ = header, .position = offset, .length = length};
  }
}

void Archive::ReadOverrides(const fs::path& directory) {
  // Iterate over all files in the directory and override the table of contents
  // if there is a loose numbered SEEN file.
  fs::directory_iterator end;
  for (fs::directory_iterator it(directory); it != end; ++it) {
    if (!it->is_regular_file())
      continue;
    std::string filename = it->path().filename().string();
    if (filename.size() > 8 && istarts_with(filename, "seen") &&
        iends_with(filename, ".txt")) {
      const auto digits =
          std::string_view(filename).substr(4, filename.size() - 8);
      if (!std::all_of(digits.begin(), digits.end(),
                       [](char c) { return c >= '0' && c <= '9'; }))
        continue;
      int index;
      const auto parsed =
          std::from_chars(digits.data(), digits.data() + digits.size(), index);
      if (parsed.ec != std::errc{})
        continue;
      FilePos filepos;
      filepos.file_ = std::make_shared<MappedFile>(it->path());
      filepos.position = 0;
      filepos.length = filepos.file_->Size();

      toc_[index] = filepos;
    }
  }
}

void Archive::FindXorKey() {
  if (regname_ == "KEY\\CLANNAD_FV") {
    second_level_xor_key_ = libreallive::clannad_full_voice_xor_mask;
  } else if (regname_ == "KEY\\リトルバスターズ！") {
    second_level_xor_key_ = libreallive::little_busters_xor_mask;
  } else if (regname_ == "KEY\\リトルバスターズ！ＥＸ") {
    second_level_xor_key_ = libreallive::little_busters_ex_xor_mask;
  } else if (regname_ == "StudioMebius\\SNOWSE") {
    second_level_xor_key_ = libreallive::snow_standard_edition_xor_mask;
  } else if (regname_ == "KEY\\クドわふたー") {
    // "KEY\<Kud Wafter in hiragana>"
    second_level_xor_key_ = libreallive::kud_wafter_xor_mask;
  } else if (regname_ == "KEY\\クドわふたー【全年齢対象版】") {
    second_level_xor_key_ = libreallive::kud_wafter_all_ages_xor_mask;
  }
}

}  // namespace libreallive
