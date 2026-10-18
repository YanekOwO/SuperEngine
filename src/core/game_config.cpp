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

#include "core/game_config.hpp"

#include "core/gameexe.hpp"
#include "utilities/graphics.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <limits>
#include <stdexcept>
#include <string_view>

static RGBAColour ReadColor(Gameexe& gexe,
                            std::string_view key,
                            RGBAColour fallback) {
  auto values = gexe(key).IntVec();
  if (!values || values->size() < 4)
    return fallback;
  return RGBAColour(
      std::clamp((*values)[0], 0, 255), std::clamp((*values)[1], 0, 255),
      std::clamp((*values)[2], 0, 255), std::clamp((*values)[3], 0, 255));
}

static int IndexedInt(Gameexe& gexe,
                      std::string_view root,
                      int id,
                      std::string_view suffix,
                      int fallback) {
  const std::string padded = std::format("{}.{:03}.{}", root, id, suffix);
  if (gexe.Exists(padded))
    return gexe(padded).ReadInt(fallback);
  return gexe(std::format("{}.{}.{}", root, id, suffix)).ReadInt(fallback);
}

static int ParseVoiceID(std::string_view text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first != std::string_view::npos) {
    const auto last = text.find_last_not_of(" \t\r\n");
    text = text.substr(first, last - first + 1);
  }
  int value;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc() || end != text.data() + text.size())
    throw std::runtime_error("Invalid KOEONOFF voice ID: " + std::string(text));
  return value;
}

LocalConfig MakeRealliveLocalConfig(Gameexe& gexe) {
  LocalConfig result;
  result.cursor_no = gexe("MOUSE_CURSOR").ReadInt(0);
  result.disable_ctrl = !gexe("CTRL_USE").ReadBool(true);
  return result;
}

GlobalConfig MakeRealliveGlobalConfig(Gameexe& gexe) {
  GlobalConfig result;
  result.screen_size_free = gexe.Exists("SCREENSIZE_MOD")
                                ? GetRealliveScreenSize(gexe)
                                : Size(800, 600);
  result.fullscreen_resolution = result.screen_size_free;
  result.message_speed = gexe("INIT_MESSAGE_SPEED").ReadInt(30);
  result.auto_mode_moji_wait = result.message_speed;
  result.auto_mode_min_wait = gexe("MESSAGE_KEY_WAIT_TIME").ReadInt(1500);
  // These preferences previously start disabled independently of the default
  // opcode's Gameexe values; preserve RealLive initialization behavior.
  result.message_speed_nowait = false;
  result.enable_auto_mode = false;
  result.font_name = gexe("CONFIG.FONT.NAME").Str().value_or("");
  result.font_shadow = 1;
  result.bgmfade_volume = 128;
  result.filter_color = ReadColor(gexe, "WINDOW_ATTR", RGBAColour::White());
  result.reallive.window_filter = gexe("WINDOW_ATTR").IntAt(4).value_or(0);
  result.object_disp_flag[0] = !gexe("INIT_OBJECT1_ONOFF_MOD").ReadBool(false);
  result.object_disp_flag[1] = !gexe("INIT_OBJECT2_ONOFF_MOD").ReadBool(false);
  result.object_disp_flag[2] = !gexe("INIT_WEATHER_ONOFF_MOD").ReadBool(false);
  result.reallive.sound_quality = gexe("SOUND_DEFAULT").ReadInt(5);
  result.reallive.generic = {gexe("INIT_ORIGINALSETING1_MOD").ReadInt(0),
                             gexe("INIT_ORIGINALSETING2_MOD").ReadInt(0)};
  const std::pair<std::string_view, bool GlobalRealLiveConfig::*> features[] = {
      {"WINDOW_MOVE_USE", &GlobalRealLiveConfig::window_move_use},
      {"WINDOW_CLEAR_USE", &GlobalRealLiveConfig::window_clear_use},
      {"WINDOW_READJUMP_USE", &GlobalRealLiveConfig::window_read_jump_use},
      {"WINDOW_AUTOMODE_USE", &GlobalRealLiveConfig::window_automode_use},
      {"WINDOW_MSGBK_USE", &GlobalRealLiveConfig::window_msgbk_use},
      {"WINDOW_MSGBKLEFT_USE", &GlobalRealLiveConfig::window_msgbkleft_use},
      {"WINDOW_MSGBKRIGHT_USE", &GlobalRealLiveConfig::window_msgbkright_use},
      {"WINDOW_EXBTN_USE", &GlobalRealLiveConfig::window_exbtn_use}};
  for (auto [key, field] : features)
    result.reallive.*field = gexe(key).ReadBool(false);
  result.reallive.display_subtitle = gexe("SUBTITLE").ReadBool(false);
  result.reallive.use_custom_mouse_cursor = gexe.Exists("MOUSE_CURSOR");

  for (auto setting : gexe.Filter("KOEONOFF.")) {
    const auto parts = setting.GetKeyParts();
    if (parts.size() < 4)
      throw std::runtime_error("Invalid KOEONOFF entry: " + setting.key());
    std::string_view ids(parts[2]);
    if (ids.starts_with('(') && ids.ends_with(')'))
      ids = ids.substr(1, ids.size() - 2);
    while (!ids.empty()) {
      const auto comma = ids.find(',');
      result.chrkoe[ParseVoiceID(ids.substr(0, comma))].enable =
          parts[3] == "ON";
      if (comma == std::string_view::npos)
        break;
      ids.remove_prefix(comma + 1);
    }
  }
  return result;
}

