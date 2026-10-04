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

#include "core/avdec/iadec.hpp"
#include "core/avspec.hpp"
#include "utilities/mapped_file.hpp"

#include <any>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

using decoder_t = std::shared_ptr<IAudioDecoder>;

class ADecoderFactory {
 public:
  using decoder_constructor_t = std::function<decoder_t(std::string_view)>;

  ADecoderFactory();

  decoder_t Create(std::string_view data,
                   std::optional<std::string> format_hint = {});

 protected:
  const std::unordered_map<std::string, decoder_constructor_t>* decoder_map_;

 private:
  static std::unordered_map<std::string, decoder_constructor_t>
      default_decoder_map_;
};

class AudioDecoder {
 public:
  AudioDecoder(FilePos fp, const std::string& format = "");
  AudioDecoder(std::shared_ptr<IAudioDecoder> dec);
  AudioDecoder(std::filesystem::path filepath, const std::string& format = "");
  AudioDecoder(std::string filestr, const std::string& format = "");

  AudioData DecodeAll() const;
  AudioData DecodeNext() const;
  bool HasNext() const;
  void Rewind() const;
  AVSpec GetSpec() const;
  SEEK_RESULT Seek(long long offset, SEEKDIR whence = SEEKDIR::CUR) const;
  long long Tell() const;

 private:
  std::any dataholder_; /* if we are responsible for memory management, the
                           container class is being held here */
  decoder_t decoderimpl_;

  static ADecoderFactory factory_;
};
