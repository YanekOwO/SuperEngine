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

#include <gtest/gtest.h>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <sstream>

TEST(GameConfigTest, GlobalRoundTrip) {
  GlobalConfig global;
  std::stringstream ss;
  {
    boost::archive::text_oarchive ar(ss);
    ar << global;
  }

  GlobalConfig restored;
  {
    boost::archive::text_iarchive ar(ss);
    ar >> restored;
  }

  EXPECT_EQ(restored, global);
}

TEST(GameConfigTest, LocalRoundTrip) {
  LocalConfig local;
  std::stringstream ss;
  {
    boost::archive::text_oarchive ar(ss);
    ar << local;
  }

  LocalConfig restored;
  {
    boost::archive::text_iarchive ar(ss);
    ar >> restored;
  }

  EXPECT_EQ(restored, local);
}

TEST(GameConfigTest, ReservedFullscreenFieldsAreNotPersisted) {
  GlobalConfig config;
  config.total_fullscreen_change_resolution = true;
  config.total_fullscreen_display_cnt = 7;
  config.total_fullscreen_display_no = 3;
  config.total_fullscreen_resolution_cnt = 20;
  config.total_fullscreen_resolution_no = 12;
  config.total_fullscreen_resolution = Size(1920, 1080);

  std::stringstream ss;
  {
    boost::archive::text_oarchive ar(ss);
    ar << config;
  }
  GlobalConfig loaded;
  {
    boost::archive::text_iarchive ar(ss);
    ar >> loaded;
  }

  EXPECT_FALSE(loaded.total_fullscreen_change_resolution);
  EXPECT_EQ(loaded.total_fullscreen_display_cnt, 0);
  EXPECT_EQ(loaded.total_fullscreen_display_no, 0);
  EXPECT_EQ(loaded.total_fullscreen_resolution_cnt, 0);
  EXPECT_EQ(loaded.total_fullscreen_resolution_no, 0);
  EXPECT_EQ(loaded.total_fullscreen_resolution, Size());
}

TEST(GameConfigTest, RealLiveDefaultsRemainEngineSpecific) {
  Gameexe gameexe;
  const auto local = MakeRealliveLocalConfig(gameexe);
  const auto global = MakeRealliveGlobalConfig(gameexe);
  EXPECT_EQ(local.msg_speed, -1);
  EXPECT_EQ(local.cursor_no, 0);
  EXPECT_FALSE(local.disable_ctrl);
  EXPECT_EQ(global.message_speed, 30);
  EXPECT_EQ(global.auto_mode_min_wait, 1500);
  EXPECT_EQ(global.auto_mode_moji_wait, 30);
  EXPECT_TRUE(global.font_name.empty());
  EXPECT_EQ(global.font_shadow, 1);
  EXPECT_EQ(global.bgmfade_volume, 128);
  EXPECT_EQ(global.filter_color, RGBAColour::White());
  EXPECT_EQ(global.screen_size_mode, 0);
  EXPECT_EQ(global.reallive.sound_quality, 5);
}

TEST(GameConfigTest, ConvertsRealLivePreferencesWithoutTruncatingSpeed) {
  Gameexe gameexe;
  gameexe.parseLine("#INIT_MESSAGE_SPEED=300");
  gameexe.parseLine("#MESSAGE_KEY_WAIT_TIME=2700");
  gameexe.parseLine("#WINDOW_ATTR=10,20,30,40,1");
  gameexe.parseLine("#INIT_OBJECT1_ONOFF_MOD=1");
  gameexe.parseLine("#INIT_OBJECT2_ONOFF_MOD=0");
  gameexe.parseLine("#INIT_WEATHER_ONOFF_MOD=1");
  gameexe.parseLine("#INIT_ORIGINALSETING1_MOD=3");
  gameexe.parseLine("#INIT_ORIGINALSETING2_MOD=4");
  gameexe.parseLine("#CTRL_USE=0");
  gameexe.parseLine("#MOUSE_CURSOR=8");
  gameexe.parseLine("#SOUND_DEFAULT=7");
  gameexe.parseLine("#WINDOW_READJUMP_USE=1");
  gameexe.parseLine("#SUBTITLE=1");
  const auto local = MakeRealliveLocalConfig(gameexe);
  const auto global = MakeRealliveGlobalConfig(gameexe);
  EXPECT_EQ(global.message_speed, 300);
  EXPECT_EQ(global.auto_mode_moji_wait, 300);
  EXPECT_EQ(global.auto_mode_min_wait, 2700);
  EXPECT_EQ(global.filter_color, RGBAColour(10, 20, 30, 40));
  EXPECT_EQ(global.reallive.window_filter, 1);
  EXPECT_EQ(global.object_disp_flag, (std::vector<int>{0, 1, 0, 1}));
  EXPECT_EQ(global.reallive.generic, (std::array<int, 2>{3, 4}));
  EXPECT_EQ(global.reallive.sound_quality, 7);
  EXPECT_TRUE(global.reallive.window_read_jump_use);
  EXPECT_TRUE(global.reallive.display_subtitle);
  EXPECT_TRUE(global.reallive.use_custom_mouse_cursor);
  EXPECT_TRUE(local.disable_ctrl);
  EXPECT_EQ(local.cursor_no, 8);
}

