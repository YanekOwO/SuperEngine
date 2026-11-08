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

#pragma once

#include "core/object_internal/objdrawer.hpp"
#include "core/serialization_context.hpp"
#include "utilities/serialization.hpp"

#include <gtest/gtest.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <sstream>
#include <string>
#include <utility>

namespace serialization_test {
enum class Format { Binary, Text };

template <class Function>
void ForEachFormat(Function run) {
  for (auto format : {Format::Binary, Format::Text}) {
    SCOPED_TRACE(format == Format::Binary ? "binary archive" : "text archive");
    run(format);
  }
}

template <class Function>
std::string Write(Format format,
                  Function write,
                  SerializationContext context = {}) {
  std::ostringstream bytes(std::ios::binary);
  auto encode = [&](auto& archive) {
    SerializationContext::Init(archive, std::move(context));
    write(archive);
  };
  if (format == Format::Binary) {
    boost::archive::binary_oarchive archive(bytes);
    encode(archive);
  } else {
    boost::archive::text_oarchive archive(bytes);
    encode(archive);
  }
  return bytes.str();
}

template <class Function>
void Read(const std::string& bytes,
          Format format,
          Function read,
          SerializationContext context = {}) {
  std::istringstream input(bytes, std::ios::binary);
  auto decode = [&](auto& archive) {
    SerializationContext::Init(archive, std::move(context));
    read(archive);
  };
  if (format == Format::Binary) {
    boost::archive::binary_iarchive archive(input);
    decode(archive);
  } else {
    boost::archive::text_iarchive archive(input);
    decode(archive);
  }
}

template <class T>
std::string Save(const T& value,
                 Format format,
                 SerializationContext context = {}) {
  return Write(
      format, [&](auto& archive) { archive << value; }, std::move(context));
}

template <class T>
void Load(const std::string& bytes,
          T& value,
          Format format,
          SerializationContext context = {}) {
  Read(
      bytes, format, [&](auto& archive) { archive >> value; },
      std::move(context));
}

template <class T>
void RoundTrip(const T& source,
               T& target,
               Format format,
               SerializationContext saving = {},
               SerializationContext loading = {}) {
  Load(Save(source, format, std::move(saving)), target, format,
       std::move(loading));
}

}  // namespace serialization_test