LocalConfig MakeSiglusLocalConfig(Gameexe& gexe) {
  LocalConfig result;
  result.cursor_no = gexe("MOUSE_CURSOR.DEFAULT").ReadInt(-1);
  const int buttons = std::clamp(gexe("WAKU.BTN.CNT").ReadInt(8), 0, 256);
  result.mwnd_btn_disable.resize(buttons, false);
  const std::pair<std::string_view, MenuEntryConfig SyscomMenuConfig::*>
      entries[] = {{"READ_SKIP", &SyscomMenuConfig::read_skip},
                   {"UNREAD_SKIP", &SyscomMenuConfig::unread_skip},
                   {"AUTO_SKIP", &SyscomMenuConfig::auto_skip},
                   {"AUTO_MODE", &SyscomMenuConfig::auto_mode},
                   {"HIDE_MWND", &SyscomMenuConfig::hide_mwnd},
                   {"MSG_BACK", &SyscomMenuConfig::msg_back},
                   {"SAVE", &SyscomMenuConfig::save},
                   {"LOAD", &SyscomMenuConfig::load},
                   {"RETURN_SEL", &SyscomMenuConfig::return_to_sel},
                   {"CONFIG", &SyscomMenuConfig::config},
                   {"MANUAL", &SyscomMenuConfig::manual},
                   {"VERSION", &SyscomMenuConfig::version},
                   {"RETURN_MENU", &SyscomMenuConfig::return_to_menu},
                   {"GAME_END", &SyscomMenuConfig::game_end},
                   {"CANCEL", &SyscomMenuConfig::cancel}};
  for (auto [key, field] : entries) {
    auto source = gexe("SYSCOMMENU", key);
    auto& entry = result.syscommenu.*field;
    entry.exist = source.IntAt(0).value_or(1) != 0;
    entry.enable = source.IntAt(1).value_or(1) != 0;
  }
  for (int id = 0; id < 4; ++id) {
    auto switch_source = gexe("SYSCOMMENU", "LOCAL_EXTRA_SWITCH", id);
    if (!switch_source.Exists())
      switch_source = gexe(std::format("SYSCOMMENU.LOCAL_EXTRA_SWITCH.{}", id));
    auto& sw = result.syscommenu.local_extra_switch[id];
    sw.exist = switch_source.IntAt(0).value_or(1) != 0;
    sw.enable = switch_source.IntAt(1).value_or(1) != 0;
    sw.onoff = switch_source.IntAt(2).value_or(1) != 0;
    auto mode_source = gexe("SYSCOMMENU", "LOCAL_EXTRA_MODE", id);
    if (!mode_source.Exists())
      mode_source = gexe(std::format("SYSCOMMENU.LOCAL_EXTRA_MODE.{}", id));
    auto& mode = result.syscommenu.local_extra_mode[id];
    mode.exist = mode_source.IntAt(0).value_or(1) != 0;
    mode.enable = mode_source.IntAt(1).value_or(1) != 0;
    mode.mode = mode_source.IntAt(2).value_or(0);
  }
  return result;
}

