// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2007 Elliot Glaysher
// Copyright (C) 2024 Serina Sakurai
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

#include "core/memory_internal/location.hpp"

#include <boost/serialization/access.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class Gameexe;
class MemoryHandle;

struct StackMemory {
  std::vector<std::vector<int>> L;
  std::vector<std::vector<std::string>> K;

 private:
  friend class boost::serialization::access;
  template <class Archive>
  void serialize(Archive& ar, unsigned int version) {
    ar & L & K;
  }
};

struct GlobalMemory {
  std::vector<int> G, Z;
  std::vector<std::string> M, global_names;

 private:
  friend class boost::serialization::access;
  template <class Archive>
  void serialize(Archive& ar, unsigned int version) {
    ar & G & Z & M & global_names;
  }
};

struct LocalMemory {
  LocalMemory();
  ~LocalMemory();

  std::vector<int> A, B, C, D, E, F, X, H, I, J;
  std::vector<std::string> S, local_names;

 private:
  friend class boost::serialization::access;
  template <class Archive>
  void serialize(Archive& ar, unsigned int version) {
    ar & A & B & C & D & E & F & X & H & I & J & S & local_names;
  }
};

// Class that encapsulates access to all integer and string
// memory. Multiple instances of this class will probably exist if
// save games are used.
class Memory {
 public:
  Memory();
  Memory(const Memory& other) = delete;
  Memory& operator=(const Memory& other) = delete;
  Memory(Memory&&) = delete;
  Memory& operator=(Memory&&) = delete;
  ~Memory();

  void Write(IntMemoryLocation, int);
  void Write(IntBank bank, size_t index, int value);
  void Write(StrMemoryLocation, const std::string&);
  void Write(StrBank bank, size_t index, const std::string& value);
  void Fill(IntBank, size_t begin, size_t end, int value);
  void Fill(StrBank, size_t begin, size_t end, const std::string& value);
  int Read(IntMemoryLocation);
  int Read(IntBank bank, size_t index);
  std::string Read(StrMemoryLocation);
  std::string Read(StrBank bank, size_t index);
  size_t Size(IntBank bank) const;
  size_t Size(StrBank bank) const;
  void Resize(IntBank, std::size_t);
  void Resize(StrBank, std::size_t);

  MemoryHandle CreateHandle();

  // Reads in default memory values from the passed in Gameexe, such as \#NAME
  // and \#LOCALNAME values.
  // @note For now, we only read \#NAME and \#LOCALNAME variables, skipping any
  // declaration of the form \#intvar[index] or \#strvar[index].
  void LoadFrom(Gameexe& gameexe);

  // Create and return a copy of global memory
  GlobalMemory GetGlobalMemory() const;

  // Create and return a copy of local memory
  LocalMemory GetLocalMemory() const;

  void PartialReset(GlobalMemory global_memory);
  void PartialReset(LocalMemory local_memory);

  std::vector<int>& GetIntBankData(IntBank);
  const std::vector<int>& GetIntBankData(IntBank) const;
  std::vector<std::string>& GetStrBankData(StrBank);
  const std::vector<std::string>& GetStrBankData(StrBank) const;

 private:
  static constexpr auto int_bank_cnt = static_cast<size_t>(IntBank::CNT);
  static constexpr auto str_bank_cnt = static_cast<size_t>(StrBank::CNT);
  static constexpr std::size_t kDefaultBankSize = 2000;

  std::array<std::vector<int>, int_bank_cnt> intbanks_;
  std::array<std::vector<std::string>, str_bank_cnt> strbanks_;
};

class MemoryHandle {
 public:
  MemoryHandle(Memory& memory);
  MemoryHandle(Memory& memory, std::shared_ptr<StackMemory> stack);
  MemoryHandle(const MemoryHandle&) = default;
  MemoryHandle(MemoryHandle&&) noexcept = default;
  MemoryHandle& operator=(const MemoryHandle&) = delete;
  MemoryHandle& operator=(MemoryHandle&&) = delete;

  void Write(IntMemoryLocation, int);
  void Write(IntBank bank, size_t index, int value);
  void Write(StrMemoryLocation, const std::string&);
  void Write(StrBank bank, size_t index, const std::string& value);

  void Fill(IntBank, size_t begin, size_t end, int value);
  void Fill(StrBank, size_t begin, size_t end, const std::string& value);

  int Read(IntMemoryLocation);
  int Read(IntBank bank, size_t index);
  std::string Read(StrMemoryLocation);
  std::string Read(StrBank bank, size_t index);

  size_t Size(IntBank bank) const;
  size_t Size(StrBank bank) const;

  void Resize(IntBank, std::size_t);
  void Resize(StrBank, std::size_t);

  int ReadStack(IntBank bank, std::size_t frame_offset, std::size_t index);
  std::string ReadStack(StrBank bank,
                        std::size_t frame_offset,
                        std::size_t index);
  void WriteStack(IntBank bank,
                  std::size_t frame_offset,
                  std::size_t index,
                  int value);
  void WriteStack(StrBank bank,
                  std::size_t frame_offset,
                  std::size_t index,
                  const std::string& value);

  std::vector<int>& GetIntBankData(IntBank);
  const std::vector<int>& GetIntBankData(IntBank) const;
  std::vector<std::string>& GetStrBankData(StrBank);
  const std::vector<std::string>& GetStrBankData(StrBank) const;

  void PushStackFrame(std::size_t int_size, std::size_t str_size);
  void PopStackFrame();
  std::size_t StackDepth() const;

  StackMemory GetStackMemory() const;
  void ResetStackMemory(StackMemory stack);
  MemoryHandle Clone() const;

 private:
  std::vector<int>& GetIntStackFrame(std::size_t frame_offset);
  std::vector<std::string>& GetStrStackFrame(std::size_t frame_offset);

  Memory& memory_;
  std::shared_ptr<StackMemory> stack_;
};
