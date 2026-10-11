// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
// Copyright (C) 2007 Elliot Glaysher
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

#include "core/memory_internal/memory.hpp"

#include "core/gameexe.hpp"
#include "core/memory_internal/proxy.hpp"
#include "utilities/string_utilities.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

LocalMemory::LocalMemory()
    : A(2000),
      B(2000),
      C(2000),
      D(2000),
      E(2000),
      F(2000),
      X(2000),
      H(2000),
      I(2000),
      J(2000),
      S(2000),
      local_names(2000) {}

LocalMemory::~LocalMemory() = default;

Memory::Memory() {
  for (size_t i = 0; i < int_bank_cnt; ++i) {
    if (static_cast<IntBank>(i) != IntBank::L)
      intbanks_[i].resize(kDefaultBankSize);
  }
  for (size_t i = 0; i < str_bank_cnt; ++i) {
    if (static_cast<StrBank>(i) != StrBank::K)
      strbanks_[i].resize(kDefaultBankSize);
  }
}

Memory::~Memory() = default;

void Memory::LoadFrom(Gameexe& gameexe) {
  // Note: We ignore the \#NAME_MAXLEN variable because manual allocation is
  // error prone and for losers.
  for (auto it : gameexe.Filter("NAME.")) {
    try {
      Write(StrMemoryLocation(StrBank::global_name,
                              ConvertLetterIndexToInt(it.GetKeyParts().at(1))),
            RemoveQuotes(it.ToStr()));
    } catch (...) {
      std::cerr << "WARNING: Invalid format for key " << it.key() << std::endl;
    }
  }

  for (auto it : gameexe.Filter("LOCALNAME.")) {
    try {
      Write(StrMemoryLocation(StrBank::local_name,
                              ConvertLetterIndexToInt(it.GetKeyParts().at(1))),
            RemoveQuotes(it.ToStr()));
    } catch (...) {
      std::cerr << "WARNING: Invalid format for key " << it.key() << std::endl;
    }
  }
}

std::vector<int>& Memory::GetIntBankData(IntBank bank) {
  const auto bankidx = static_cast<uint8_t>(bank);
  if (bankidx >= int_bank_cnt)
    throw std::invalid_argument("Memory: invalid int bank " +
                                std::to_string(bankidx));
  if (bank == IntBank::L)
    throw std::logic_error("Memory: stack bank L requires a MemoryHandle.");

  return intbanks_[bankidx];
}

const std::vector<int>& Memory::GetIntBankData(IntBank bank) const {
  const auto bankidx = static_cast<uint8_t>(bank);
  if (bankidx >= int_bank_cnt)
    throw std::invalid_argument("Memory: invalid int bank " +
                                std::to_string(bankidx));
  if (bank == IntBank::L)
    throw std::logic_error("Memory: stack bank L requires a MemoryHandle.");

  return intbanks_[bankidx];
}

std::vector<std::string>& Memory::GetStrBankData(StrBank bank) {
  const auto bankidx = static_cast<uint8_t>(bank);
  if (bankidx >= str_bank_cnt)
    throw std::invalid_argument("Memory: invalid string bank " +
                                std::to_string(bankidx));
  if (bank == StrBank::K)
    throw std::logic_error("Memory: stack bank K requires a MemoryHandle.");

  return strbanks_[bankidx];
}

const std::vector<std::string>& Memory::GetStrBankData(StrBank bank) const {
  const auto bankidx = static_cast<uint8_t>(bank);
  if (bankidx >= str_bank_cnt)
    throw std::invalid_argument("Memory: invalid string bank " +
                                std::to_string(bankidx));
  if (bank == StrBank::K)
    throw std::logic_error("Memory: stack bank K requires a MemoryHandle.");

  return strbanks_[bankidx];
}

int Memory::Read(IntMemoryLocation loc) {
  return IntListProxy(GetIntBankData(loc.Bank()))
      .Get(loc.Index(), loc.Bitwidth());
}

int Memory::Read(IntBank bank, size_t index) {
  return IntListProxy(GetIntBankData(bank)).Get(index);
}

