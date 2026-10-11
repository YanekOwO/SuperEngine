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

#include "core/event_listener.hpp"
#include "core/mwnd_config.hpp"
#include "machine/long_operation.hpp"

#include <boost/serialization/split_member.hpp>
#include <boost/serialization/version.hpp>

#include <filesystem>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class Gameexe;
class MemoryHandle;
class Point;
class RGBColour;
class RLMachine;
class Size;
class SDLSurface;
class System;
class TextKeyCursor;
class TextPage;
class TextWindow;
class ITextSystem;
class IFont;

namespace text_system_detail {

struct KoeReplayConfig {
  std::string icon_name;
  int x_offset = 0;
  int y_offset = 0;
};

std::optional<KoeReplayConfig> ReadKoeReplayConfig(Gameexe& gexe);

}  // namespace text_system_detail

// Global variables written to disk.
struct TextSystemGlobals {
  TextSystemGlobals();
  explicit TextSystemGlobals(Gameexe& gexe);

  int auto_mode_base_time;
  int auto_mode_char_time;

  // Message speed; range from 0 to 255
  char message_speed;

  // Whether we should be bolded.
  int font_weight;

  // Whether we should draw a shadow.
  int font_shadow;

  // The default #WINDOW_ATTR. This is what is changed by the
  std::vector<int> window_attr;

  // boost::serialization support
  template <class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    ar & auto_mode_base_time & auto_mode_char_time & message_speed &
        window_attr;

    if (version > 0)
      ar & font_weight & font_shadow;
  }
};

BOOST_CLASS_VERSION(TextSystemGlobals, 1)

// -----------------------------------------------------------------------

class TextSystem final : public EventListener {
 public:
  // Internal structure used to keep track of the state of
  typedef std::map<int, TextPage> PageSet;

 public:
  TextSystem(System& system,
             Gameexe& gexe,
             std::unique_ptr<ITextSystem> impl,
             MwndConfig mwnd_config);
  virtual ~TextSystem();

  // Controls whether the text system is rendered at all.
  bool system_visible() const { return system_visible_; }
  void set_system_visible(bool in) { system_visible_ = in; }

  void ExecuteTextSystem();

  void Render();
  void HideTextWindow(int win_number);
  void HideAllTextWindows();
  void HideAllTextWindowsExcept(int i);
  void ShowTextWindow(int win_number);
  void ShowAllTextWindows();
  void ClearAllTextWindows();
  void SetVisualOverride(int win_number, bool show_window);
  void SetVisualOverrideAll(bool show_window);
  void ClearVisualOverrides();

  // Text window, aka mwnd. Consists of waku decoration and TextPage layout
  // management
  std::shared_ptr<TextWindow> GetTextWindow(int text_window_number);
  std::shared_ptr<TextWindow> GetTextWindow(
      int num,
      std::function<std::shared_ptr<TextWindow>()> orelse);
  std::shared_ptr<TextWindow> GetCurrentWindow();
  const MwndConfig& mwnd_config() const { return mwnd_config_; }
  using MwndCallHandler =
      std::function<void(const MwndConfig::CallTarget& target)>;
  using MwndActionHandler =
      std::function<bool(const MwndConfig::Button& button, bool execute)>;
  void SetMwndCallHandler(MwndCallHandler handler) {
    mwnd_call_handler_ = std::move(handler);
  }
  void SetMwndActionHandler(MwndActionHandler handler) {
    mwnd_action_handler_ = std::move(handler);
  }
  bool IsMwndButtonEnabled(const MwndConfig::Button& button) const;
  void ExecuteMwndButton(const MwndConfig::Button& button);

  void set_in_pause_state(bool in) { in_pause_state_ = in; }

  int active_window() const { return active_window_; }
  void set_active_window(int window) { active_window_ = window; }

  std::vector<int> GetActiveWindows();

  // Take a snapshot of the current window state, with their
  // respective TextPages, and add it to the backlog.
  void Snapshot();

  // Resets the text page in the current_set
  void NewPageOnWindow(int window);

  // Get the active page. This function will return
  // windows_[active_window_].page().
  TextPage& GetCurrentPage();

  // Clears the screen, moves back one page and renders it.
  void BackPage();
  void ForwardPage();

  void ReplayPageSet(PageSet& set, bool is_current_page);

  bool IsReadingBacklog() const;
  void StopReadingBacklog();

  // Performs #NAMAE replacement; used in the English Edition of Clannad.
  std::string InterpretName(const std::string& utf8name);

  // A temporary version of |message_no_wait_| controllable by the script.
  void set_script_message_nowait(const int in) { script_message_no_wait_ = in; }
  int script_message_nowait() const { return script_message_no_wait_; }

  // It is possible to set the interpreter up to advance text
  // automatically instead of waiting for player input after each
  // screen is displayed; the `auto mode' controls permit this
  // behavior to be customized.
  int auto_mode() const { return (int)auto_mode_; }
  void SetAutoMode(int i);

  int auto_base_time() const { return globals_.auto_mode_base_time; }
  void set_auto_base_time(int i) { globals_.auto_mode_base_time = i; }

