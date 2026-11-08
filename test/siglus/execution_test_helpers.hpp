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

#include "libsiglus/execution_serialization.hpp"
#include "serialization_test_helpers.hpp"

namespace serialization_test {
inline std::string WriteMachine(libsiglus::ExecutionWriter& writer,
                                libsiglus::SiglusMachine& machine,
                                serilang::Fiber* restored_savepoint = nullptr) {
  std::ostringstream output(std::ios::binary);
  boost::archive::binary_oarchive archive(output);
  SerializationContext::Init(
      archive, libsiglus::MakeSerializationContext(machine.host()));
  writer.WriteMachine(archive, machine, restored_savepoint);
  return output.str();
}
inline std::vector<libsiglus::NativeWaitState> ReadMachine(
    libsiglus::ExecutionReader& reader,
    libsiglus::SiglusMachine& machine,
    const std::string& bytes) {
  std::istringstream input(bytes, std::ios::binary);
  boost::archive::binary_iarchive archive(input);
  SerializationContext::Init(
      archive, libsiglus::MakeSerializationContext(machine.host()));
  return reader.ReadMachine(archive, machine);
}
template <class T>
auto RoundTripMachineResource(const T& resource,
                              libsiglus::ExecutionWriter& writer,
                              libsiglus::ExecutionReader& reader,
                              libsiglus::SiglusMachine& source,
                              libsiglus::SiglusMachine& target) {
  std::ostringstream output(std::ios::binary);
  {
    boost::archive::binary_oarchive archive(output);
    SerializationContext::Init(
        archive, libsiglus::MakeSerializationContext(source.host()));
    archive << resource;
    writer.WriteMachine(archive, source);
  }
  std::istringstream input(output.str(), std::ios::binary);
  boost::archive::binary_iarchive archive(input);
  SerializationContext::Init(
      archive, libsiglus::MakeSerializationContext(target.host()));
  T loaded;
  archive >> loaded;
  auto waits = reader.ReadMachine(archive, target);
  return std::make_pair(std::move(loaded), std::move(waits));
}
}  // namespace serialization_test
