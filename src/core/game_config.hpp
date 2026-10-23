// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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

#pragma once

#include "core/colour.hpp"
#include "core/rect.hpp"

#include <boost/serialization/array.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>

#include <array>
#include <map>
#include <string>
#include <vector>

class Gameexe;

enum class SoundCategory : unsigned {
  Bgm = 0,
  Voice = 1,
  Pcm = 2,
  Se = 3,
  Movie = 4,
  Extra = 16
};
inline constexpr unsigned SoundIndex(SoundCategory category) {
  return static_cast<unsigned>(category);
}

struct CharacterVoiceConfig {
  // Whether this character's voice playback is enabled.
  bool enable = true;
  // Character voice user volume, from 0 (silent) to 255 (full).
  int volume = 255;

  void serialize(auto& ar, const unsigned int) { ar & enable & volume; }
  bool operator==(const CharacterVoiceConfig&) const = default;
  auto operator<=>(const CharacterVoiceConfig&) const = default;
};

struct MenuEntryConfig {
  // Whether this system-menu entry is present.
  bool exist = true;
  // Whether this system-menu entry may be selected.
  bool enable = true;

  void serialize(auto& ar, const unsigned int) { ar & exist & enable; }
  bool operator==(const MenuEntryConfig&) const = default;
  auto operator<=>(const MenuEntryConfig&) const = default;
};

struct ExtraSwitchConfig {
  // Whether this custom switch is present in the system menu.
  bool exist = true;
  // Whether this custom switch may be changed.
  bool enable = true;
  // Current value of this game-defined switch.
  bool onoff = true;

  void serialize(auto& ar, const unsigned int) { ar & exist & enable & onoff; }
  bool operator==(const ExtraSwitchConfig&) const = default;
  auto operator<=>(const ExtraSwitchConfig&) const = default;
};

struct ExtraModeConfig {
  // Whether this custom mode is present in the system menu.
  bool exist = true;
  // Whether this custom mode may be changed.
  bool enable = true;
  // Current game-defined mode index, initially zero.
  int mode = 0;

  void serialize(auto& ar, const unsigned int) { ar & exist & enable & mode; }
  bool operator==(const ExtraModeConfig&) const = default;
  auto operator<=>(const ExtraModeConfig&) const = default;
};

struct SyscomMenuConfig {
  // Skip previously read messages.
  MenuEntryConfig read_skip{};
  // Skip unread messages.
  MenuEntryConfig unread_skip{};
  // Automatically toggle message skipping.
  MenuEntryConfig auto_skip{};
  // Automatically advance messages.
  MenuEntryConfig auto_mode{};
  // Hide the message window.
  MenuEntryConfig hide_mwnd{};
  // Open message history.
  MenuEntryConfig msg_back{};
  // Open the save interface.
  MenuEntryConfig save{};
  // Open the load interface.
  MenuEntryConfig load{};
  // Return to the previous selection.
  MenuEntryConfig return_to_sel{};
  // Open game preferences.
  MenuEntryConfig config{};
  // Open the game manual.
  MenuEntryConfig manual{};
  // Display the game version.
  MenuEntryConfig version{};
  // Return to the title menu.
  MenuEntryConfig return_to_menu{};
  // Quit the game.
  MenuEntryConfig game_end{};
  // Close the system menu.
  MenuEntryConfig cancel{};
  // Four game-defined per-save menu switches.
  std::array<ExtraSwitchConfig, 4> local_extra_switch{};
  // Four game-defined per-save menu modes.
  std::array<ExtraModeConfig, 4> local_extra_mode{};

  void serialize(auto& ar, const unsigned int) {
    ar & read_skip & unread_skip & auto_skip & auto_mode & hide_mwnd &
        msg_back & save & load & return_to_sel & config & manual & version &
        return_to_menu & game_end & cancel & local_extra_switch &
        local_extra_mode;
  }
  bool operator==(const SyscomMenuConfig&) const = default;
  auto operator<=>(const SyscomMenuConfig&) const = default;
};

struct GlobalSystemConfig {
  // Lower process priority to allow other applications to run smoothly.
  bool low_priority = false;
  // Disable screen transition animations.
  bool disable_wipe_anime = false;
  // Allow input to finish a screen transition early.
  bool skip_wipe_anime = true;
  // Disable message-window opening and closing animations.
  bool disable_mwnd_anime = false;
  // Allow the mouse wheel to advance messages.
  bool allow_wheel_next_message = true;
  // Continue voice playback when advancing to the next message.
  bool continue_koe_playback = false;
  // Allow skipping messages that have not been read.
  bool allow_skip_unread_message = false;
  // Ask for confirmation before saving or loading.
  bool alert_saveload = true;
  // Require a double click to select save/load entries.
  bool require_dblclick_saveload = false;