TEST(GameConfigTest, RealLivePreservesConfiguredFontName) {
  Gameexe gameexe;
  gameexe.parseLine("#CONFIG.FONT.NAME=\"Custom Font\"");
  const auto local = MakeRealliveLocalConfig(gameexe);
  const auto global = MakeRealliveGlobalConfig(gameexe);
  EXPECT_EQ(global.font_name, "Custom Font");
  EXPECT_EQ(EffectiveFontName(local, global), "Custom Font");
}

TEST(GameConfigTest, RealLiveVoiceIDsAreSparseAndMayBeGrouped) {
  Gameexe gameexe;
  gameexe.parseLine("#KOEONOFF.007.003.OFF=\"Different ids\"");
  gameexe.parseLine("#KOEONOFF.008.(002, 020, 1105).ON=\"Grouped ids\"");
  const auto config = MakeRealliveGlobalConfig(gameexe);
  ASSERT_EQ(config.chrkoe.size(), 4);
  EXPECT_FALSE(config.chrkoe.at(3).enable);
  EXPECT_TRUE(config.chrkoe.at(2).enable);
  EXPECT_TRUE(config.chrkoe.at(20).enable);
  EXPECT_TRUE(config.chrkoe.at(1105).enable);
  EXPECT_EQ(config.chrkoe.at(3).volume, 255);
}

TEST(GameConfigTest, SiglusDefaultsMatchLegacyOverrides) {
  Gameexe gameexe;
  const auto local = MakeSiglusLocalConfig(gameexe);
  const auto global = MakeSiglusGlobalConfig(gameexe);
  EXPECT_EQ(local.msg_speed, -1);
  EXPECT_EQ(local.auto_mode_moji_wait, -1);
  EXPECT_EQ(local.auto_mode_min_wait, -1);
  EXPECT_EQ(local.font_futoku, -1);
  EXPECT_EQ(local.font_shadow, -1);
  EXPECT_EQ(local.mouse_cursor_hide_onoff, -1);
  EXPECT_EQ(local.mouse_cursor_hide_time, -1);
  EXPECT_EQ(local.cursor_no, -1);
  EXPECT_EQ(global.message_speed, 20);
  EXPECT_EQ(global.auto_mode_min_wait, 300);
  EXPECT_EQ(global.auto_mode_moji_wait, 70);
  EXPECT_EQ(global.font_shadow, 2);
  EXPECT_EQ(global.bgmfade_volume, 192);
  EXPECT_EQ(global.filter_color, RGBAColour(0, 0, 0, 128));
  EXPECT_TRUE(global.system.skip_wipe_anime);
  EXPECT_TRUE(global.system.allow_wheel_next_message);
  EXPECT_EQ(global.chrkoe.size(), 64);
  EXPECT_EQ(local.key_disable.size(), 256);
  EXPECT_EQ(local.mwnd_btn_disable.size(), 8);
  EXPECT_EQ(local.syscommenu.local_extra_switch.size(), 4);
}

