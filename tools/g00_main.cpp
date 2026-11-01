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

#include "utilities/g00_asset.hpp"

#include <boost/program_options.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace po = boost::program_options;

namespace {

std::string ReadFile(const fs::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input)
    throw std::runtime_error("Cannot open input: " + path.string());
  const auto size = input.tellg();
  if (size < 0 || size > 256 * 1024 * 1024)
    throw std::runtime_error("Input exceeds 256 MiB: " + path.string());
  std::string bytes(static_cast<std::size_t>(size), '\0');
  input.seekg(0);
  if (!input.read(bytes.data(), bytes.size()))
    throw std::runtime_error("Cannot read input: " + path.string());
  return bytes;
}

struct Output {
  fs::path path;
  std::vector<char> bytes;
};

// Check the entire batch before writing, and replace each output by rename so
// a failed write cannot truncate an existing asset.
void WriteFiles(const std::vector<Output>& outputs,
                const std::vector<fs::path>& inputs,
                bool force) {
  for (std::size_t i = 0; i < outputs.size(); ++i) {
    const auto& path = outputs[i].path;
    if (fs::exists(path) && (!force || !fs::is_regular_file(path)))
      throw std::runtime_error(
          "Output already exists (use --force to replace files): " +
          path.string());
    for (const auto& input : inputs)
      if (fs::weakly_canonical(path) == fs::weakly_canonical(input) ||
          (fs::exists(path) && fs::equivalent(path, input)))
        throw std::runtime_error("Output would overwrite an input: " +
                                 path.string());
    for (std::size_t j = 0; j < i; ++j)
      if (fs::weakly_canonical(path) == fs::weakly_canonical(outputs[j].path))
        throw std::runtime_error("Output paths must be distinct");
    const auto parent =
        path.has_parent_path() ? path.parent_path() : fs::path(".");
    if (!fs::is_directory(parent))
      throw std::runtime_error("Output directory does not exist: " +
                               parent.string());
  }
  std::vector<fs::path> temporary;
  try {
    for (std::size_t i = 0; i < outputs.size(); ++i) {
      const auto& output = outputs[i];
      const auto stamp =
          std::chrono::steady_clock::now().time_since_epoch().count();
      fs::path temp = output.path;
      temp += ".g00-tmp-" + std::to_string(stamp) + "-" + std::to_string(i);
      if (fs::exists(temp))
        throw std::runtime_error("Temporary output already exists: " +
                                 temp.string());
      temporary.push_back(temp);
      std::ofstream stream(temp, std::ios::binary);
      stream.write(output.bytes.data(), output.bytes.size());
      stream.close();
      if (!stream)
        throw std::runtime_error("Cannot write output: " +
                                 output.path.string());
    }
    for (std::size_t i = 0; i < outputs.size(); ++i) {
      if (!force && fs::exists(outputs[i].path))
        throw std::runtime_error("Output appeared while writing: " +
                                 outputs[i].path.string());
      fs::rename(temporary[i], outputs[i].path);
      std::cout << outputs[i].path.string() << '\n';
    }
  } catch (...) {
    for (const auto& temp : temporary) {
      std::error_code ignored;
      fs::remove(temp, ignored);
    }
    throw;
  }
}

int Integer(std::string_view text) {
  int value;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc() || end != text.data() + text.size() || value <= 0)
    throw std::runtime_error("Grid dimensions must be positive integers");
  return value;
}