std::string Memory::Read(StrMemoryLocation loc) {
  return Read(loc.Bank(), loc.Index());
}

std::string Memory::Read(StrBank bank, size_t index) {
  return StrListProxy(GetStrBankData(bank)).Get(index);
}

size_t Memory::Size(IntBank bank) const { return GetIntBankData(bank).size(); }

size_t Memory::Size(StrBank bank) const { return GetStrBankData(bank).size(); }

void Memory::Write(IntMemoryLocation loc, int value) {
  IntListProxy(GetIntBankData(loc.Bank()))
      .Set(loc.Index(), value, loc.Bitwidth());
}

void Memory::Write(IntBank bankid, size_t index, int value) {
  IntListProxy(GetIntBankData(bankid)).Set(index, value);
}

void Memory::Write(StrMemoryLocation loc, const std::string& value) {
  StrListProxy(GetStrBankData(loc.Bank())).Set(loc.Index(), value);
}

void Memory::Write(StrBank bank, size_t index, const std::string& value) {
  Write(StrMemoryLocation(bank, index), value);
}

void Memory::Fill(IntBank bankid, size_t begin, size_t end, int value) {
  IntListProxy(GetIntBankData(bankid)).Fill(begin, end, value);
}

void Memory::Fill(StrBank bankid,
                  size_t begin,
                  size_t end,
                  const std::string& value) {
  StrListProxy(GetStrBankData(bankid)).Fill(begin, end, value);
}

void Memory::Resize(IntBank bankid, std::size_t size) {
  GetIntBankData(bankid).resize(size);
}

void Memory::Resize(StrBank bankid, std::size_t size) {
  GetStrBankData(bankid).resize(size);
}

GlobalMemory Memory::GetGlobalMemory() const {
  return GlobalMemory{
      .G = intbanks_[static_cast<uint8_t>(IntBank::G)],
      .Z = intbanks_[static_cast<uint8_t>(IntBank::Z)],
      .M = strbanks_[static_cast<uint8_t>(StrBank::M)],
      .global_names = strbanks_[static_cast<uint8_t>(StrBank::global_name)]};
}

void Memory::PartialReset(GlobalMemory global_memory) {
  intbanks_[static_cast<uint8_t>(IntBank::G)] = std::move(global_memory.G);
  intbanks_[static_cast<uint8_t>(IntBank::Z)] = std::move(global_memory.Z);
  strbanks_[static_cast<uint8_t>(StrBank::M)] = std::move(global_memory.M);
  strbanks_[static_cast<uint8_t>(StrBank::global_name)] =
      std::move(global_memory.global_names);
}

LocalMemory Memory::GetLocalMemory() const {
  LocalMemory local_memory;
  local_memory.A = intbanks_[static_cast<uint8_t>(IntBank::A)];
  local_memory.B = intbanks_[static_cast<uint8_t>(IntBank::B)];
  local_memory.C = intbanks_[static_cast<uint8_t>(IntBank::C)];
  local_memory.D = intbanks_[static_cast<uint8_t>(IntBank::D)];
  local_memory.E = intbanks_[static_cast<uint8_t>(IntBank::E)];
  local_memory.F = intbanks_[static_cast<uint8_t>(IntBank::F)];
  local_memory.X = intbanks_[static_cast<uint8_t>(IntBank::X)];
  local_memory.H = intbanks_[static_cast<uint8_t>(IntBank::H)];
  local_memory.I = intbanks_[static_cast<uint8_t>(IntBank::I)];
  local_memory.J = intbanks_[static_cast<uint8_t>(IntBank::J)];
  local_memory.S = strbanks_[static_cast<uint8_t>(StrBank::S)];
  local_memory.local_names =
      strbanks_[static_cast<uint8_t>(StrBank::local_name)];
  return local_memory;
}

