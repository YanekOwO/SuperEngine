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

#include "utilities/byte_inserter.hpp"

/* wrapper around ByteInserter */
class oBytestream {
 public:
  oBytestream() : buffer_{}, back_(buffer_) {}
  ~oBytestream() = default;

  using ByteBuffer_t = ByteInserter::ByteBuffer_t;

  template <typename T>
  oBytestream& operator<<(const T& value) {
    back_ = value;
    return *this;
  }

  const ByteBuffer_t& Get() const { return buffer_; }

  ByteBuffer_t& Get() { return buffer_; }

  ByteBuffer_t GetCopy() { return buffer_; }

  void Flush() { buffer_.clear(); }

  size_t Tell() const { return buffer_.size(); }

 private:
  ByteBuffer_t buffer_;
  ByteInserter back_;
};
