// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
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

// include headers that implement a archive in simple text format
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/date_time/posix_time/time_serialize.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/split_free.hpp>
#include <boost/serialization/vector.hpp>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "core/object_internal/drawer/anm.hpp"
#include "core/object_internal/drawer/gan.hpp"
#include "machine/rlmachine.hpp"
#include "machine/save_game_header.hpp"
#include "machine/serialization.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"
#include "utilities/exception.hpp"
#include "utilities/gettext.hpp"

namespace fs = std::filesystem;

namespace Serialization {

RLMachine* g_current_machine = NULL;

RLMachine& currentMachine() {
  if (!g_current_machine)
    throw std::logic_error("No machine is being deserialized");
  return *g_current_machine;
}

System& currentSystem() {
  if (!g_current_machine)
    throw std::runtime_error("No active serialization machine context");
  return g_current_machine->GetSystem();
}

}  // namespace Serialization

namespace {

void ReadLocalFormat(boost::archive::text_iarchive& archive) {
  int version;
  std::string signature;
  archive >> version >> signature;
  if (version != Serialization::CURRENT_LOCAL_VERSION ||
      signature != Serialization::LOCAL_FORMAT_SIGNATURE)
    throw std::runtime_error("Unsupported local save format");
}

template <typename TYPE>
void checkInFileOpened(TYPE& file, const fs::path& home) {
  if (!file) {
    throw rlvm::Exception(
        str(format(_("Could not open save game file %1%")) % home));
  }
}

}  // namespace

namespace Serialization {

void saveGameForSlot(RLMachine& machine, int slot) {
  fs::path path = buildSaveGameFilename(machine, slot);
  std::ofstream file(path, std::ios::binary);
  checkInFileOpened(file, path);

  saveGameTo(file, machine);
}

void saveGameTo(std::ostream& oss, RLMachine& machine) {
  boost::iostreams::filtering_stream<boost::iostreams::output> filtered_output;
  filtered_output.push(boost::iostreams::zlib_compressor());
  filtered_output.push(oss);

  const SaveGameHeader header(machine.window_title()->subtitle_);

  g_current_machine = &machine;

  try {
    boost::archive::text_oarchive oa(filtered_output);
    oa << CURRENT_LOCAL_VERSION << std::string(LOCAL_FORMAT_SIGNATURE) << header
       << machine.GetMemoryStorage().GetLocalMemory()
       << const_cast<const RLMachine&>(machine)
       << const_cast<const TextSystem&>(machine.GetSystem().text())
       << const_cast<const SoundSystem&>(machine.GetSystem().sound());
  } catch (std::exception& e) {
    std::cerr << "--- WARNING: ERROR DURING SAVING FILE: " << e.what() << " ---"
              << std::endl;

    g_current_machine = NULL;
    throw;
  }

  g_current_machine = NULL;
}

fs::path buildSaveGameFilename(RLMachine& machine, int slot) {
  std::ostringstream oss;
  oss << "save" << std::setw(3) << std::setfill('0') << slot << ".sav.gz";

  return machine.GetSystem().GameSaveDirectory() / oss.str();
}

SaveGameHeader loadHeaderForSlot(RLMachine& machine, int slot) {
  fs::path path = buildSaveGameFilename(machine, slot);
  std::ifstream file(path, std::ios::binary);
  checkInFileOpened(file, path);

  return loadHeaderFrom(file);
}

SaveGameHeader loadHeaderFrom(std::istream& iss) {
  boost::iostreams::filtering_stream<boost::iostreams::input> filtered_input;
  filtered_input.push(boost::iostreams::zlib_decompressor());
  filtered_input.push(iss);

  SaveGameHeader header;

  // Only load the header
  boost::archive::text_iarchive ia(filtered_input);
  ReadLocalFormat(ia);
  ia >> header;

  return header;
}

void loadLocalMemoryForSlot(RLMachine& machine, int slot, Memory& memory) {
  fs::path path = buildSaveGameFilename(machine, slot);
  std::ifstream file(path, std::ios::binary);
  checkInFileOpened(file, path);

  loadLocalMemoryFrom(file, memory);
}

void loadLocalMemoryFrom(std::istream& iss, Memory& memory) {
  boost::iostreams::filtering_stream<boost::iostreams::input> filtered_input;
  filtered_input.push(boost::iostreams::zlib_decompressor());
  filtered_input.push(iss);

  SaveGameHeader header;
  LocalMemory local_memory;

  // Only load the header
  boost::archive::text_iarchive ia(filtered_input);
  ReadLocalFormat(ia);
  ia >> header >> local_memory;
  memory.PartialReset(std::move(local_memory));
}

void loadGameForSlot(RLMachine& machine, int slot) {
  fs::path path = buildSaveGameFilename(machine, slot);
  std::ifstream file(path, std::ios::binary);
  checkInFileOpened(file, path);

  loadGameFrom(file, machine);
}

void loadGameFrom(std::istream& iss, RLMachine& machine) {
  boost::iostreams::filtering_stream<boost::iostreams::input> filtered_input;
  filtered_input.push(boost::iostreams::zlib_decompressor());
  filtered_input.push(iss);

  SaveGameHeader header;

  g_current_machine = &machine;

  try {
    boost::archive::text_iarchive ia(filtered_input);
    ReadLocalFormat(ia);
    LocalMemory local_memory;
    ia >> header >> local_memory;

    // Long operations may borrow subsystem objects; clear the stack first.
    // Validate the layout before resetting any current machine state.
    machine.Reset();
    ia >> machine >> machine.GetSystem().text() >> machine.GetSystem().sound();

    machine.GetMemoryStorage().PartialReset(std::move(local_memory));
    machine.ApplyConfiguration();
    machine.ReplayGraphicsStackCommand();
    machine.ForceRefresh();
  } catch (std::exception& e) {
    std::cerr << "--- WARNING: ERROR DURING LOADING FILE: " << e.what()
              << " ---" << std::endl;

    g_current_machine = NULL;
    throw;
  }

  g_current_machine = NULL;
}

}  // namespace Serialization