void Memory::PartialReset(LocalMemory local_memory) {
  intbanks_[static_cast<uint8_t>(IntBank::A)] = std::move(local_memory.A);
  intbanks_[static_cast<uint8_t>(IntBank::B)] = std::move(local_memory.B);
  intbanks_[static_cast<uint8_t>(IntBank::C)] = std::move(local_memory.C);
  intbanks_[static_cast<uint8_t>(IntBank::D)] = std::move(local_memory.D);
  intbanks_[static_cast<uint8_t>(IntBank::E)] = std::move(local_memory.E);
  intbanks_[static_cast<uint8_t>(IntBank::F)] = std::move(local_memory.F);
  intbanks_[static_cast<uint8_t>(IntBank::X)] = std::move(local_memory.X);
  intbanks_[static_cast<uint8_t>(IntBank::H)] = std::move(local_memory.H);
  intbanks_[static_cast<uint8_t>(IntBank::I)] = std::move(local_memory.I);
  intbanks_[static_cast<uint8_t>(IntBank::J)] = std::move(local_memory.J);
  strbanks_[static_cast<uint8_t>(StrBank::S)] = std::move(local_memory.S);
  strbanks_[static_cast<uint8_t>(StrBank::local_name)] =
      std::move(local_memory.local_names);
}

MemoryHandle Memory::CreateHandle() { return MemoryHandle(*this); }

// ------------------------------------------------------------------------------
// class MemoryHandle
MemoryHandle::MemoryHandle(Memory& memory)
    : MemoryHandle(memory, std::make_shared<StackMemory>()) {}
MemoryHandle::MemoryHandle(Memory& memory, std::shared_ptr<StackMemory> stack)
    : memory_(memory), stack_(std::move(stack)) {
  if (!stack_)
    throw std::invalid_argument("MemoryHandle: stack memory is null.");
  if (stack_->L.size() != stack_->K.size())
    throw std::invalid_argument("MemoryHandle: mismatched stack bank depths.");
}

std::vector<int>& MemoryHandle::GetIntBankData(IntBank bank) {
  if (bank != IntBank::L)
    return memory_.GetIntBankData(bank);
  if (stack_->L.empty())
    throw std::runtime_error("MemoryHandle: stack bank L has no frame.");
  return stack_->L.back();
}

const std::vector<int>& MemoryHandle::GetIntBankData(IntBank bank) const {
  if (bank != IntBank::L)
    return memory_.GetIntBankData(bank);
  if (stack_->L.empty())
    throw std::runtime_error("MemoryHandle: stack bank L has no frame.");
  return stack_->L.back();
}

std::vector<std::string>& MemoryHandle::GetStrBankData(StrBank bank) {
  if (bank != StrBank::K)
    return memory_.GetStrBankData(bank);
  if (stack_->K.empty())
    throw std::runtime_error("MemoryHandle: stack bank K has no frame.");
  return stack_->K.back();
}

const std::vector<std::string>& MemoryHandle::GetStrBankData(
    StrBank bank) const {
  if (bank != StrBank::K)
    return memory_.GetStrBankData(bank);
  if (stack_->K.empty())
    throw std::runtime_error("MemoryHandle: stack bank K has no frame.");
  return stack_->K.back();
}

int MemoryHandle::Read(IntMemoryLocation loc) {
  return IntListProxy(GetIntBankData(loc.Bank()))
      .Get(loc.Index(), loc.Bitwidth());
}

int MemoryHandle::Read(IntBank bank, size_t index) {
  return IntListProxy(GetIntBankData(bank)).Get(index);
}

std::string MemoryHandle::Read(StrMemoryLocation loc) {
  return Read(loc.Bank(), loc.Index());
}

std::string MemoryHandle::Read(StrBank bank, size_t index) {
  return StrListProxy(GetStrBankData(bank)).Get(index);
}

size_t MemoryHandle::Size(IntBank bank) const {
  return GetIntBankData(bank).size();
}

size_t MemoryHandle::Size(StrBank bank) const {
  return GetStrBankData(bank).size();
}

void MemoryHandle::Write(IntMemoryLocation loc, int value) {
  IntListProxy(GetIntBankData(loc.Bank()))
      .Set(loc.Index(), value, loc.Bitwidth());
}

void MemoryHandle::Write(IntBank bankid, size_t index, int value) {
  IntListProxy(GetIntBankData(bankid)).Set(index, value);
}

void MemoryHandle::Write(StrMemoryLocation loc, const std::string& value) {
  StrListProxy(GetStrBankData(loc.Bank())).Set(loc.Index(), value);
}

