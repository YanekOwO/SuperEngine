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
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
// -----------------------------------------------------------------------

#include "machine/dumper.hpp"

#include "core/asset_scanner.hpp"
#include "core/avdec/audio_decoder.hpp"
#include "core/avdec/image_decoder.hpp"
#include "core/avdec/video_encoder.hpp"
#include "core/avdec/wav.hpp"
#include "libreallive/elements/bytecode.hpp"
#include "libreallive/elements/command.hpp"
#include "libreallive/scenario.hpp"
#include "libreallive/visitors.hpp"
#include "machine/module_manager.hpp"

#include <format>
#include <functional>

namespace fs = std::filesystem;
using namespace std::placeholders;

static void DumpImpl(libreallive::Scenario* scenario, std::ostream& out) {
  const auto& script = scenario->script;
  std::map<unsigned long, int> in_degree;
  std::map<unsigned long, std::string> lines;

  /* first pass – collect vertices and initialise in-degree */
  for (auto const& [loc, bytecode_ptr] : script.elements_)
    in_degree.emplace(loc, 0);

  /* second pass – produce textual representation & graph edges */
  for (auto const& [loc, bytecode_ptr] : script.elements_) {
    static auto prototype = ModuleManager::CreatePrototype();
    libreallive::DebugStringVisitor visitor(&prototype);

    auto ptr = bytecode_ptr->DownCast();
    lines.emplace(loc, std::visit(visitor, ptr));

    if (std::holds_alternative<libreallive::CommandElement const*>(ptr)) {
      auto cmd = std::get<libreallive::CommandElement const*>(ptr);
      for (size_t i = 0; i < cmd->GetLocationCount(); ++i)
        ++in_degree[cmd->GetLocation(i)];
    }
  }

  /* final pass – emit */
  for (auto const& [loc, txt] : lines) {
    if (in_degree[loc] != 0)
      out << ".L" << loc << '\n';
    out << txt << '\n';
  }
}

Dumper::Dumper(fs::path gexe_path, fs::path seen_path, fs::path root_path)
    : root_path_(std::move(root_path)),
      gexe_(Gameexe::FromFile(gexe_path).value()),
      regname_(gexe_("REGNAME").ToStr()),
      archive_(seen_path, regname_) {
  regname_ = archive_.regname_;  // ensure canonical name
}

std::vector<IDumper::Task> Dumper::GetTasks(std::vector<int> scenarios) {
  using tsk_t = typename IDumper::task_t;
  std::vector<IDumper::Task> tasks;

  for (int i = 0; i < 10'000; ++i) {
    libreallive::Scenario* sc = archive_.GetScenario(i);
    if (!sc)
      continue;

    tsk_t job(std::bind(DumpImpl, sc, _1));

    tasks.emplace_back(
        std::format("{}.{:04}.txt", regname_, sc->scene_number()),
        std::move(job));
  }

  AssetScanner scanner;
  scanner.IndexDirectory(root_path_);
  for (const auto& it : scanner.filesystem_cache_) {
    static const std::set<std::string> audio_ext{"nwa", "wav", "ogg", "mp3",
                                                 "ovk", "koe", "nwk"};
    static const std::set<std::string> image_ext{"g00", "pdt"};
    static const std::set<std::string> movie_ext{"omv"};
    auto name = it.first;
    auto [ext, path] = it.second;

    if (audio_ext.contains(ext)) {
      tasks.emplace_back(std::filesystem::path("audio") / (name + '.' + ext),
                         tsk_t(std::bind(&Dumper::DumpAudio, this, path, _1)));
    } else if (image_ext.contains(ext)) {
      tasks.emplace_back(std::filesystem::path("image") / (name + '.' + ext),
                         tsk_t(std::bind(&Dumper::DumpImage, this, path, _1)));
    } else if (movie_ext.contains(ext)) {
      tasks.emplace_back(std::filesystem::path("movie") / (name + ".webm"),
                         tsk_t(std::bind(&Dumper::DumpMovie, this, path, _1)));
    }
  }

  return tasks;
}

void Dumper::DumpAudio(std::filesystem::path path, std::ostream& out) {
  AudioDecoder decoder(path);
  AudioData data = decoder.DecodeAll();
  std::vector<uint8_t> wav = EncodeWav(std::move(data));
  out.write(reinterpret_cast<const char*>(wav.data()),
            static_cast<std::streamsize>(wav.size()));
}

void Dumper::DumpImage(std::filesystem::path path, std::ostream& out) {
  MappedFile mfile(path);
  ImageDecoder decoder(mfile.Read());
  saveBGRAasPPM(out, decoder.width, decoder.height, decoder.mem);
}

void Dumper::DumpMovie(std::filesystem::path path, std::ostream& out) {
  EncodeOmvAsWebm(path, out);
}
