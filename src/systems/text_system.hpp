// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
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
//
// -----------------------------------------------------------------------

#pragma once

#include "core/event_listener.hpp"
#include "core/game_config.hpp"
#include "core/mwnd_config.hpp"

#include <boost/serialization/split_member.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>

#include <filesystem>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Gameexe;
class MemoryHandle;
class Point;
class RGBColour;
class RLMachine;
class Size;
class SDLSurface;
class System;
class Stage;
class TextKeyCursor;
class TextPage;
class TextWindow;
class TextWaku;
class GraphicsObject;
class ITextSystem;
class IFont;

class TextSystem final : public EventListener {
 public:
  enum class MwndButtonSound { Hit, Push, Decide };

  struct MwndActionStatus {
    bool supported = false;
    bool exists = true;
    bool enabled = true;
    int mode = 0;
  };

  struct MwndButtonPresentation {
    bool visible = true;
    bool enabled = true;
    bool touch_enabled = true;
    bool activated = false;
  };

  // Owning foreground page snapshots used for message history.
  using PageSet = std::map<int, TextPage>;

 public:
  TextSystem(System& system,
             Stage& stage,
             Gameexe& gexe,
             std::unique_ptr<ITextSystem> impl,
             MwndConfig mwnd_config);
  virtual ~TextSystem();

  // Controls whether the text system is rendered at all.
  bool system_visible() const { return system_visible_; }
  void set_system_visible(bool in) { system_visible_ = in; }

  void ExecuteTextSystem();

  void Render();
  void RenderWindow(TextWindow& window);
  void HideTextWindow(int win_number);
  void HideAllTextWindows();
  void HideAllTextWindowsExcept(int i);
  void ShowTextWindow(int win_number);
  void ShowAllTextWindows();
  void ClearAllTextWindows();
  void SetVisualOverride(int win_number, bool show_window);
  void SetVisualOverrideAll(bool show_window);
  void ClearVisualOverrides();
  bool ShowWindow(int win_num) const;

  // Text window, aka mwnd. Consists of waku decoration and TextPage layout
  // management
  std::shared_ptr<TextWindow> GetMwnd(int text_window_number);
  std::shared_ptr<TextWindow> GetMwnd(int layer, int text_window_number);
  std::shared_ptr<TextWindow> FindMwnd(int layer, int index) const;
  bool IsForegroundWindow(const TextWindow* window) const;
  std::shared_ptr<TextWindow> GetMwnd(
      int num,
      std::function<std::shared_ptr<TextWindow>()> orelse);
  bool HasCurrentWindow() const;
  std::shared_ptr<TextWindow> GetCurrentMwnd();

  const MwndConfig& mwnd_config() const { return mwnd_config_; }
  using MwndCallHandler =
      std::function<void(const MwndConfig::CallTarget& target)>;
  using MwndActionHandler =
      std::function<bool(const MwndConfig::Button& button, bool execute)>;
  using MwndActionStatusHandler =
      std::function<MwndActionStatus(const MwndConfig::Button& button)>;
  using MwndFrameActionHandler =
      std::function<void(std::shared_ptr<GraphicsObject>,
                         const MwndConfig::CallTarget&)>;
  void SetMwndFrameActionHandler(MwndFrameActionHandler handler) {
    mwnd_frame_action_handler_ = std::move(handler);
  }
  void InitializeMwndButtonFrameActions(TextWaku& waku);
  void SetMwndCallHandler(MwndCallHandler handler) {
    mwnd_call_handler_ = std::move(handler);
  }
  void SetMwndActionHandler(MwndActionHandler handler) {
    mwnd_action_handler_ = std::move(handler);
  }
  void SetMwndActionStatusHandler(MwndActionStatusHandler handler) {
    mwnd_action_status_handler_ = std::move(handler);
  }
  bool IsMwndButtonEnabled(const MwndConfig::Button& button) const;
  MwndButtonPresentation GetMwndButtonPresentation(
      const MwndConfig::Button& button,
      bool has_script_call = false) const;
  void ExecuteMwndButton(const MwndConfig::Button& button);
  void PlayMwndButtonSound(const MwndConfig::Button& button,
                           MwndButtonSound event);

  void set_in_pause_state(bool in) { in_pause_state_ = in; }

  int active_window() const { return active_window_; }
  void set_active_window(int window) { active_window_ = window; }

  std::vector<int> GetActiveWindows();

  // Take a snapshot of the current window state, with their
  // respective TextPages, and add it to the backlog.
  void Snapshot();

  // Resets the text page in the current_set
  void NewPageOnWindow(int window);
  void NewPageOnWindow(std::shared_ptr<TextWindow> window);

  TextPage& GetPageOnWindow(std::shared_ptr<TextWindow> window);

  // Get the active foreground window's live page.
  TextPage& GetCurrentPage();

  // Clears the screen, moves back one page and renders it.
  void BackPage();
  void ForwardPage();

  void ReplayPageSet(PageSet& set, bool is_current_page);

  bool IsReadingBacklog() const;
  void StopReadingBacklog();

  // Performs #NAMAE replacement; used in the English Edition of Clannad.
  std::string InterpretName(const std::string& utf8name);

  void SetKeyCursor(int new_cursor);