  void serialize(auto& ar, const unsigned int) {
    ar & low_priority & disable_wipe_anime & skip_wipe_anime &
        disable_mwnd_anime & allow_wheel_next_message & continue_koe_playback &
        allow_skip_unread_message & alert_saveload & require_dblclick_saveload;
  }
  bool operator==(const GlobalSystemConfig&) const = default;
  auto operator<=>(const GlobalSystemConfig&) const = default;
};

struct GlobalRealLiveConfig {
  // RealLive SOUND_DEFAULT format index;
  // 0: 11 k_hz, 8 bit stereo
  // 1: 11 k_hz, 16 bit stereo
  // 2: 22 k_hz, 8 bit stereo
  // 3: 22 k_hz, 16 bit stereo
  // 4: 44 k_hz, 8 bit stereo
  // 5: 44 k_hz, 16 bit stereo
  // 6: 48 k_hz, 8 bit stereo
  // 7: 48 k_hz, 16 bit stereo
  int sound_quality = 5;
  // RealLive's two game-defined original settings.
  std::array<int, 2> generic{};
  // Fifth WINDOW_ATTR component, passed to colour-mask rendering.
  int window_filter = 0;
  // Availability of the RealLive WINDOW_MOVE_USE window feature.
  bool window_move_use = false;
  // Availability of the RealLive WINDOW_CLEAR_USE window feature.
  bool window_clear_use = false;
  // Availability of the RealLive WINDOW_READJUMP_USE window feature.
  bool window_read_jump_use = false;
  // Availability of the RealLive WINDOW_AUTOMODE_USE window feature.
  bool window_automode_use = false;
  // Availability of the RealLive WINDOW_MSGBK_USE window feature.
  bool window_msgbk_use = false;
  // Availability of the RealLive WINDOW_MSGBKLEFT_USE window feature.
  bool window_msgbkleft_use = false;
  // Availability of the RealLive WINDOW_MSGBKRIGHT_USE window feature.
  bool window_msgbkright_use = false;
  // Availability of the RealLive WINDOW_EXBTN_USE window feature.
  bool window_exbtn_use = false;
  // Whether the window title includes the current scene subtitle.
  bool display_subtitle = false;
  // Whether Gameexe declares a custom MOUSE_CURSOR graphic.
  bool use_custom_mouse_cursor = false;

  void serialize(auto& ar, const unsigned int) {
    ar & sound_quality & generic & window_filter & window_move_use &
        window_clear_use & window_read_jump_use & window_automode_use &
        window_msgbk_use & window_msgbkleft_use & window_msgbkright_use &
        window_exbtn_use & display_subtitle & use_custom_mouse_cursor;
  }
  bool operator==(const GlobalRealLiveConfig&) const = default;
  auto operator<=>(const GlobalRealLiveConfig&) const = default;
};

struct LocalRealLiveConfig {
  // RealLive script request to display message text immediately.
  bool fast_text_mode = false;
  // RealLive menu statuses: 0 hidden, 1 selectable, 2 greyed out.
  std::array<int, 32> syscom_status = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                       1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                       1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  // Default filename used by RealLive GRP operations.
  std::string default_grp_name{};
  // Default filename used by RealLive BGR operations.
  std::string default_bgr_name{};

  void serialize(auto& ar, const unsigned int) {
    ar & fast_text_mode & syscom_status & default_grp_name & default_bgr_name;
  }
  bool operator==(const LocalRealLiveConfig&) const = default;
  auto operator<=>(const LocalRealLiveConfig&) const = default;
};