void Help(const po::options_description& options) {
  std::cout
      << "G00 image asset tool\n\n"
         "  g00 info INPUT.g00 [--json]\n"
         "  g00 decode INPUT.g00 OUTPUT.png [--metadata OUTPUT.json]\n"
         "  g00 encode INPUT.png OUTPUT.g00 [--grid WIDTHxHEIGHT |\n"
         "             --metadata INPUT.json | --like TEMPLATE.g00]\n"
         "  g00 extract INPUT.g00 OUTPUT_DIR\n\n"
         "decode writes OUTPUT.png.json unless --metadata specifies a path.\n"
         "encode defaults to one full-image cut; metadata is always explicit.\n"
         "extract writes numbered PNGs at each region's logical size.\n"
         "All encoding is lossless G00 type 2; existing files need --force.\n\n"
      << options << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    po::options_description options("Options");
    options.add_options()("help,h", "Show usage")(
        "force,f", "Replace existing output files")(
        "json", "Print info as versioned JSON metadata")(
        "metadata", po::value<std::string>(), "Region metadata JSON path")(
        "grid", po::value<std::string>(),
        "Sprite cell size, in row-major order")(
        "like", po::value<std::string>(),
        "Copy region metadata from an existing G00");
    po::options_description all;
    all.add(options).add_options()("command", po::value<std::string>(),
                                   "Command")("input", po::value<std::string>(),
                                              "Input")(
        "output", po::value<std::string>(), "Output");
    po::positional_options_description positional;
    positional.add("command", 1).add("input", 1).add("output", 1);
    po::variables_map args;
    po::store(po::command_line_parser(argc, argv)
                  .options(all)
                  .positional(positional)
                  .style(po::command_line_style::unix_style &
                         ~po::command_line_style::allow_guessing)
                  .run(),
              args);
    po::notify(args);
    if (args.count("help") || argc == 1) {
      Help(options);
      return 0;
    }
    if (!args.count("command") || !args.count("input"))
      throw std::runtime_error("A command and input are required; see --help");
    const auto command = args["command"].as<std::string>();
    if (command != "info" && command != "decode" && command != "encode" &&
        command != "extract")
      throw std::runtime_error("Unknown command: " + command);
    if ((command != "info") != static_cast<bool>(args.count("output")))
      throw std::runtime_error(command == "info"
                                   ? "info does not take an output path"
                                   : "An output path is required");
    for (const auto& name : {"grid", "like", "metadata", "json", "force"}) {
      const bool allowed =
          (std::string_view(name) == "json" && command == "info") ||
          (std::string_view(name) == "force" && command != "info") ||
          (std::string_view(name) == "metadata" &&
           (command == "encode" || command == "decode")) ||
          ((std::string_view(name) == "grid" ||
            std::string_view(name) == "like") &&
           command == "encode");
      if (args.count(name) && !allowed)
        throw std::runtime_error("Option --" + std::string(name) +
                                 " is not valid for " + command);
    }
    if (args.count("grid") + args.count("metadata") + args.count("like") > 1)
      throw std::runtime_error("Choose one of --grid, --metadata or --like");
    const fs::path input = args["input"].as<std::string>();
    const auto bytes = ReadFile(input);
    auto asset =
        command == "encode" ? g00::DecodePNG(bytes) : g00::DecodeG00(bytes);
    if (command == "info") {
      if (args.count("json")) {
        std::cout << g00::Metadata(asset);
      } else {
        std::cout << "G00 type "
                  << static_cast<int>(static_cast<unsigned char>(bytes[0]))
                  << "\nAtlas: " << asset.width << 'x' << asset.height
                  << "\nRegions: " << asset.regions.size() << '\n';
        for (std::size_t i = 0; i < asset.regions.size(); ++i) {
          const auto& region = asset.regions[i];
          std::cout << "  " << i << ": " << region.rect.width() << 'x'
                    << region.rect.height() << " at " << region.rect.x() << ','
                    << region.rect.y() << " origin " << region.originX << ','
                    << region.originY;
          if (region.pixel_region) {
            const auto& pixel = *region.pixel_region;
            std::cout << "; pixels " << pixel.rect.width() << 'x'
                      << pixel.rect.height() << " at " << pixel.rect.x() << ','
                      << pixel.rect.y() << " origin " << pixel.origin.x() << ','
                      << pixel.origin.y();
          }
          std::cout << '\n';
        }
      }
      return 0;
    }
    const fs::path output = args["output"].as<std::string>();
    std::vector<fs::path> inputs{input};
    std::vector<Output> outputs;
    if (command == "encode") {
      if (args.count("grid")) {
        const auto grid = args["grid"].as<std::string>();
        const auto separator = grid.find('x');
        if (separator == std::string::npos)
          throw std::runtime_error("Grid must have the form WIDTHxHEIGHT");
        asset.regions = g00::MakeGrid(
            asset.width, asset.height,
            Integer(std::string_view(grid).substr(0, separator)),
            Integer(std::string_view(grid).substr(separator + 1)));
      } else if (args.count("metadata") || args.count("like")) {
        const bool like = args.count("like");
        const fs::path source =
            args[like ? "like" : "metadata"].as<std::string>();
        // A template is fully read before writing, so --force may replace it.
        // PNG input and explicit JSON metadata remain protected from overwrite.
        if (!like)
          inputs.push_back(source);
        const auto metadata = ReadFile(source);
        g00::ApplyMetadata(
            asset, like ? g00::Metadata(g00::DecodeG00(metadata)) : metadata);
      }
      outputs.push_back({output, g00::EncodeG00(asset)});
    } else if (command == "decode") {
      const fs::path metadata_path =
          args.count("metadata") ? fs::path(args["metadata"].as<std::string>())
                                 : fs::path(output.string() + ".json");
      const auto metadata = g00::Metadata(asset);
      outputs.push_back({output, g00::EncodePNG(asset)});
      outputs.push_back({metadata_path, {metadata.begin(), metadata.end()}});
    } else {
      const auto count = std::max<std::size_t>(1, asset.regions.size());
      for (std::size_t i = 0; i < count; ++i) {
        std::ostringstream name;
        name << std::setw(4) << std::setfill('0') << i << ".png";
        outputs.push_back({output / name.str(),
                           g00::EncodePNG(g00::ExtractRegion(asset, i))});
      }
      // Extraction is the only command that creates its destination directory.
      fs::create_directories(output);
    }
    WriteFiles(outputs, inputs, args.count("force"));
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "g00: " << error.what() << '\n';
    return 1;
  }
}