  // Returns the key cursor index. (or -1 if no key cursor).
  int GetCursorNumber() const;

  // Update the mouse cursor.
  void SetMousePosition(const Point& pos);
  bool HandleMouseClick(const Point& pos, bool pressed);

  // Save pieces of state that would be saved to disk.
  void TakeSavepointSnapshot();

  // Returns a surface with |utf8str| rendered with the other specified
  // properties. Will search |utf8str| for object text syntax and will change
  // various properties based on that syntax.
  std::shared_ptr<SDLSurface> RenderText(const std::string& utf8str,
                                         int size,
                                         int xspace,
                                         int yspace,
                                         const RGBColour& colour,
                                         RGBColour* shadow_colour,
                                         int max_chars_in_line);

  // Resets non-configuration values (so we can load games).
  virtual void Reset();
  void ApplyConfiguration();

  bool kidoku_read() const { return kidoku_read_; }
  void SetKidokuRead(const int in);

  bool skip_mode() const { return skip_mode_; }
  void SetSkipMode(int in);
  bool auto_skip_mode() const { return auto_skip_mode_; }
  void SetAutoSkipMode(bool enabled) { auto_skip_mode_ = enabled; }

  bool CurrentlySkipping() const;

  void set_in_selection_mode(const bool in) { in_selection_mode_ = in; }

  // Overridden from EventListener
  virtual void OnEvent(std::shared_ptr<Event> event) override;

  System& system() { return system_; }

  ITextSystem& impl() { return *text_impl_; }

  // Resolve the active family or filename and load it at the given pixel size
  // with the current bold setting. Reuse cached fonts with matching file, size,
  // style and collection face; throw if resolution or loading fails.
  std::shared_ptr<IFont> LoadFont(int size);

 private:
  void UpdateWindowsForChangeToWindowAttr();

  // Reduces the number of page snapshots in previous_page_sets_ down to a
  // manageable constant number.
  void ExpireOldPages();

  // Platform dependent text implementor
  std::unique_ptr<ITextSystem> text_impl_;

  // TextPage will call our internals since it actually does most of
  // the work while we hold state.
  friend class TextPage;

  LocalConfig& local_config_;
  GlobalConfig& global_config_;

  // Sets which window is the current active window.
  int active_window_;

 public:
  // The stage owns the active text windows.
  Stage& stage_;
  MwndConfig mwnd_config_;
  MwndCallHandler mwnd_call_handler_;
  MwndActionHandler mwnd_action_handler_;
  MwndActionStatusHandler mwnd_action_status_handler_;
  MwndFrameActionHandler mwnd_frame_action_handler_;
  std::optional<std::weak_ptr<TextWindow>> mwnd_capture_window_;

 private:
  // Whether we are reading the backlog
  bool is_reading_backlog_;

  // The current page set. Represents what is on the screen right now.
  void CollectCurrentPages();
  PageSet current_pageset_;

  // Name mappings. Clannad English Edition maps name boxes from the key to the
  // value.
  std::map<std::string, std::string> namae_mapping_;

  // Previous Text Pages. The TextSystem owns the list of previous
  // pages because multiple windows can be displayed in one text page.
  std::list<PageSet> previous_page_sets_;

  // When previous_page_it_ == previous_pages_.end(), active_page_ is
  // currently being rendered to the screen. When it is any valid
  // iterator pointing into previous_pages_, that is the current page
  // being rendered.
  std::list<PageSet>::iterator previous_page_it_;

  // Whether we are in a state where the interpreter is pause()d.
  bool in_pause_state_;

  std::shared_ptr<TextKeyCursor> text_key_cursor_;

  bool system_visible_;

  // Whether we skip text that we've already seen
  bool skip_mode_;
  // Automatically start skipping eligible text until interrupted by input or
  // unread text. The preference survives those interruptions.
  bool auto_skip_mode_ = false;
  bool auto_skip_suspended_ = false;

  // Whether we are currently on a page of text that we've previously read.
  bool kidoku_read_;

  // Whether we are currently paused at a user choice.
  bool in_selection_mode_;

  // Contains overrides for showing or hiding the text windows.
  std::map<int, bool> window_visual_override_;

  // Our parent system object.
  System& system_;

  // This state can change after the last savepoint marker. These are the
  // values that should be saved to disk.
  int savepoint_active_window_;
  int savepoint_cursor_number_;

  // cached font data
  std::map<std::string, std::pair<std::filesystem::path, int>> resolved_fonts_;

  std::map<std::tuple<std::filesystem::path, int, bool, int>,
           // Cached fonts by file, size, weight, and face index.
           std::shared_ptr<IFont>>
      font_cache_;

  // boost::serialization support
  friend class boost::serialization::access;

  template <class Archive>
  void save(Archive& ar, const unsigned int file_version) const;

  template <class Archive>
  void load(Archive& ar, const unsigned int file_version);

  BOOST_SERIALIZATION_SPLIT_MEMBER()
};

// Name parser. Takes a raw, local machine encoded string and replaces name
// variable placeholders with the names from Memory. This function assumes that
// text is in CP932 encoding, and will need to be generalized when we try to
// support other hacks on top of cp932.
std::string parseNames(MemoryHandle& memory, const std::string& input);
