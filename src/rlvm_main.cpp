// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
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

#include <SDL3/SDL_main.h>

#include <boost/program_options.hpp>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include "log/core.hpp"
#include "platforms/implementor.hpp"
#include "platforms/platform_factory.hpp"
#include "rlvm_instance.hpp"
#include "sgvm_instance.hpp"
#include "systems/igraphics_backend.hpp"
#include "utilities/file.hpp"
#include "version.h"

namespace po = boost::program_options;
namespace fs = std::filesystem;

// -----------------------------------------------------------------------

void printVersionInformation() {
  std::cout
      << "rlvm (" << rlvm_version << ")\n"
      << "Copyright (C) 2006-2014 Elliot Glaysher, et all.\n\n"
      << "Contains code that is: \n"
      << "  Copyright (C) 2006-2007 Peter \"Haeleth\" Jolly\n"
      << "  Copyright (C) 2004-2006 Kazunori \"jagarl\" Ueno\n\n"

      << "This program is free software: you can redistribute it and/or modify"
      << '\n'
      << "it under the terms of the GNU General Public License as published by"
      << '\n'
      << "the Free Software Foundation, either version 3 of the License, or"
      << '\n'
      << "(at your option) any later version.\n\n"

      << "This program is distributed in the hope that it will be useful,"
      << '\n'
      << "but WITHOUT ANY WARRANTY; without even the implied warranty of"
      << '\n'
      << "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
      << "GNU General Public License for more details.\n\n"

      << "You should have received a copy of the GNU General Public License"
      << '\n'
      << "along with this program.  If not, see <http://www.gnu.org/licenses/>."
      << std::endl
      << std::endl;
}

void printUsage(const std::string& name, po::options_description& opts) {
  std::cout << "Usage: " << name << " [options] <game root>" << std::endl;
  std::cout << opts << std::endl;
}

