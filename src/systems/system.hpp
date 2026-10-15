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

#pragma once

#include "core/asset_scanner.hpp"
#include "core/game_config.hpp"

#include <filesystem>
#include <functional>

#include <memory>
#include <sstream>
#include <string>

class GraphicsSystem;
class EventSystem;
class TextSystem;
class Stage;
class SoundSystem;
class RLMachine;
class Gameexe;
class GameexeInterpretObject;
class EventListener;
class RLEventListener;
class MwndConfig;

struct SystemOptions {
  bool fast_forward = false;
};

// Syscom Constants
//
// Associations between syscom integer values and their names.
[[maybe_unused]] constexpr int NUM_SYSCOM_ENTRIES = 32;
[[maybe_unused]] constexpr int SYSCOM_INVISIBLE = 0;
[[maybe_unused]] constexpr int SYSCOM_VISIBLE = 1;
[[maybe_unused]] constexpr int SYSCOM_GREYED_OUT = 2;

[[maybe_unused]] constexpr int SYSCOM_SAVE = 0;
[[maybe_unused]] constexpr int SYSCOM_LOAD = 1;
[[maybe_unused]] constexpr int SYSCOM_MESSAGE_SPEED = 2;
[[maybe_unused]] constexpr int SYSCOM_WINDOW_ATTRIBUTES = 3;
[[maybe_unused]] constexpr int SYSCOM_VOLUME_SETTINGS = 4;
[[maybe_unused]] constexpr int SYSCOM_SCREEN_MODE = 5;
[[maybe_unused]] constexpr int SYSCOM_MISCELLANEOUS_SETTINGS = 6;
// No 7?
[[maybe_unused]] constexpr int SYSCOM_VOICE_SETTINGS = 8;
[[maybe_unused]] constexpr int SYSCOM_FONT_SELECTION = 9;
[[maybe_unused]] constexpr int SYSCOM_BGM_FADE = 10;
[[maybe_unused]] constexpr int SYSCOM_BGM_SETTINGS = 11;
[[maybe_unused]] constexpr int SYSCOM_WINDOW_DECORATION_STYLE = 12;
[[maybe_unused]] constexpr int SYSCOM_AUTO_MODE_SETTINGS = 13;
[[maybe_unused]] constexpr int SYSCOM_RETURN_TO_PREVIOUS_SELECTION = 14;
[[maybe_unused]] constexpr int SYSCOM_USE_KOE = 15;
[[maybe_unused]] constexpr int SYSCOM_DISPLAY_VERSION = 16;
[[maybe_unused]] constexpr int SYSCOM_SHOW_WEATHER = 17;
[[maybe_unused]] constexpr int SYSCOM_SHOW_OBJECT_1 = 18;
[[maybe_unused]] constexpr int SYSCOM_SHOW_OBJECT_2 = 19;
[[maybe_unused]] constexpr int SYSCOM_CLASSIFY_TEXT =
    20;  // ??????? Unknown function.
[[maybe_unused]] constexpr int SYSCOM_GENERIC_1 = 21;
[[maybe_unused]] constexpr int SYSCOM_GENERIC_2 = 22;
// No 23?
[[maybe_unused]] constexpr int SYSCOM_OPEN_MANUAL_PATH = 24;
[[maybe_unused]] constexpr int SYSCOM_SET_SKIP_MODE = 25;
[[maybe_unused]] constexpr int SYSCOM_AUTO_MODE = 26;
// No 27?
[[maybe_unused]] constexpr int SYSCOM_MENU_RETURN = 28;
[[maybe_unused]] constexpr int SYSCOM_EXIT_GAME = 29;
[[maybe_unused]] constexpr int SYSCOM_HIDE_MENU = 30;
[[maybe_unused]] constexpr int SYSCOM_SHOW_BACKGROUND = 31;

// The system class provides the SDL-backed components that make up a local
// runtime system, i.e., sound, graphics, filesystem etc.
class System {
 public:
  System(Gameexe& gameexe,
         Stage& stage,
         LocalConfig& local_config,
         GlobalConfig& global_config,
         std::shared_ptr<AssetScanner> scanner = nullptr,
         SystemOptions options = {});
  System(Gameexe& gameexe,
         Stage& stage,
         LocalConfig& local_config,
         GlobalConfig& global_config,
         std::shared_ptr<AssetScanner> scanner,
         MwndConfig mwnd_config,
         SystemOptions options = {});
  ~System();

  // Whether we're currently forcing fast forward (only used during game tests
  // to zoom through).
  inline bool force_fast_forward() { return force_fast_forward_; }
  // Set in lua_rlvm, to speed through the game with maximum speed!
  inline void set_force_fast_forward() { force_fast_forward_ = true; }

  inline bool force_wait() { return force_wait_; }
  inline void set_force_wait(bool in) { force_wait_ = in; }

  inline bool IsQuitRequested() const { return quit_requested_; }
  inline void RequestQuit() { quit_requested_ = true; }

  // We record what the text encoding response was during the first scene, and
  // then during every scene change, if it was western, we flip this bit to
  // true. We do this as a big hack because we only have System access while
  // we're loading fonts.
  inline bool use_western_font() { return use_western_font_; }
  inline void set_use_western_font() { use_western_font_ = true; }

  // Takes and restores the previous selection snapshot; a special emphemeral
  // save game slot that autosaves on selections and is restored through a
  // special kepago method/syscom call.
  void TakeSelectionSnapshot(RLMachine& machine);
  void RestoreSelectionSnapshot(RLMachine& machine);

