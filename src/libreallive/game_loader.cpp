// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
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

#include "libreallive/game_loader.hpp"

#include "core/event_listener.hpp"
#include "core/gameexe.hpp"
#include "libreallive/archive.hpp"
#include "libreallive/scriptor.hpp"
#include "log/domain_logger.hpp"
#include "machine/game_hacks.hpp"
#include "machine/long_operation.hpp"
#include "machine/rlmachine.hpp"
#include "machine/serialization.hpp"
#include "systems/graphics_system.hpp"
#include "systems/event_system.hpp"
#include "systems/system.hpp"
#include "utilities/file.hpp"

#include <stdexcept>

namespace libreallive {

namespace fs = std::filesystem;

namespace {
fs::path FindGameFile(const std::filesystem::path& gamerootPath,
                      const std::string& filename) {
  fs::path search_for = gamerootPath / filename;
  fs::path corrected_path = CorrectPathCase(search_for);
  if (corrected_path.empty()) {
    throw std::runtime_error("Could not open " + search_for.string());
  }

  return corrected_path;
}

// AVG32 file checks. We can't run AVG32 games.
static const std::vector<std::string> avg32_exes{"avg3216m.exe",
                                                 "avg3217m.exe"};

// Siglus engine filenames. We can't run VisualArts' newer engine.
static const std::vector<std::string> siglus_exes{
    "siglus.exe", "siglusengine-ch.exe", "siglusengine.exe",
    "siglusenginechs.exe"};

void CheckBadEngine(const std::filesystem::path& gamerootPath,
                    const std::vector<std::string> filenames,
                    const std::string& message_text) {
  for (const auto& cur_file : filenames) {
    if (fs::exists(CorrectPathCase(gamerootPath / cur_file))) {
      throw std::runtime_error(message_text);
    }
  }
}

}  // namespace

GameLoader::GameLoader(fs::path gameroot,
                       std::optional<int> start_scene,
                       bool fast_forward) {
  fs::path gameexePath = FindGameFile(gameroot, "Gameexe.ini");
  fs::path seenPath = FindGameFile(gameroot, "Seen.txt");

  // Check for VisualArt's older and newer engines, which we can't emulate:
  CheckBadEngine(gameroot, avg32_exes, "Can't run AVG32 games");
  CheckBadEngine(gameroot, siglus_exes, "Can't run Siglus games");

  gameexe_ = std::make_shared<Gameexe>();
  gameexe_->LoadFromFile(gameexePath);
  (*gameexe_)("__GAMEPATH") = gameroot.string();

  archive_ = std::make_shared<libreallive::Archive>(
      seenPath.string(), (*gameexe_)("REGNAME").Str().value_or(""));

  SystemOptions system_options;
  system_options.fast_forward = fast_forward;
  system_ = std::make_shared<System>(
      *gameexe_,
      std::make_shared<AssetScanner>(AssetScanner::BuildFromGameexe(*gameexe_)),
      system_options);

  // Instantiate the rl machine
  auto memory = std::make_unique<Memory>();
  memory->LoadFrom(*gameexe_);
  int first_seen =
      (*gameexe_)("SEEN_START").Int().value_or(archive_->GetFirstScenarioID());
  if (start_scene.has_value())
    first_seen = *start_scene;

  auto scriptor = std::make_shared<Scriptor>(*archive_);
  ScenarioConfig default_config;
  auto savepoint_decide = [&](std::string key) -> bool {
    switch ((*gameexe_)(key).Int().value_or(-1)) {
      case 0:
        return false;
      case 1:
        [[fallthrough]];
      default:
        return true;
    }
  };
  default_config.enable_message_savepoint =
      savepoint_decide("SAVEPOINT_MESSAGE");
  default_config.enable_seentop_savepoint =
      savepoint_decide("SAVEPOINT_SEENTOP");
  default_config.enable_selcom_savepoint = savepoint_decide("SAVEPOINT_SELCOM");
  scriptor->SetDefaultScenarioConfig(std::move(default_config));

  // instantiate virtual machine
  machine_ = std::make_shared<RLMachine>(system_, scriptor, std::move(memory));
  system_->machine_ = machine_.get();
  machine_->GetCallStack().Push(
      StackFrame(scriptor->Load(first_seen), StackFrame::TYPE_ROOT));

  // Event listener for long operations
  struct LongopListenerAdapter
      : public EventListener {  // Adaptor class that fetches event and feed
                                // them to the topmost long
                                // operation, if there is any.
    RLMachine& machine_;
    LongopListenerAdapter(RLMachine& machine) : machine_(machine) {}
    void OnEvent(std::shared_ptr<Event> event) override {
      if (auto sp = machine_.CurrentLongOperation())
        sp->OnEvent(event);
    }
  };
  longop_listener_adapter_ = std::make_shared<LongopListenerAdapter>(*machine_);
  system_->event().AddListener(-20 /*we want the lowest priority*/,
                               longop_listener_adapter_);

  // Event listener for system
  struct SystemEventListener : public EventListener {
    RLMachine& machine_;
    SystemEventListener(RLMachine& machine) : machine_(machine) {}
    void OnEvent(std::shared_ptr<Event> event) override {
      if (std::visit(
              [&](auto& event) -> bool {
                using T = std::decay_t<decltype(event)>;
                if constexpr (std::same_as<T, Quit>) {
                  machine_.Halt();
                  return true;
                }
                if constexpr (std::same_as<T, VideoExpose>) {
                  machine_.GetSystem().graphics().ForceRefresh();
                  return true;
                }
                if constexpr (std::same_as<T, VideoResize>) {
                  machine_.GetSystem().graphics().Resize(event.size);
                  return true;
                }
                if constexpr (std::same_as<T, MouseMotion>) {
                  event.pos =
                      machine_.GetSystem().graphics().DisplayToScreenPoint(
                          event.pos);
                  return false;
                }
                return false;
              },
              *event))
        *event = std::monostate();
    }
  };
  system_listener_ = std::make_shared<SystemEventListener>(*machine_);
  system_->event().AddListener(20 /*we want the highest priority*/,
                               system_listener_);

  // Load the "DLLs" required
  for (auto it : gameexe_->Filter("DLL.")) {
    const std::string& name = it.Str().value_or("");
    try {
      std::string index_str = it.key().substr(it.key().find_first_of(".") + 1);
      int index = std::stoi(index_str);
      machine_->LoadDLL(index, name);
    } catch (std::exception& e) {
      static DomainLogger logger("GameLoader");
      logger(Severity::Warn)
          << "Don't know what to do with DLL '" << name << "'";
    }
  }
  AddGameHacks(*machine_);

  Serialization::loadGlobalMemory(*machine_);
}

}  // namespace libreallive