TEST(GameConfigTest, ConvertsSiglusAudioFontWindowAndMenuPreferences) {
  Gameexe gameexe;
  gameexe.parseLine("#SCREEN_SIZE=1920,1080");
  gameexe.parseLine("#CONFIG.WINDOW_MODE=1");
  gameexe.parseLine("#CONFIG.VOLUME.ALL=200");
  gameexe.parseLine("#CONFIG.VOLUME.BGM=148");
  gameexe.parseLine("#CONFIG.VOLUME.MOV=140");
  gameexe.parseLine("#CONFIG.BGMFADE_VOLUME=204");
  gameexe.parseLine("#CONFIG.FILTER_COLOR=0,1,2,160");
  gameexe.parseLine("#CONFIG.FONT.NAME=\"Custom Font\"");
  gameexe.parseLine("#CONFIG.FONT.TYPE=3");
  gameexe.parseLine("#CONFIG.FONT.FUTOKU=1");
  gameexe.parseLine("#CONFIG.FONT.SHADOW=3");
  gameexe.parseLine("#CONFIG.NO_WIPE_ANIME.ONOFF=1");
  gameexe.parseLine("#SYSTEM.SKIP_WIPE_ANIME=0");
  gameexe.parseLine("#CONFIG.GLOBAL_EXTRA_MODE.003.MODE=2");
  gameexe.parseLine("#CONFIG.GLOBAL_EXTRA_SWITCH.1.ONOFF=0");
  gameexe.parseLine("#CHRKOE.CNT=2");
  gameexe.parseLine("#CHRKOE.001=\"A\",2,\"\",0,123");
  gameexe.parseLine("#MOUSE_CURSOR.DEFAULT=5");
  gameexe.parseLine("#WAKU.BTN.CNT=60");
  gameexe.parseLine("#SYSCOMMENU.READ_SKIP=0,1,\"Skip\"");
  gameexe.parseLine("#SYSCOMMENU.LOCAL_EXTRA_SWITCH.000=1,0,0,\"Extra\"");
  gameexe.parseLine("#SYSCOMMENU.LOCAL_EXTRA_MODE.1=0,1,2,\"Mode\"");
  const auto local = MakeSiglusLocalConfig(gameexe);
  const auto global = MakeSiglusGlobalConfig(gameexe);
  EXPECT_EQ(global.screen_size_free, Size(1920, 1080));
  EXPECT_EQ(global.screen_size_mode, 1);
  EXPECT_EQ(global.all_sound_user_volume, 200);
  EXPECT_EQ(global.sound_user_volume[SoundIndex(SoundCategory::Bgm)], 148);
  EXPECT_EQ(global.sound_user_volume[SoundIndex(SoundCategory::Movie)], 140);
  EXPECT_EQ(global.bgmfade_volume, 204);
  EXPECT_EQ(global.filter_color, RGBAColour(0, 1, 2, 160));
  EXPECT_EQ(global.font_name, "Custom Font");
  EXPECT_TRUE(global.font_futoku);
  EXPECT_EQ(global.font_shadow, 3);
  EXPECT_TRUE(global.system.disable_wipe_anime);
  EXPECT_FALSE(global.system.skip_wipe_anime);
  EXPECT_EQ(global.global_extra_mode_flag[3], 2);
  EXPECT_FALSE(global.global_extra_switch_flag[1]);
  ASSERT_EQ(global.chrkoe.size(), 2);
  EXPECT_FALSE(global.chrkoe.at(1).enable);
  EXPECT_EQ(global.chrkoe.at(1).volume, 123);
  EXPECT_EQ(local.cursor_no, 5);
  EXPECT_EQ(local.mwnd_btn_disable.size(), 60);
  EXPECT_FALSE(local.syscommenu.read_skip.exist);
  EXPECT_TRUE(local.syscommenu.read_skip.enable);
  EXPECT_FALSE(local.syscommenu.local_extra_switch[0].enable);
  EXPECT_FALSE(local.syscommenu.local_extra_switch[0].onoff);
  EXPECT_FALSE(local.syscommenu.local_extra_mode[1].exist);
  EXPECT_EQ(local.syscommenu.local_extra_mode[1].mode, 2);
}

TEST(GameConfigTest, SiglusFontTypeSelectsBuiltinOrCustomFamily) {
  Gameexe gameexe;
  gameexe.SetStringAt("CONFIG.FONT.NAME", "Custom Font");
  EXPECT_EQ(MakeSiglusGlobalConfig(gameexe).font_name, "MS Gothic");
  gameexe.SetIntAt("CONFIG.FONT.TYPE", 1);
  EXPECT_EQ(MakeSiglusGlobalConfig(gameexe).font_name, "MS Mincho");
  gameexe.SetIntAt("CONFIG.FONT.TYPE", 2);
  EXPECT_EQ(MakeSiglusGlobalConfig(gameexe).font_name, "Meiryo");
  gameexe.SetIntAt("CONFIG.FONT.TYPE", 3);
  EXPECT_EQ(MakeSiglusGlobalConfig(gameexe).font_name, "Custom Font");
}

