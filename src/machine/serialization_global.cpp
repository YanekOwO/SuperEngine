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

#include "machine/serialization.hpp"

// include headers that implement a archive in simple text format
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/split_free.hpp>
#include <boost/serialization/vector.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "machine/rlmachine.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"
#include "utilities/exception.hpp"
#include "utilities/gettext.hpp"

namespace fs = std::filesystem;

namespace Serialization {

fs::path buildGlobalMemoryFilename(RLMachine& machine) {
  return machine.GetSystem().GameSaveDirectory() / "global.sav.gz";
}

void saveGlobalMemory(RLMachine& machine) {
  fs::path home = buildGlobalMemoryFilename(machine);
  std::ofstream file(home, std::ios::binary);
  if (!file) {
    throw rlvm::Exception(_("Could not open global memory file."));
  }

  saveGlobalMemoryTo(file, machine);
}

void saveGlobalMemoryTo(std::ostream& oss, RLMachine& machine) {
  boost::iostreams::filtering_stream<boost::iostreams::output> filtered_output;
  filtered_output.push(boost::iostreams::zlib_compressor());
  filtered_output.push(oss);

  boost::archive::text_oarchive oa(filtered_output);

  oa << CURRENT_GLOBAL_VERSION << std::string(GLOBAL_FORMAT_SIGNATURE)
     << machine.GetMemoryStorage().GetGlobalMemory() << machine.GetKidokus()
     << machine.GetGlobalConfig() << machine.cg_table();
}

void loadGlobalMemory(RLMachine& machine) {
  fs::path home = buildGlobalMemoryFilename(machine);
  std::ifstream file(home, std::ios::binary);

  // If we were able to open the file for reading, load it. Don't
  // complain if we're unable to, since this may be the first run on
  // this certain game and it may not exist yet.
  if (file) {
    try {
      loadGlobalMemoryFrom(file, machine);
    } catch (...) {
      // Swallow ALL exceptions during file reading. If loading the global
      // memory file fails in any way, something is EXTREMELY wrong. Either
      // we're trying to read an incompatible old version's files or the global
      // data is corrupted. Either way, we can't safely do ANYTHING with this
      // game's entire save data so move it out of the way.
      file.close();
      fs::path save_dir = machine.GetSystem().GameSaveDirectory();
      const std::string backup_name =
          save_dir.filename().string() + ".old_corrupted_data";
      fs::path dest_save_dir = save_dir.parent_path() / backup_name;
      unsigned int suffix = 1;
      while (fs::exists(dest_save_dir))
        dest_save_dir = save_dir.parent_path() /
                        (backup_name + "." + std::to_string(suffix++));

      fs::rename(save_dir, dest_save_dir);

      std::cerr << "WARNING: Unable to read saved global memory file. Moving "
                << save_dir << " to " << dest_save_dir << std::endl;
    }
  }
}

void loadGlobalMemoryFrom(std::istream& iss, RLMachine& machine) {
  boost::iostreams::filtering_stream<boost::iostreams::input> filtered_input;
  filtered_input.push(boost::iostreams::zlib_decompressor());
  filtered_input.push(iss);

  boost::archive::text_iarchive ia(filtered_input);
  int version;
  std::string signature;
  ia >> version >> signature;
  if (version != CURRENT_GLOBAL_VERSION || signature != GLOBAL_FORMAT_SIGNATURE)
    throw std::runtime_error("Unsupported global save format");

  GlobalMemory global_memory;
  KidokuTable kidokus;
  GlobalConfig config;
  // CG asset metadata is not serialized, so retain its filename mapping.
  CGMTable cg_table = machine.cg_table();
  ia >> global_memory >> kidokus >> config >> cg_table;

  machine.GetMemoryStorage().PartialReset(std::move(global_memory));
  machine.GetKidokus() = std::move(kidokus);
  machine.GetGlobalConfig() = std::move(config);
  machine.cg_table() = std::move(cg_table);
  machine.ApplyConfiguration();
}

}  // namespace Serialization