GlobalConfig MakeSiglusGlobalConfig(Gameexe& gexe) {
  GlobalConfig result;
  result.screen_size_mode = gexe("CONFIG.WINDOW_MODE").ReadInt(0);
  result.screen_size_free = GetSiglusScreenSize(gexe);
  result.fullscreen_resolution = result.screen_size_free;
  switch (gexe("CONFIG.FONT.TYPE").ReadInt(0)) {
    case 1:
      result.font_name = "MS Mincho";
      break;
    case 2:
      result.font_name = "Meiryo";
      break;
    case 3:
      result.font_name = gexe("CONFIG.FONT.NAME").Str().value_or("MS Gothic");
      break;
    default:
      result.font_name = "MS Gothic";
      break;
  }
  result.font_futoku = gexe("CONFIG.FONT.FUTOKU").ReadBool(false);
  result.font_shadow = gexe("CONFIG.FONT.SHADOW").ReadInt(2);
  result.message_speed = gexe("CONFIG.MESSAGE_SPEED").ReadInt(20);
  result.message_speed_nowait =
      gexe("CONFIG.MESSAGE_SPEED_NOWAIT.ONOFF").ReadBool(false);
  result.message_chrcolor_flag =
      gexe("CONFIG.MESSAGE_CHRCOLOR.ONOFF").ReadBool(true);
  result.hide_idle_cursor =
      gexe("CONFIG.MOUSE_CURSOR_HIDE_ONOFF").ReadBool(false);
  result.hide_idle_cursor_time =
      gexe("CONFIG.MOUSE_CURSOR_HIDE_TIME").ReadInt(5000);
  result.bgmfade_volume = gexe("CONFIG.BGMFADE_VOLUME").ReadInt(192);
  result.bgmfade_use_check = gexe("CONFIG.BGMFADE_ONOFF").ReadBool(true);
  result.filter_color =
      ReadColor(gexe, "CONFIG.FILTER_COLOR", result.filter_color);
  result.all_sound_user_volume = gexe("CONFIG.VOLUME.ALL").ReadInt(255);
  const std::pair<std::string_view, SoundCategory> categories[] = {
      {"BGM", SoundCategory::Bgm},
      {"KOE", SoundCategory::Voice},
      {"PCM", SoundCategory::Pcm},
      {"SE", SoundCategory::Se},
      {"MOV", SoundCategory::Movie}};
  for (auto [name, category] : categories)
    result.sound_user_volume[SoundIndex(category)] =
        gexe(std::format("CONFIG.VOLUME.{}", name)).ReadInt(255);
  const std::pair<std::string_view, bool GlobalSystemConfig::*> flags[] = {
      {"SLEEP", &GlobalSystemConfig::low_priority},
      {"NO_WIPE_ANIME", &GlobalSystemConfig::disable_wipe_anime},
      {"SKIP_WIPE_ANIME", &GlobalSystemConfig::skip_wipe_anime},
      {"NO_MWND_ANIME", &GlobalSystemConfig::disable_mwnd_anime},
      {"WHEEL_NEXT_MESSAGE", &GlobalSystemConfig::allow_wheel_next_message},
      {"KOE_DONT_STOP", &GlobalSystemConfig::continue_koe_playback},
      {"SKIP_UNREAD_MESSAGE", &GlobalSystemConfig::allow_skip_unread_message},
      {"SAVELOAD_ALERT", &GlobalSystemConfig::alert_saveload},
      {"SAVELOAD_DBLCLICK", &GlobalSystemConfig::require_dblclick_saveload}};
  for (auto [key, field] : flags) {
    // CONFIG is the legacy spelling; SYSTEM was previously used by bindings.
    result.system.*field = gexe(std::format("CONFIG.{}.ONOFF", key))
                               .ReadBool(gexe(std::format("SYSTEM.{}", key))
                                             .ReadBool(result.system.*field));
  }
  const int character_count =
      std::clamp(gexe("CHRKOE.CNT").ReadInt(64), 0, 256);
  for (int id = 0; id < character_count; ++id) {
    auto source = gexe("CHRKOE", id);
    if (!source.Exists())
      source = gexe(std::format("CHRKOE.{}", id));
    auto& voice = result.chrkoe[id];
    voice.enable = source.IntAt(3).value_or(1) != 0;
    voice.volume = std::clamp(source.IntAt(4).value_or(255), 0, 255);
  }
  for (int id = 0; id < 4; ++id) {
    result.object_disp_flag[id] =
        IndexedInt(gexe, "CONFIG.OBJECT_DISP", id, "ONOFF", 1) != 0;
    result.global_extra_switch_flag[id] =
        IndexedInt(gexe, "CONFIG.GLOBAL_EXTRA_SWITCH", id, "ONOFF", 1) != 0;
    result.global_extra_mode_flag[id] =
        IndexedInt(gexe, "CONFIG.GLOBAL_EXTRA_MODE", id, "MODE", 0);
  }
  return result;
}

