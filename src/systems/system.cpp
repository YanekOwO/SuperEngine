// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
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

#include "systems/system.hpp"

#include "core/gameexe.hpp"
#include "core/mwnd_config.hpp"
#include "core/rlevent_listener.hpp"
#include "effects/fade_effect.hpp"
#include "libreallive/alldefs.hpp"
#include "machine/long_operation.hpp"
#include "machine/rlmachine.hpp"
#include "machine/serialization.hpp"
#include "modules/jump.hpp"
#include "modules/module_sys.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "systems/rlvm_info.hpp"
#include "systems/sdl/event_backend.hpp"
#include "systems/sdl/graphics_backend.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/sdl/sound_implementor.hpp"
#include "systems/sdl/text_implementor.hpp"
#include "systems/sound_system.hpp"
#include "systems/system_error.hpp"
#include "systems/text_system.hpp"
#include "utilities/exception.hpp"
#include "utilities/shutdown_signal.hpp"
#include "utilities/string_utilities.hpp"
#include "version.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using boost::replace_all;
using boost::to_lower;

namespace fs = std::filesystem;

namespace {

const std::vector<std::string> ALL_FILETYPES = {
    "g00", "pdt", "anm", "gan", "hik", "wav", "ogg", "nwa", "mp3",
    "ovk", "koe", "nwk", "wmv", "asf", "avi", "mpg", "mpeg"};

}  // namespace

class MenuReseter : public LongOperation {
 public:
  explicit MenuReseter(System& sys) : sys_(sys) {}

  bool operator()(RLMachine& machine) override {
    sys_.in_menu_ = false;
    return true;
  }

 private:
  System& sys_;
};

class QuitEventListener : public EventListener {
 public:
  explicit QuitEventListener(System& system) : system_(system) {}

  void OnEvent(std::shared_ptr<Event> event) override {
    if (std::holds_alternative<Quit>(*event))
      system_.RequestQuit();
  }

 private:
  System& system_;
};

// -----------------------------------------------------------------------
// SystemGlobals
// -----------------------------------------------------------------------

SystemGlobals::SystemGlobals()
    : confirm_save_load_(true), low_priority_(false) {}

// -----------------------------------------------------------------------
// System
// -----------------------------------------------------------------------

System::System(Gameexe& gameexe,
               std::shared_ptr<AssetScanner> scanner,
               SystemOptions options)
    : System(gameexe,
             std::move(scanner),
             MwndConfig::ParseReallive(gameexe),
             options) {}

System::System(Gameexe& gameexe,
               std::shared_ptr<AssetScanner> scanner,
               MwndConfig mwnd_config,
               SystemOptions options)
    : gameexe_(gameexe),
      in_menu_(false),
      force_fast_forward_(false),
      force_wait_(false),
      use_western_font_(false),
      assets_(scanner) {
  std::fill(syscom_status_, syscom_status_ + NUM_SYSCOM_ENTRIES,
            SYSCOM_VISIBLE);

  rlevent_handler_ = std::make_shared<RLEventListener>();

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::ostringstream ss;
    ss << "Video initialization failed: " << SDL_GetError();
    throw libreallive::Error(ss.str());
  }
  InstallShutdownSignalHandlers();

  auto graphics_backend = std::make_shared<SDLGraphicsBackend>();
  graphics_system_ =
      std::make_shared<GraphicsSystem>(*this, gameexe, graphics_backend);

  std::unique_ptr<IEventBackend> event_impl =
      std::make_unique<SDLEventBackend>();
  if (options.fast_forward)
    event_impl =
        std::make_unique<FastForwardEventBackend>(std::move(event_impl));
  event_system_ = std::make_shared<EventSystem>(std::move(event_impl));
  quit_listener_ = std::make_shared<QuitEventListener>(*this);
  event_system_->AddListener(100, quit_listener_);

  auto text_impl = std::make_unique<SDLTextImpl>();
  text_system_ = std::make_shared<TextSystem>(
      *this, gameexe, std::move(text_impl), std::move(mwnd_config));

  auto sound_impl = std::make_unique<SDLSoundImpl>();
  sound_system_ = std::make_shared<SoundSystem>(*this, std::move(sound_impl));

  event_system_->AddListener(graphics_system_);
  event_system_->AddListener(text_system_);

  event_system_->AddListener(19, rlevent_handler_);
}

System::~System() {
  sound_system_.reset();
  graphics_system_.reset();
  event_system_.reset();
  text_system_.reset();

  SDL_Quit();
}

void System::TakeSelectionSnapshot(RLMachine& machine) {
  previous_selection_ = std::make_shared<std::stringstream>();
  Serialization::saveGameTo(*previous_selection_, machine);
}