struct GlobalConfig {
  // Screen mode: 0 windowed, 1 fullscreen, 2 freely sized.
  int screen_size_mode = 0;
  // Last windowed sizing mode, restored after leaving fullscreen.
  int screen_size_mode_window = 0;
  // Horizontal and vertical window scale, in percent.
  Size screen_size_scale = Size(100, 100);
  // Requested freely sized window dimensions, in pixels.
  Size screen_size_free{};
  // Whether fullscreen requests a display resolution change.
  bool fullscreen_change_resolution = false;
  // Display count associated with the saved fullscreen preference.
  int fullscreen_display_cnt = 0;
  // Zero-based display selected for fullscreen.
  int fullscreen_display_no = 0;
  // Resolution count associated with the saved display preference.
  int fullscreen_resolution_cnt = 0;
  // Zero-based fullscreen display resolution selection.
  int fullscreen_resolution_no = 0;
  // Requested fullscreen display resolution, in pixels.
  Size fullscreen_resolution{};
  // Reserved legacy effective-resolution placeholder; not persisted.
  bool total_fullscreen_change_resolution = false;
  // Reserved legacy effective display-count placeholder; not persisted.
  int total_fullscreen_display_cnt = 0;
  // Reserved legacy effective display-index placeholder; not persisted.
  int total_fullscreen_display_no = 0;
  // Reserved legacy effective resolution-count placeholder; not persisted.
  int total_fullscreen_resolution_cnt = 0;
  // Reserved legacy effective resolution-index placeholder; not persisted.
  int total_fullscreen_resolution_no = 0;
  // Reserved legacy effective resolution placeholder; not persisted.
  Size total_fullscreen_resolution{};
  // Fullscreen layout: 0 native, 1 fit, 2 fit both axes, 3 vertical,
  // 4 horizontal, 5 custom scale.
  int fullscreen_mode = 1;
  // Custom fullscreen image scale, in percent.
  Size fullscreen_scale = Size(100, 100);
  // Keep horizontal and vertical fullscreen scaling synchronized.
  bool fullscreen_scale_sync_switch = true;
  // Fullscreen image displacement, as percentages of the game screen size.
  Size fullscreen_move{};
  // Master user volume, from 0 to 255.
  int all_sound_user_volume = 255;
  // User volume per SoundCategory; unused categories also default to 255.
  std::array<int, 32> sound_user_volume = {
      255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
      255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
      255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
  // Master audio playback enable preference.
  bool play_all_sound_check = true;
  // Audio enable preferences indexed by SoundCategory.
  std::array<bool, 32> play_sound_check = {
      true, true, true, true, true, true, true, true, true, true, true,
      true, true, true, true, true, true, true, true, true, true, true,
      true, true, true, true, true, true, true, true, true, true};
  // Background music volume while voice ducking is active, from 0 to 255.
  int bgmfade_volume = 192;
  // Whether voice playback ducks background music.
  bool bgmfade_use_check = true;
  // Message-window filter tint with RGBA channels from 0 to 255.
  RGBAColour filter_color = RGBAColour(0, 0, 0, 128);
  // Whether proportional character widths are enabled.
  bool font_proportional = false;
  // User-selected font name or path; empty selects the platform fallback.
  std::string font_name{};
  // Whether user-selected message text is bold.
  bool font_futoku = false;
  // Font decoration bits: bit 0 shadow, bit 1 outline; zero disables both.
  int font_shadow = 2;
  // Delay between message characters, in milliseconds.
  int message_speed = 20;
  // User preference to display message text without a delay.
  bool message_speed_nowait = false;
  // User preference for automatic message advancement.
  bool enable_auto_mode = false;
  // Additional automatic-message wait per character, in milliseconds.
  int auto_mode_moji_wait = 70;
  // Minimum automatic-message wait, in milliseconds.
  int auto_mode_min_wait = 300;
  // Whether an idle mouse cursor is automatically hidden.
  bool hide_idle_cursor = false;
  // Mouse cursor inactivity timeout, in milliseconds.
  int hide_idle_cursor_time = 5000;
  // Enable accelerated voice playback during normal reading.
  bool enable_jitan_normal = false;
  // Enable accelerated voice playback during automatic advancement.
  bool enable_jitan_auto_mode = false;
  // Enable accelerated voice playback while browsing history.
  bool enable_jitan_msgbk = false;
  // Accelerated voice playback rate, in percent.
  int jitan_speed = 100;
  // Voice mode: 0 text and voice, 1 text only, 2 voice only.
  int koe_mode = 0;
  // Voice preferences keyed by Siglus configuration group or RealLive
  // KOEPLAY ID; missing IDs are enabled.
  std::map<int, CharacterVoiceConfig> chrkoe{};
  // Whether message text uses character-specific colours.
  bool message_chrcolor_flag = true;
  // Display flags by object group; RealLive groups 0/1/2 are
  // object1/object2/weather.
  std::vector<int> object_disp_flag = {1, 1, 1, 1};
  // Global game-defined switch values; configured capacity is at most four.
  std::vector<bool> global_extra_switch_flag = {true, true, true, true};
  // Global game-defined mode indices; configured capacity is at most four.
  std::vector<int> global_extra_mode_flag = {0, 0, 0, 0};
  // Global system behavior preferences.
  GlobalSystemConfig system{};
  // Legacy scene-source directory; empty means no configured development path.
  std::string ss_path{};
  // Legacy script-editor path; empty means no configured editor.
  std::string editor_path{};
  // Legacy voice-resource directory; empty means no configured development
  // path.
  std::string koe_path{};
  // Legacy voice-tool executable path; empty means no configured tool.
  std::string koe_tool_path{};
  // RealLive preferences without a direct Siglus counterpart.
  GlobalRealLiveConfig reallive{};

  void serialize(auto& ar, const unsigned int) {
    ar & screen_size_mode & screen_size_mode_window & screen_size_scale &
        screen_size_free & fullscreen_change_resolution &
        fullscreen_display_cnt & fullscreen_display_no &
        fullscreen_resolution_cnt & fullscreen_resolution_no &
        fullscreen_resolution & fullscreen_mode & fullscreen_scale &
        fullscreen_scale_sync_switch & fullscreen_move & all_sound_user_volume &
        sound_user_volume & play_all_sound_check & play_sound_check &
        bgmfade_volume & bgmfade_use_check & filter_color & font_proportional &
        font_name & font_futoku & font_shadow & message_speed &
        message_speed_nowait & enable_auto_mode & auto_mode_moji_wait &
        auto_mode_min_wait & hide_idle_cursor & hide_idle_cursor_time &
        enable_jitan_normal & enable_jitan_auto_mode & enable_jitan_msgbk &
        jitan_speed & koe_mode & chrkoe & message_chrcolor_flag &
        object_disp_flag & global_extra_switch_flag & global_extra_mode_flag &
        system & ss_path & editor_path & koe_path & koe_tool_path & reallive;
  }
  bool operator==(const GlobalConfig&) const = default;
};

struct LocalConfig {
  // Selected mouse cursor graphic; -1 selects no custom cursor.
  int cursor_no = -1;
  // Prevent the system command menu from opening.
  bool disable_syscom_menu = false;
  // Prevent the message window from being hidden.
  bool disable_hide_mwnd = false;
  // Prevent opening message history.
  bool disable_msgback = false;
  // Disable system shortcuts.
  bool disable_shortcut = false;
  // Disable message skipping.
  bool disable_skip = false;
  // Disable skipping through the Control key.
  bool disable_ctrl = false;
  // Keep skipping active when the user clicks.
  bool not_stop_skip_by_click = false;
  // Do not immediately finish message text on a click.
  bool not_skip_msg_by_click = false;
  // Script permission to skip unread messages.
  bool skip_unread_message = false;
  // Script request for automatic message advancement.
  bool auto_mode = false;
  // Script per-character auto wait in milliseconds; -1 inherits global.
  int auto_mode_moji_wait = -1;
  // Script minimum auto wait in milliseconds; -1 inherits global.
  int auto_mode_min_wait = -1;
  // Explicit character count for the current automatic-message wait.
  int auto_mode_moji_cnt = 0;
  // Idle cursor hiding override: -1 inherit, 0 disabled, 1 enabled.
  int mouse_cursor_hide_onoff = -1;
  // Idle cursor timeout in milliseconds; -1 inherits global.
  int mouse_cursor_hide_time = -1;
  // Script message delay in milliseconds; -1 inherits global.
  int msg_speed = -1;
  // Script request to display message text immediately.
  bool msg_nowait = false;
  // Enable asynchronous message output until explicitly disabled.
  bool async_msg_mode = false;
  // Enable asynchronous output for the next message only.
  bool async_msg_mode_once = false;
  // Enable independently revealing message streams within a message window.
  bool multi_msg_mode = false;
  // Request the start of message skipping.
  bool skip_trigger = false;
  // Force voice continuation across messages; takes precedence over off.
  bool koe_dont_stop_on = false;
  // Force voice playback to stop when advancing messages.
  bool koe_dont_stop_off = false;
  // Disable all message-window buttons.
  bool mwnd_btn_disable_all = false;
  // Disable pointer interaction with message-window buttons.
  bool disable_mwnd_btn_touch = false;
  // Force message-window animation; takes precedence over disable.
  bool force_mwnd_anime = false;
  // Disable message-window animation for this script.
  bool disable_mwnd_anime = false;
  // Suppress message-window display for this script.
  bool disable_mwnd_disp = false;
  // Disable recording messages in history.
  bool msg_back_off = false;
  // Suppress message-history display.
  bool msg_back_disp_off = false;
  // Script font bold override: -1 inherit, 0 regular, 1 bold.
  int font_futoku = -1;
  // Script font decoration bitmask; -1 inherits global.
  int font_shadow = -1;
  // Hide the mouse cursor at the script's request.
  bool cursor_disp_off = false;
  // Disable moving the mouse cursor with keyboard input.
  bool cursor_move_by_key_disable = false;
  // Disabled virtual-key flags indexed from 0 through 255.
  std::array<bool, 256> key_disable{};
  // Suppress screen and stage quake rendering.
  bool quake_stop = false;
  // Stop E-mote mouth animation.
  bool emote_mouth_stop = false;
  // Disable recording viewed images in the CG table.
  bool cg_table_off = false;
  // Script request to apply background-music ducking.
  bool bgmfade = false;
  // Disable implicit savepoints; explicit savepoints remain permitted.
  bool dont_set_save_point = false;
  // Ignore message R commands.
  bool ignore_r = false;
  // Disable waiting for display vertical synchronization.
  bool wait_display_vsync_off = false;
  // Stop advancement of all game-time-controlled systems.
  bool time_stop = false;
  // Stop counter advancement independently of other systems.
  bool counter_time_stop = false;
  // Stop frame-action time advancement.
  bool frame_action_time_stop = false;
  // Stop stage animation and quake time advancement.
  bool stage_time_stop = false;
  // Disabled message-window button slots; absent slots remain enabled.
  std::vector<bool> mwnd_btn_disable{};
  // Script font override; empty inherits the global font.
  std::string font_name{};
  // Per-save system-menu permissions and custom option values.
  SyscomMenuConfig syscommenu{};
  // RealLive script settings without a direct Siglus counterpart.
  LocalRealLiveConfig reallive{};