bool EffectiveMessageNowait(const LocalConfig& local,
                            const GlobalConfig& global) {
  return local.msg_nowait ||
         (local.msg_speed == -1 && global.message_speed_nowait);
}
int EffectiveMessageSpeed(const LocalConfig& local,
                          const GlobalConfig& global) {
  if (EffectiveMessageNowait(local, global))
    return 0;
  return local.msg_speed == -1 ? global.message_speed : local.msg_speed;
}
bool EffectiveAutoMode(const LocalConfig& local, const GlobalConfig& global) {
  return local.auto_mode || global.enable_auto_mode;
}
int EffectiveAutoTime(const LocalConfig& local,
                      const GlobalConfig& global,
                      int character_count) {
  const int base = local.auto_mode && local.auto_mode_min_wait >= 0
                       ? local.auto_mode_min_wait
                       : global.auto_mode_min_wait;
  const int per_character =
      local.auto_mode && local.auto_mode_moji_wait >= 0
          ? local.auto_mode_moji_wait
          : global.auto_mode_moji_wait;
  const std::int64_t total =
      static_cast<std::int64_t>(base) +
      static_cast<std::int64_t>(per_character) * std::max(character_count, 0);
  return static_cast<int>(
      std::clamp<std::int64_t>(total, 0, std::numeric_limits<int>::max()));
}
const std::string& EffectiveFontName(const LocalConfig& local,
                                     const GlobalConfig& global) {
  return local.font_name.empty() ? global.font_name : local.font_name;
}
bool EffectiveFontBold(const LocalConfig& local, const GlobalConfig& global) {
  return local.font_futoku < 0 ? global.font_futoku : local.font_futoku != 0;
}
int EffectiveFontShadow(const LocalConfig& local, const GlobalConfig& global) {
  return local.font_shadow < 0 ? global.font_shadow : local.font_shadow;
}
bool EffectiveMouseCursorHide(const LocalConfig& local,
                              const GlobalConfig& global) {
  if (local.mouse_cursor_hide_onoff == 0)
    return false;
  if (local.mouse_cursor_hide_onoff == 1)
    return true;
  return global.hide_idle_cursor;
}
int EffectiveMouseCursorHideTime(const LocalConfig& local,
                                 const GlobalConfig& global) {
  return local.mouse_cursor_hide_time == -1 ? global.hide_idle_cursor_time
                                            : local.mouse_cursor_hide_time;
}
bool EffectiveMwndAnimation(const LocalConfig& local,
                            const GlobalConfig& global) {
  return local.force_mwnd_anime ||
         (!local.disable_mwnd_anime && !global.system.disable_mwnd_anime);
}
bool EffectiveVoiceContinuation(const LocalConfig& local,
                                const GlobalConfig& global) {
  return local.koe_dont_stop_on ||
         (!local.koe_dont_stop_off && global.system.continue_koe_playback);
}