void System::RestoreSelectionSnapshot(RLMachine& machine) {
  // We need to reference this on the stack because it will call
  // System::reset() to get the black screen.
  std::shared_ptr<std::stringstream> s = previous_selection_;
  if (s) {
    std::shared_ptr<SDLSurface> before =
        machine.GetSystem().graphics().RenderToSurface();
    Size screen_size = before->GetSize();
    std::shared_ptr<SDLSurface> black_screen =
        std::make_shared<SDLSurface>(screen_size);
    black_screen->Fill(RGBAColour::Black());

    Serialization::loadGameFrom(*s, machine);
    std::shared_ptr<SDLSurface> after =
        machine.GetSystem().graphics().RenderToSurface();

    constexpr int duration = 250;
    machine.PushLongOperation(std::make_shared<FadeEffect>(
        machine, after, black_screen, screen_size, duration));
    machine.PushLongOperation(std::make_shared<FadeEffect>(
        machine, black_screen, before, screen_size, duration));
  }
}

int System::IsSyscomEnabled(int syscom) {
  CheckSyscomIndex(syscom, "System::is_syscom_enabled");

  // Special cases where state of the interpreter would override the
  // programmatically set (or user set) values.
  if (syscom == SYSCOM_SET_SKIP_MODE && !text().kidoku_read()) {
    // Skip mode should be grayed out when there's no text to read
    if (syscom_status_[syscom] == SYSCOM_VISIBLE)
      return SYSCOM_GREYED_OUT;
  } else if (syscom == SYSCOM_RETURN_TO_PREVIOUS_SELECTION) {
    if (syscom_status_[syscom] == SYSCOM_VISIBLE)
      return previous_selection_.get() ? SYSCOM_VISIBLE : SYSCOM_GREYED_OUT;
  }

  return syscom_status_[syscom];
}

void System::HideSyscom() {
  std::fill(syscom_status_, syscom_status_ + NUM_SYSCOM_ENTRIES,
            SYSCOM_INVISIBLE);
}

void System::HideSyscomEntry(int syscom) {
  CheckSyscomIndex(syscom, "System::hide_system");
  syscom_status_[syscom] = SYSCOM_INVISIBLE;
}

void System::EnableSyscom() {
  std::fill(syscom_status_, syscom_status_ + NUM_SYSCOM_ENTRIES,
            SYSCOM_VISIBLE);
}

void System::EnableSyscomEntry(int syscom) {
  CheckSyscomIndex(syscom, "System::enable_system");
  syscom_status_[syscom] = SYSCOM_VISIBLE;
}

void System::DisableSyscom() {
  std::fill(syscom_status_, syscom_status_ + NUM_SYSCOM_ENTRIES,
            SYSCOM_GREYED_OUT);
}

void System::DisableSyscomEntry(int syscom) {
  CheckSyscomIndex(syscom, "System::disable_system");
  syscom_status_[syscom] = SYSCOM_GREYED_OUT;
}

int System::ReadSyscom(int syscom) {
  throw rlvm::Exception("ReadSyscom unimplemented!");
}

void System::ShowSyscomMenu(RLMachine& machine) {
  Gameexe& gexe = machine.GetSystem().gameexe();

  if (gexe("CANCELCALL_MOD").Int() == 1) {
    if (!in_menu_) {
      // Multiple right clicks shouldn't spawn multiple copies of the menu
      // system on top of each other.
      in_menu_ = true;
      machine.PushLongOperation(std::make_shared<MenuReseter>(*this));

      const std::vector<int> cancelcall = gexe("CANCELCALL").ToIntVec();
      Farcall(machine, cancelcall.at(0), cancelcall.at(1));
    }
  } else {
    std::cerr << "(We don't deal with non-custom SYSCOM calls yet.)"
              << std::endl;
  }
}