TEST(GameConfigTest, MessageOverrideTakesPrecedenceOverGlobalNowait) {
  LocalConfig local;
  GlobalConfig global;
  global.message_speed_nowait = true;
  EXPECT_EQ(EffectiveMessageSpeed(local, global), 0);
  local.msg_speed = 42;
  EXPECT_EQ(EffectiveMessageSpeed(local, global), 42);
  EXPECT_FALSE(EffectiveMessageNowait(local, global));
  local.msg_nowait = true;
  EXPECT_EQ(EffectiveMessageSpeed(local, global), 0);
  local.msg_nowait = false;
  local.msg_speed = -1;
  global.message_speed_nowait = false;
  EXPECT_EQ(EffectiveMessageSpeed(local, global), 20);
}

TEST(GameConfigTest, LocalAutoWaitOverridesApplyOnlyDuringLocalAutoMode) {
  LocalConfig local;
  GlobalConfig global;
  local.auto_mode_min_wait = 100;
  local.auto_mode_moji_wait = 10;
  EXPECT_EQ(EffectiveAutoTime(local, global, 3), 510);
  local.auto_mode = true;
  EXPECT_TRUE(EffectiveAutoMode(local, global));
  EXPECT_EQ(EffectiveAutoTime(local, global, 3), 130);
  local.auto_mode_moji_wait = -1;
  EXPECT_EQ(EffectiveAutoTime(local, global, 3), 310);
  local.auto_mode_min_wait = -2;
  local.auto_mode_moji_wait = -2;
  EXPECT_EQ(EffectiveAutoTime(local, global, 3), 510);
  local.auto_mode = false;
  global.enable_auto_mode = true;
  EXPECT_TRUE(EffectiveAutoMode(local, global));
  EXPECT_EQ(EffectiveAutoTime(local, global, 3), 510);
}

TEST(GameConfigTest, FontAndCursorOverridesRestoreGlobalPreferences) {
  LocalConfig local;
  GlobalConfig global;
  global.font_name = "Global font";
  global.font_futoku = true;
  global.font_shadow = 3;
  global.hide_idle_cursor = true;
  EXPECT_EQ(EffectiveFontName(local, global), "Global font");
  EXPECT_TRUE(EffectiveFontBold(local, global));
  EXPECT_EQ(EffectiveFontShadow(local, global), 3);
  EXPECT_TRUE(EffectiveMouseCursorHide(local, global));
  local.font_name = "Local font";
  local.font_futoku = 0;
  local.font_shadow = 0;
  local.mouse_cursor_hide_onoff = 0;
  local.mouse_cursor_hide_time = 123;
  EXPECT_EQ(EffectiveFontName(local, global), "Local font");
  EXPECT_FALSE(EffectiveFontBold(local, global));
  EXPECT_EQ(EffectiveFontShadow(local, global), 0);
  EXPECT_FALSE(EffectiveMouseCursorHide(local, global));
  EXPECT_EQ(EffectiveMouseCursorHideTime(local, global), 123);
  local.font_futoku = -2;
  local.font_shadow = -2;
  EXPECT_TRUE(EffectiveFontBold(local, global));
  EXPECT_EQ(EffectiveFontShadow(local, global), 3);
  global.hide_idle_cursor = false;
  local.mouse_cursor_hide_onoff = 2;
  EXPECT_FALSE(EffectiveMouseCursorHide(local, global));
  local = LocalConfig{};
  EXPECT_EQ(EffectiveFontName(local, global), "Global font");
  EXPECT_EQ(EffectiveMouseCursorHideTime(local, global), 5000);
}

TEST(GameConfigTest, ExplicitAnimationAndVoiceOnOverridesOff) {
  LocalConfig local;
  GlobalConfig global;
  local.disable_mwnd_anime = true;
  local.koe_dont_stop_off = true;
  global.system.continue_koe_playback = true;
  EXPECT_FALSE(EffectiveMwndAnimation(local, global));
  EXPECT_FALSE(EffectiveVoiceContinuation(local, global));
  local.force_mwnd_anime = true;
  local.koe_dont_stop_on = true;
  EXPECT_TRUE(EffectiveMwndAnimation(local, global));
  EXPECT_TRUE(EffectiveVoiceContinuation(local, global));
}
