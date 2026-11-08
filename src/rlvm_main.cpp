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
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "log/core.hpp"
#include "platforms/implementor.hpp"
#include "platforms/platform_factory.hpp"
#include "rlvm_instance.hpp"
#include "sgvm_instance.hpp"
#include "utilities/game_detection.hpp"
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
      "version", "Display version and license information")(
      "font", po::value<std::string>(), "Specifies TrueType font to use.")(
      "scene", po::value<int>(),
      "Specifies which scenario we should start execution from")(
      "platform", po::value<std::string>(),
      "Specifies which gui platform to use.")(
      "show-platforms", "Print all avaliable gui platforms.")(
      "log-level", po::value<std::string>(),
      "Minimum severity level to log (none, info, warning, error)")(
      "trace", po::value<std::string>(),
      "Trace instructions: --trace[=script|frame-action|all]")(
      "fast-forward", "Fast forward execution by injecting Ctrl key events")(
      "debug",
      po::value<bool>()
          ->default_value(false, "false")
          ->implicit_value(true, "true"),
      "Enable debug instrumentation (true/false)")(
      "hook", po::value<std::string>(),
      "Run a script before the game payload (Siglus only).");

  // Declare the final option to be game-root
  po::options_description hidden("Hidden");
  hidden.add_options()("game-root", po::value<std::string>(),
                       "Location of game root");

  po::positional_options_description p;
  p.add("game-root", -1);

  // Use these on the command line
  po::options_description commandLineOpts;
  commandLineOpts.add(opts).add(hidden);

  po::variables_map vm;
  try {
    // Optional string arguments otherwise consume the following game path.
    std::vector<std::string> arguments;
    bool parse_options = true;
    for (int i = 1; i < argc; ++i) {
      std::string argument(argv[i]);
      if (argument == "--")
        parse_options = false;
      if (parse_options && argument == "--trace")
        argument = "--trace=script";
      arguments.push_back(std::move(argument));
    }
    po::store(po::basic_command_line_parser<char>(arguments)
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
  TraceOptions trace_options;
  if (vm.count("trace")) {
    try {
      trace_options = ParseTraceOptions(vm["trace"].as<std::string>());
    } catch (const std::invalid_argument& e) {
      std::cerr << e.what() << std::endl;
      return -1;
    }
  }
  SetupLogging(log_level, trace_options);

  if (vm.count("help")) {
    printUsage(argv[0], opts);
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
  } else {
    if (!platform_impl) {
      std::cerr << "ERROR: Specify a game directory on the command line."
                << std::endl;
      return -1;
    }
    gamerootPath = platform_impl->SelectGameDirectory();
    if (gamerootPath.empty())
      return -1;
  }

  GameLayout game;
  try {
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
    game = DetectGameRoot(gamerootPath);
  } catch (const fs::filesystem_error& e) {
    std::cerr << "ERROR: Could not inspect game directory: " << e.what()
              << std::endl;
    return -1;
  }
  if (game.engine == EngineKind::Unknown) {
    std::cerr << "ERROR: Could not detect a RealLive or Siglus game in '"
              << gamerootPath << "'." << std::endl;
    for (const auto& reason : game.evidence)
      std::cerr << "  " << reason << std::endl;
    std::cerr << "Select a directory containing Scene.pck or Gameexe.dat, or "
                 "Gameexe.ini with RealLive scenarios."
              << std::endl;
    return -1;
  }
  if (vm.count("hook") && game.engine != EngineKind::Siglus) {
    std::cerr << "ERROR: --hook requires a Siglus game." << std::endl;
    return -1;
  }

  // -----------------------------------------------------------------------
  // Create game instance

  if (game.engine == EngineKind::RealLive) {
    RLVMInstance instance;
    instance.SetPlatformImplementor(platform_impl);
    instance.SetFastForward(vm.count("fast-forward"));

    if (vm.count("font"))
      instance.SetCustomFont(vm["font"].as<std::string>());

    if (vm.count("scene"))
      instance.SetStartScene(vm["scene"].as<int>());

    return instance.Main(game);
  } else if (game.engine == EngineKind::Siglus) {
    SgvmInstance instance;
    instance.platform_implementor_ = platform_impl;
    instance.fast_forward_ = vm.count("fast-forward");
    if (vm.count("font"))
      instance.font_ = vm["font"].as<std::string>();
    if (vm.count("scene"))
      instance.start_scene_ = vm["scene"].as<int>();
    if (vm.count("hook"))
      instance.hook_path_ = vm["hook"].as<std::string>();
    instance.debug_ = vm["debug"].as<bool>();
    instance.trace_ = trace_options.Any();

    return instance.Main(game.root);
  }

  return 0;
}