int main(int argc, char* argv[]) {
  // -----------------------------------------------------------------------
  // Set up argument parser

  // Declare command line options
  po::options_description opts("Options");
  opts.add_options()("help", "Produce help message")(
      "help-debug", "Print help message for people working on rlvm")(
      "version", "Display version and license information")(
      "font", po::value<std::string>(), "Specifies TrueType font to use.")(
      "scene", po::value<int>(),
      "Specifies which scenario we should start execution from")(
      "platform", po::value<std::string>(),
      "Specifies which gui platform to use.")(
      "show-platforms", "Print all avaliable gui platforms.")(
      "log-level", po::value<std::string>(),
      "Minimum severity level to log (none, info, warning, error)")(
      "fast-forward",
      "Fast forward execution by injecting Ctrl key events")(
      "debug",
      po::value<bool>()
          ->default_value(false, "false")
          ->implicit_value(true, "true"),
      "Enable debug instrumentation (true/false)")(
      "engine", po::value<std::string>()->default_value("reallive"));

  po::options_description debugOpts("Debug options");
  debugOpts.add_options()(
      "debug-frame-dump", po::value<int>(),
      "Dump every N presented frames to ./rlvm-frame-dumps as BMP files.");

  // Declare the final option to be game-root
  po::options_description hidden("Hidden");
  hidden.add_options()("game-root", po::value<std::string>(),
                       "Location of game root");

  po::positional_options_description p;
  p.add("game-root", -1);

  // Use these on the command line
  po::options_description commandLineOpts;
  commandLineOpts.add(opts).add(debugOpts).add(hidden);

  po::variables_map vm;
  try {
    po::store(po::basic_command_line_parser<char>(argc, argv)
                  .options(commandLineOpts)
                  .positional(p)
                  .run(),
              vm);
    po::notify(vm);
  } catch (boost::program_options::multiple_occurrences& e) {
    std::cerr
        << "Couldn't parse command line: multiple_occurances." << std::endl
        << " (Hint: this can happen when your shell doesn't escape properly,"
        << std::endl
        << "  e.g. \"/path/to/Clannad Full Voice/\" without the quotes.)"
        << std::endl;
    return -1;
  } catch (boost::program_options::error& e) {
    std::cerr << "Couldn't parse command line: " << e.what() << std::endl;
    return -1;
  }

  // -----------------------------------------------------------------------

  po::options_description allOpts("Allowed options");
  allOpts.add(opts).add(debugOpts);

  // -----------------------------------------------------------------------
  // Process command line options
  fs::path gamerootPath;

  Severity log_level = Severity::Error;
  if (vm.count("log-level")) {
    std::string level = vm["log-level"].as<std::string>();
    for (auto& c : level)
      c = static_cast<char>(std::tolower(c));

    if (level == "none" || level.empty())
      log_level = Severity::None;
    else if (level == "info")
      log_level = Severity::Info;
    else if (level == "warning")
      log_level = Severity::Warn;
    else if (level == "error")
      log_level = Severity::Error;
  }
  SetupLogging(log_level);

  if (vm.count("help")) {
    printUsage(argv[0], opts);
    return 0;
  }

  if (vm.count("help-debug")) {
    printUsage(argv[0], allOpts);
    return 0;
  }

  if (vm.count("version")) {
    printVersionInformation();
    return 0;
  }

  if (vm.count("show-platforms")) {
    for (auto begin = PlatformFactory::cbegin(), end = PlatformFactory::cend();
         begin != end; ++begin) {
      std::cout << begin->first << std::endl;
    }
    return 0;
  }

  DebugFrameDumpConfig debug_frame_dump_config;
  if (vm.count("debug-frame-dump")) {
    int interval = vm["debug-frame-dump"].as<int>();
    if (interval <= 0) {
      std::cerr << "ERROR: --debug-frame-dump must be greater than zero."
                << std::endl;
      return -1;
    }

    debug_frame_dump_config.enabled = true;
    debug_frame_dump_config.frame_interval = interval;
    debug_frame_dump_config.output_dir = "rlvm-frame-dumps";

    std::error_code ec;
    fs::create_directories(debug_frame_dump_config.output_dir, ec);
    if (ec) {
      std::cerr << "ERROR: Could not create frame dump directory '"
                << debug_frame_dump_config.output_dir.string()
                << "': " << ec.message() << std::endl;
      return -1;
    }
  }

  // This is where we need platform implementor to pop up platform-specific
  // dialogue if we need to ask user for providing the path.
  PlatformImpl_t platform_impl = nullptr;
  if (vm.count("platform"))
    platform_impl = PlatformFactory::Create(vm["platform"].as<std::string>());
  else
    platform_impl = PlatformFactory::Create("default");

  if (!platform_impl) {
    // If everything fails
    std::cerr << "[WARNING] No gui implementation found." << std::endl;
  }

  // -----------------------------------------------------------------------
  // Select game root directory.
  if (vm.count("game-root")) {
    gamerootPath = vm["game-root"].as<std::string>();

    if (!fs::exists(gamerootPath)) {
      std::cerr << "ERROR: Path '" << gamerootPath << "' does not exist."
                << std::endl;
      return -1;
    }

    if (!fs::is_directory(gamerootPath)) {
      std::cerr << "ERROR: Path '" << gamerootPath << "' is not a directory."
                << std::endl;
      return -1;
    }

    // Some games hide data in a lower subdirectory.  A little hack to
    // make these behave as expected...
    if (CorrectPathCase(gamerootPath / "Gameexe.ini").empty()) {
      if (!CorrectPathCase(gamerootPath / "KINETICDATA" / "Gameexe.ini")
               .empty()) {
        gamerootPath /= "KINETICDATA/";
      } else if (!CorrectPathCase(gamerootPath / "REALLIVEDATA" / "Gameexe.ini")
                      .empty()) {
        gamerootPath /= "REALLIVEDATA/";
      } else {
        std::cerr << "WARNING: Path '" << gamerootPath << "' may not contain a "
                  << "RealLive game." << std::endl;
      }
    }
  } else {
    gamerootPath = platform_impl->SelectGameDirectory();
    if (gamerootPath.empty())
      return -1;
  }

  // -----------------------------------------------------------------------
  // Create game instance

  std::string engine = "reallive";
  if (vm.count("engine"))
    engine = vm["engine"].as<std::string>();

  if (engine == "reallive" || engine == "Reallive") {
    RLVMInstance instance;
    instance.SetPlatformImplementor(platform_impl);
    instance.SetDebugFrameDumpConfig(debug_frame_dump_config);
    instance.SetFastForward(vm.count("fast-forward"));

    if (vm.count("font"))
      instance.SetCustomFont(vm["font"].as<std::string>());

    if (vm.count("scene"))
      instance.SetStartScene(vm["scene"].as<int>());

    return instance.Main(gamerootPath);
  } else if (engine == "siglus" || engine == "Siglus") {
    SgvmInstance instance;
    instance.platform_implementor_ = platform_impl;
    instance.debug_frame_dump_config_ = debug_frame_dump_config;
    instance.fast_forward_ = vm.count("fast-forward");
    if (vm.count("font"))
      instance.font_ = vm["font"].as<std::string>();
    if (vm.count("scene"))
      instance.start_scene_ = vm["scene"].as<int>();
    instance.debug_ = vm["debug"].as<bool>();

    return instance.Main(gamerootPath);
  } else {
    std::cerr << "Couldn't recongnize engine: " << engine;
    return -1;
  }

  return 0;
}