  void serialize(auto& ar, const unsigned int) {
    ar & cursor_no & disable_syscom_menu & disable_hide_mwnd & disable_msgback &
        disable_shortcut & disable_skip & disable_ctrl &
        not_stop_skip_by_click & not_skip_msg_by_click & skip_unread_message &
        auto_mode & auto_mode_moji_wait & auto_mode_min_wait &
        auto_mode_moji_cnt & mouse_cursor_hide_onoff & mouse_cursor_hide_time &
        msg_speed & msg_nowait & async_msg_mode & async_msg_mode_once &
        multi_msg_mode & skip_trigger & koe_dont_stop_on & koe_dont_stop_off &
        mwnd_btn_disable_all & disable_mwnd_btn_touch & force_mwnd_anime &
        disable_mwnd_anime & disable_mwnd_disp & msg_back_off &
        msg_back_disp_off & font_futoku & font_shadow & cursor_disp_off &
        cursor_move_by_key_disable & key_disable & quake_stop &
        emote_mouth_stop & cg_table_off & bgmfade & dont_set_save_point &
        ignore_r & wait_display_vsync_off & time_stop & counter_time_stop &
        frame_action_time_stop & stage_time_stop & mwnd_btn_disable &
        font_name & syscommenu & reallive;
  }
  bool operator==(const LocalConfig&) const = default;
};

// Convert engine Gameexe defaults before constructing any configuration
// borrowers.
LocalConfig MakeRealliveLocalConfig(Gameexe& gameexe);
GlobalConfig MakeRealliveGlobalConfig(Gameexe& gameexe);
LocalConfig MakeSiglusLocalConfig(Gameexe& gameexe);
GlobalConfig MakeSiglusGlobalConfig(Gameexe& gameexe);

// Resolve script overrides against persistent user preferences.
bool EffectiveMessageNowait(const LocalConfig&, const GlobalConfig&);
int EffectiveMessageSpeed(const LocalConfig&, const GlobalConfig&);
bool EffectiveAutoMode(const LocalConfig&, const GlobalConfig&);
int EffectiveAutoTime(const LocalConfig&,
                      const GlobalConfig&,
                      int character_count);
const std::string& EffectiveFontName(const LocalConfig&, const GlobalConfig&);
bool EffectiveFontBold(const LocalConfig&, const GlobalConfig&);
int EffectiveFontShadow(const LocalConfig&, const GlobalConfig&);
bool EffectiveMouseCursorHide(const LocalConfig&, const GlobalConfig&);
int EffectiveMouseCursorHideTime(const LocalConfig&, const GlobalConfig&);
bool EffectiveMwndAnimation(const LocalConfig&, const GlobalConfig&);
bool EffectiveVoiceContinuation(const LocalConfig&, const GlobalConfig&);
