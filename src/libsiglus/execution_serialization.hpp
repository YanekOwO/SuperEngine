// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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

#include "libsiglus/native_wait_serialization.hpp"
#include "vm/serialization.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace serilang {
struct Fiber;
class Promise;
}  // namespace serilang

namespace libsiglus {
struct SiglusMachine;
void RememberBindingSymbols(SiglusMachine& machine);

struct SelectionExecutionState {
  bool present = false;
  std::shared_ptr<::Selection> prepared, active;
  std::string last_message;
  int mouse_x = 0, mouse_y = 0;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) {
    ar & present & prepared & active & last_message & mouse_x & mouse_y;
  }
};
// One writer/reader spans all machines in a checkpoint. GC objects and promises
// retain their VM identities; native resource pointers share the outer archive.
class ExecutionWriter {
 public:
  void WriteMachine(boost::archive::binary_oarchive& archive,
                    SiglusMachine& machine,
                    serilang::Fiber* restored_savepoint = nullptr);
  uint32_t PromiseId(const std::shared_ptr<serilang::Promise>& promise) {
    return session_.PromiseId(promise);
  }

 private:
  serilang::SerializationSession session_;
};
class ExecutionReader {
 public:
  std::vector<NativeWaitState> ReadMachine(
      boost::archive::binary_iarchive& archive,
      SiglusMachine& machine);
  std::shared_ptr<serilang::Promise> Promise(uint32_t id) {
    return session_.Promise(id);
  }
  void FinishRestore() { session_.FinishRestore(); }

 private:
  serilang::SerializationSession session_;
};
}  // namespace libsiglus
