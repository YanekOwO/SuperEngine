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
// -----------------------------------------------------------------------

#include "idumper.hpp"
#include "libsiglus/dumper.hpp"
#include "log/domain_logger.hpp"
#include "machine/dumper.hpp"
#include "utilities/file.hpp"

#include <boost/program_options.hpp>
#include <execution>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <syncstream>

namespace fs = std::filesystem;
namespace po = boost::program_options;

int main(int argc, char* argv[]) {
  std::string output;
  std::string input;
  int scenario;

  try {
    // Define options
    po::options_description desc("Allowed options");
    desc.add_options()("help,h", "Produce help message")(
        "output,o", po::value<std::string>(&output)->default_value("stdout"),
        "Specify output directory")(
        "input", po::value<std::string>(&input)->required(), "Input directory")(
        "scenario", po::value<int>(&scenario)->default_value(-1));

    // Positional arguments
    po::positional_options_description pos_desc;
    pos_desc.add("input", 1);

    // Parse arguments
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv)
                  .options(desc)
                  .positional(pos_desc)
                  .run(),
              vm);

    // Handle --help
    if (vm.count("help")) {
      std::cout << "rlkp" << '\n';
      std::cout << desc << '\n';
      return 0;
    }

    po::notify(vm);
  } catch (const po::error& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << "\n";
    return 1;
  }

  fs::path game_root = input;
  if (!fs::exists(game_root)) {
    std::cerr << "ERROR: Path '" << game_root << "' does not exist."
              << std::endl;
    return -1;
  }

  // reallive game
  std::unique_ptr<IDumper> dumper = nullptr;
  fs::path gameexe_path = CorrectPath(game_root, "Gameexe.ini");
  fs::path seen_path = CorrectPath(game_root, "Seen.txt");

  if (fs::exists(gameexe_path) && fs::exists(seen_path)) {
    dumper = std::make_unique<Dumper>(gameexe_path, seen_path, game_root);
  } else {
    // siglus game
    gameexe_path = CorrectPath(game_root, "Gameexe.dat");
    seen_path = CorrectPath(game_root, "Scene.pck");
    dumper =
        std::make_unique<libsiglus::Dumper>(gameexe_path, seen_path, game_root);
  }

  std::vector<int> scenarios;
  if (scenario >= 0)
    scenarios.push_back(scenario);
  std::vector<IDumper::Task> tasks = dumper->GetTasks(std::move(scenarios));
  auto run = [](std::ostream& os, typename IDumper::task_t& t) {
    try {
      t(os);
    } catch (std::exception& e) {
      static DomainLogger logger("main");
      logger(Severity::Error) << e.what();
    } catch (...) {
    }
  };

  if (output == "stdout") {
    std::for_each(std::execution::seq, tasks.begin(), tasks.end(),
                  [&run](Dumper::Task& t) {
                    std::cout << '\n'
                              << std::string(6, '=') << t.path.string()
                              << std::string(6, '=') << '\n';
                    run(std::cout, t.task);
                  });

  } else {
    fs::path output_path = output;
    fs::create_directories(output_path);
    fs::create_directory(output_path / "audio");
    fs::create_directory(output_path / "image");
    fs::create_directory(output_path / "movie");

    const size_t total_tasks = tasks.size();
    std::atomic<size_t> completed_count = 0;

    std::for_each(std::execution::par_unseq, tasks.begin(), tasks.end(),
                  [&](IDumper::Task& t) {
                    const size_t cnt = completed_count.fetch_add(1) + 1;
                    const int percentage = (cnt * 100) / total_tasks;
                    std::osyncstream(std::clog) << std::format(
                        "[{}%] {}\n", percentage, t.path.string());
                    std::ofstream ofs(output_path / t.path, std::ios::binary);
                    run(ofs, t.task);
                  });
  }

  return 0;
}