  // Syscom related functions
  //
  // RealLive provides a context menu system to handle most actions
  // and configuration settings. The system command menu is configured
  // with the \#SYSCOM variables in gameexe.ini. It can be disabled by
  // setting \#SYSCOM_USE to 0, and if a \#CANCELCALL hook is defined it
  // will never be used at all (Clannad does this, although it uses
  // the internal flags associated with the system command menu to
  // control its own menu system).
  //
  // These functions are used to manipulate the visibility, change the
  // values of, and invoke standard dialogs for various SYSCOM elements.

  // Checks the visibility of a single syscom command. Returns 0 if the given
  // system command is invisible, 1 if it is visible, and 2 if it is visible
  // but disabled (greyed out).
  int IsSyscomEnabled(int syscom);

  // Hides all syscom entries
  void HideSyscom();

  // Hides the syscom entry |syscom|
  void HideSyscomEntry(int syscom);

  // Enables all syscom entries
  void EnableSyscom();

  // Enables the syscom entry |syscom|
  void EnableSyscomEntry(int syscom);

  // Disables all syscom entries
  void DisableSyscom();

  // Disables the syscom entry |syscom|
  void DisableSyscomEntry(int syscom);

  // Reads the corresponding value for syscom number |syscom|
  int ReadSyscom(int syscom);

  // Called by various LongOperations to show the right click menu.
  void ShowSyscomMenu(RLMachine& machine);

  // If there is a standard dialog box associated with syscom, it is
  // displayed; if there is a standard action, it is performed. The list of
  // menu commands in section 4.5 has details of which menu commands have
  // standard dialogs. The optional value is used for the setting where
  // relevant (for example, InvokeSyscom(5, val) is exactly equivalent to
  // SetScreenMode(val)).
  void InvokeSyscom(RLMachine& machine, int syscom);

  // Resets the present values of the system; this doesn't clear user settings,
  // but clears things like the current graphics state and the status of all
  // the text windows. This method is called when the user loads a game or
  // resets the machine. The System implementation of reset() will call
  // reset() on all systems.
  void Reset();
  void ApplyConfiguration();

  inline LocalConfig& local_config() { return local_config_; }
  inline const LocalConfig& local_config() const { return local_config_; }
  inline GlobalConfig& global_config() { return global_config_; }
  inline const GlobalConfig& global_config() const { return global_config_; }

  // Cleans the regname entry from the gameexe and makes it filesystem
  // safe. This translates it to UTF-8, as Gameexe files are written in
  // Shift-JIS.
  std::string Regname();

  // Returns a std::filesystem object which points to the directory
  // where saved game data, preferences, etc should be stored
  // for this game.
  //
  // The default implementation returns "~/.rlvm/#{REGNAME}/". A Mac
  // specific override could return "~/Library/Application
  // Support/rlvm/#{REGNAME}/"
  std::filesystem::path GameSaveDirectory();

  // Testing and Debugging Tools

  // Whether we are zooming through text and events quickly. Currently can be
  // triggered by holding down the control key, or using skip previously read
  // text.
  bool ShouldFastForward();

  // Called once per gameloop. |after_event_poll| runs after input dispatch and
  // before the remaining subsystems advance.
  void Run(std::function<void()> after_event_poll = {});

  inline RLEventListener& rlEvent() { return *rlevent_handler_; }
  inline std::shared_ptr<AssetScanner> GetAssetScanner() { return assets_; }
  inline GraphicsSystem& graphics() { return *graphics_system_; }
  inline EventSystem& event() { return *event_system_; }
  inline Gameexe& gameexe() { return gameexe_; }
  inline TextSystem& text() { return *text_system_; }
  SoundSystem& sound();
  inline std::shared_ptr<GraphicsSystem> graphics_ptr() {
    return graphics_system_;
  }
  inline std::shared_ptr<EventSystem> event_ptr() { return event_system_; }

  RLMachine* machine_ = nullptr;

 private:
  std::shared_ptr<RLEventListener> rlevent_handler_;
  std::shared_ptr<EventListener> quit_listener_;

  std::filesystem::path GetHomeDirectory();

  // Invokes a custom dialog or the standard one if none present.
  void InvokeSaveOrLoad(RLMachine& machine,
                        int syscom,
                        const std::string& mod_key,
                        const std::string& location);

  // Verify that |index| is valid and throw if it isn't.
  void CheckSyscomIndex(int index, const char* function);

  Gameexe& gameexe_;
  std::shared_ptr<GraphicsSystem> graphics_system_;
  std::shared_ptr<EventSystem> event_system_;
  std::shared_ptr<TextSystem> text_system_;
  std::shared_ptr<SoundSystem> sound_system_;

  // Whether the SYSCOM menu is currently being displayed.
  bool in_menu_;

  // Whether we are being forced to fast forward through the game for testing
  // reasons.
  bool force_fast_forward_;

  // Forces a 10ms sleep at the end of the System::run function. Used to lower
  // CPU usage during manual redrawing.
  bool force_wait_;

  bool quit_requested_ = false;

  // Whether we should be trying to find a western font.
  bool use_western_font_;

  // Object to index and find rlvm related files
  std::shared_ptr<AssetScanner> assets_;

  LocalConfig& local_config_;
  GlobalConfig& global_config_;

  // A stream with the save game data at the time of the last selection. Used
  // for the Return to Previous Selection feature.
  std::shared_ptr<std::stringstream> previous_selection_;

  // Implementation detail which resets in_menu_;
  friend class MenuReseter;
};