void MemoryHandle::Write(StrBank bank, size_t index, const std::string& value) {
  Write(StrMemoryLocation(bank, index), value);
}

void MemoryHandle::Fill(IntBank bankid, size_t begin, size_t end, int value) {
  IntListProxy(GetIntBankData(bankid)).Fill(begin, end, value);
}

void MemoryHandle::Fill(StrBank bankid,
                        size_t begin,
                        size_t end,
                        const std::string& value) {
  StrListProxy(GetStrBankData(bankid)).Fill(begin, end, value);
}

void MemoryHandle::Resize(IntBank bankid, std::size_t size) {
  GetIntBankData(bankid).resize(size);
}

void MemoryHandle::Resize(StrBank bankid, std::size_t size) {
  GetStrBankData(bankid).resize(size);
}

std::vector<int>& MemoryHandle::GetIntStackFrame(std::size_t frame_offset) {
  if (frame_offset >= stack_->L.size())
    throw std::out_of_range("MemoryHandle: integer stack frame out of range.");
  return stack_->L[stack_->L.size() - 1 - frame_offset];
}

std::vector<std::string>& MemoryHandle::GetStrStackFrame(
    std::size_t frame_offset) {
  if (frame_offset >= stack_->K.size())
    throw std::out_of_range("MemoryHandle: string stack frame out of range.");
  return stack_->K[stack_->K.size() - 1 - frame_offset];
}

int MemoryHandle::ReadStack(IntBank bank,
                            std::size_t frame_offset,
                            std::size_t index) {
  if (bank != IntBank::L)
    throw std::invalid_argument("MemoryHandle: expected stack bank L.");
  return IntListProxy(GetIntStackFrame(frame_offset)).Get(index);
}

std::string MemoryHandle::ReadStack(StrBank bank,
                                    std::size_t frame_offset,
                                    std::size_t index) {
  if (bank != StrBank::K)
    throw std::invalid_argument("MemoryHandle: expected stack bank K.");
  return StrListProxy(GetStrStackFrame(frame_offset)).Get(index);
}

void MemoryHandle::WriteStack(IntBank bank,
                              std::size_t frame_offset,
                              std::size_t index,
                              int value) {
  if (bank != IntBank::L)
    throw std::invalid_argument("MemoryHandle: expected stack bank L.");
  IntListProxy(GetIntStackFrame(frame_offset)).Set(index, value);
}

void MemoryHandle::WriteStack(StrBank bank,
                              std::size_t frame_offset,
                              std::size_t index,
                              const std::string& value) {
  if (bank != StrBank::K)
    throw std::invalid_argument("MemoryHandle: expected stack bank K.");
  StrListProxy(GetStrStackFrame(frame_offset)).Set(index, value);
}

void MemoryHandle::PushStackFrame(std::size_t int_size, std::size_t str_size) {
  std::vector<int> int_frame(int_size);
  std::vector<std::string> str_frame(str_size);
  stack_->L.emplace_back(std::move(int_frame));
  try {
    stack_->K.emplace_back(std::move(str_frame));
  } catch (...) {
    stack_->L.pop_back();
    throw;
  }
}

void MemoryHandle::PopStackFrame() {
  if (stack_->L.empty() || stack_->K.empty())
    throw std::underflow_error("MemoryHandle: stack frame underflow.");
  stack_->L.pop_back();
  stack_->K.pop_back();
}

std::size_t MemoryHandle::StackDepth() const {
  if (stack_->L.size() != stack_->K.size())
    throw std::logic_error("MemoryHandle: mismatched stack bank depths.");
  return stack_->L.size();
}

StackMemory MemoryHandle::GetStackMemory() const { return *stack_; }

void MemoryHandle::ResetStackMemory(StackMemory stack) {
  if (stack.L.size() != stack.K.size())
    throw std::invalid_argument("MemoryHandle: mismatched stack bank depths.");
  *stack_ = std::move(stack);
}

MemoryHandle MemoryHandle::Clone() const {
  return MemoryHandle(memory_, std::make_shared<StackMemory>(*stack_));
}