void System::InvokeSyscom(RLMachine& machine, int syscom) {
  switch (syscom) {
    case SYSCOM_SAVE:
      InvokeSaveOrLoad(machine, syscom, "SYSTEMCALL_SAVE_MOD",
                       "SYSTEMCALL_SAVE");
      break;
    case SYSCOM_LOAD:
      InvokeSaveOrLoad(machine, syscom, "SYSTEMCALL_LOAD_MOD",
                       "SYSTEMCALL_LOAD");
      break;
    case SYSCOM_MESSAGE_SPEED:
    case SYSCOM_WINDOW_ATTRIBUTES:
    case SYSCOM_VOLUME_SETTINGS:
    case SYSCOM_MISCELLANEOUS_SETTINGS:
    case SYSCOM_VOICE_SETTINGS:
    case SYSCOM_FONT_SELECTION:
    case SYSCOM_BGM_FADE:
    case SYSCOM_BGM_SETTINGS:
    case SYSCOM_AUTO_MODE_SETTINGS:
    case SYSCOM_USE_KOE:
    case SYSCOM_DISPLAY_VERSION: {
      break;
    }
    case SYSCOM_RETURN_TO_PREVIOUS_SELECTION:
      RestoreSelectionSnapshot(machine);
      break;
    case SYSCOM_SHOW_WEATHER:
      graphics().set_should_show_weather(!graphics().should_show_weather());
      break;
    case SYSCOM_SHOW_OBJECT_1:
      graphics().set_should_show_object1(!graphics().should_show_object1());
      break;
    case SYSCOM_SHOW_OBJECT_2:
      graphics().set_should_show_object2(!graphics().should_show_object2());
      break;
    case SYSCOM_CLASSIFY_TEXT:
      std::cerr << "We have no idea what classifying text even means!"
                << std::endl;
      break;
    case SYSCOM_OPEN_MANUAL_PATH:
      std::cerr << "Opening manual path..." << std::endl;
      break;
    case SYSCOM_SET_SKIP_MODE:
      text().SetSkipMode(!text().skip_mode());
      break;
    case SYSCOM_AUTO_MODE:
      text().SetAutoMode(!text().auto_mode());
      break;
    case SYSCOM_MENU_RETURN:
      // This is a hack since we probably have a bunch of crap on the stack.
      ClearLongOperationsOffBackOfStack(machine);

      // Simulate a MenuReturn.
      Sys_MenuReturn()(machine);
      break;
    case SYSCOM_EXIT_GAME:
      machine.Halt();
      break;
    case SYSCOM_SHOW_BACKGROUND:
      graphics().ToggleInterfaceHidden();
      break;
    case SYSCOM_HIDE_MENU:
      // Do nothing. The menu will be hidden on its own.
      break;
    case SYSCOM_GENERIC_1:
    case SYSCOM_GENERIC_2:
    case SYSCOM_SCREEN_MODE:
    case SYSCOM_WINDOW_DECORATION_STYLE:
      std::cerr << "No idea what to do!" << std::endl;
      break;
  }
}

void System::Reset() {
  in_menu_ = false;
  previous_selection_.reset();

  EnableSyscom();

  sound().Reset();
  graphics().Reset();
  text().Reset();
}

std::string System::Regname() {
  Gameexe& gexe = gameexe();
  std::string regname = gexe("REGNAME").Str().value_or("");
  replace_all(regname, "\\", "_");

  // Note that we assume the Gameexe file is written in Shift-JIS. I don't
  // think you can write it in anything else.
  return cp932toUTF8(regname, 0);
}

std::filesystem::path System::GameSaveDirectory() {
  fs::path base_dir = GetHomeDirectory() / ".rlvm" / Regname();
  fs::create_directories(base_dir);

  return base_dir;
}

bool System::ShouldFastForward() {
  return (rlEvent().CtrlPressed() && text().ctrl_key_skip()) ||
         text().CurrentlySkipping() || force_fast_forward_;
}

void System::Run(std::function<void()> after_event_poll) {
  event_system_->ExecuteEventSystem();
  if (after_event_poll)
    after_event_poll();
  text_system_->ExecuteTextSystem();
  sound_system_->ExecuteSoundSystem();
  graphics_system_->ExecuteGraphicsSystem();
}

SoundSystem& System::sound() { return *sound_system_; }

std::filesystem::path System::GetHomeDirectory() {
  std::string drive, home;
  char* homeptr = getenv("HOME");
  char* driveptr = getenv("HOMEDRIVE");
  char* homepathptr = getenv("HOMEPATH");
  char* profileptr = getenv("USERPROFILE");
  if (homeptr != 0 && (home = homeptr) != "") {
    // UN*X like home directory
    return fs::path(home);
  } else if (driveptr != 0 && homepathptr != 0 && (drive = driveptr) != "" &&
             (home = homepathptr) != "") {
    // Windows.
    return fs::path(drive) / fs::path(home);
  } else if (profileptr != 0 && (home = profileptr) != "") {
    // Windows?
    return fs::path(home);
  } else {
    throw SystemError("Could not find location of home directory.");
  }
}

void System::InvokeSaveOrLoad(RLMachine& machine,
                              int syscom,
                              const std::string& mod_key,
                              const std::string& location) {
  auto save_mod = gameexe()(mod_key).Int();
  auto save_loc = gameexe()(location).IntVec();

  if (save_mod && save_loc && save_mod == 1) {
    int scenario = save_loc->at(0);
    int entrypoint = save_loc->at(1);

    text().set_system_visible(false);
    machine.PushLongOperation(std::make_shared<RestoreTextSystemVisibility>());
    Farcall(machine, scenario, entrypoint);
  }
}

void System::CheckSyscomIndex(int index, const char* function) {
  if (index < 0 || index >= NUM_SYSCOM_ENTRIES) {
    std::ostringstream oss;
    oss << "Illegal syscom index #" << index << " in " << function;
    throw std::runtime_error(oss.str());
  }
}