  void set_auto_char_time(int i) { globals_.auto_mode_char_time = i; }
  int autoCharTime() const { return globals_.auto_mode_char_time; }

  int GetAutoTime(int num_chars);

  void SetKeyCursor(int new_cursor);

  // Returns the key cursor index. (or -1 if no key cursor).
  int GetCursorNumber() const;

  int ctrl_key_skip() const { return ctrl_key_skip_; }
  void set_ctrl_key_skip(int i) { ctrl_key_skip_ = i; }

  int fast_text_mode() const { return fast_text_mode_; }
  void set_fast_text_mode(int i) { fast_text_mode_ = i; }

  int message_no_wait() const { return message_no_wait_; }
  void set_message_no_wait(int i) { message_no_wait_ = i; }

  int message_speed() const { return globals_.message_speed; }
  void set_message_speed(int i) { globals_.message_speed = i; }

  int font_weight() const { return globals_.font_weight; }
  void set_font_weight(int i) { globals_.font_weight = i; }

  int font_shadow() const { return globals_.font_shadow; }
  void set_font_shadow(int i) { globals_.font_shadow = i; }

  const std::vector<int>& window_attr() const { return globals_.window_attr; }
  void SetDefaultWindowAttr(const std::vector<int>& attr);

  int window_attr_r() const { return globals_.window_attr.at(0); }
  int window_attr_g() const { return globals_.window_attr.at(1); }
  int window_attr_b() const { return globals_.window_attr.at(2); }
  int window_attr_a() const { return globals_.window_attr.at(3); }
  int window_attr_f() const { return globals_.window_attr.at(4); }

  void SetWindowAttrR(int i);
  void SetWindowAttrG(int i);
  void SetWindowAttrB(int i);
  void SetWindowAttrA(int i);
  void SetWindowAttrF(int i);

  bool window_move_use() const { return move_use_; }
  bool window_clear_use() const { return clear_use_; }
  bool window_read_jump_use() const { return read_jump_use_; }
  bool window_automode_use() const { return automode_use_; }
  bool window_msgbk_use() const { return msgbk_use_; }
  bool window_msgbkleft_use() const { return msgbkleft_use_; }
  bool window_msgbkright_use() const { return msgbkright_use_; }
  bool window_exbtn_use() const { return exbtn_use_; }

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

  TextSystemGlobals& globals() { return globals_; }

  // Resets non-configuration values (so we can load games).
  virtual void Reset();

  bool kidoku_read() const { return kidoku_read_; }
  void SetKidokuRead(const int in);

  bool skip_mode() const { return skip_mode_; }
  void SetSkipMode(int in);

  bool CurrentlySkipping() const;

  void set_in_selection_mode(const bool in) { in_selection_mode_ = in; }

  // Overridden from EventListener
  virtual void OnEvent(std::shared_ptr<Event> event) override;

  System& system() { return system_; }

  ITextSystem& impl() { return *text_impl_; }

  // Load a font of given size
  std::shared_ptr<IFont> LoadFont(int size);

 private:
  void UpdateWindowsForChangeToWindowAttr();

  bool ShowWindow(int win_num) const;

  void CheckAndSetBool(Gameexe& gexe, const std::string& key, bool& out);

  // Reduces the number of page snapshots in previous_page_sets_ down to a
  // manageable constant number.
  void ExpireOldPages();

  // Platform dependent text implementor
  std::unique_ptr<ITextSystem> text_impl_;

  std::filesystem::path default_font_file_;

  // Cached instances of default font of various sizes
  std::unordered_map<int, std::shared_ptr<IFont>> font_cache_;

  // TextPage will call our internals since it actually does most of
  // the work while we hold state.
  friend class TextPage;

  // Whether Auto mode is enabled
  bool auto_mode_;

  // Whether holding down the control key will skip text.
  bool ctrl_key_skip_;

  // Fast text mode
  bool fast_text_mode_;

  // Internal 'no wait' flag (user customization)
  bool message_no_wait_;

  // 'No wait' flag under script control.
  bool script_message_no_wait_;

  // Sets which window is the current active window.
  int active_window_;

  // Type of the Window storage
  typedef std::map<int, std::shared_ptr<TextWindow>> WindowMap;

  // Storage of active windows
  WindowMap text_window_;
  MwndConfig mwnd_config_;
  MwndCallHandler mwnd_call_handler_;
  MwndActionHandler mwnd_action_handler_;

  // Whether we are reading the backlog
  bool is_reading_backlog_;

  // The current page set. Represents what is on the screen right now.
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

  bool move_use_, clear_use_, read_jump_use_, automode_use_, msgbk_use_,
      msgbkleft_use_, msgbkright_use_, exbtn_use_;

  TextSystemGlobals globals_;

  bool system_visible_;

  // Whether we skip text that we've already seen
  bool skip_mode_;

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

// LongOperation which just calls text().set_system_visible(true) and removes
// itself from the callstack.
struct RestoreTextSystemVisibility : public LongOperation {
  virtual bool operator()(RLMachine& machine) override;
};
