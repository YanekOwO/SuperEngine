// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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

#include "core/mwnd_config.hpp"

#include "core/gameexe.hpp"

#include <gtest/gtest.h>

TEST(MwndConfigTest, SiglusWakuObjectsUseLegacyDefaultAndExplicitCounts) {
  Gameexe gameexe;
  EXPECT_EQ(MwndConfig::ParseSiglus(gameexe).GetWaku(0).object_count, 1);
  gameexe.parseLine("WAKU.CNT = 2");
  EXPECT_EQ(MwndConfig::ParseSiglus(gameexe).GetWaku(1).object_count, 1);
  gameexe.parseLine("WAKU.OBJECT.CNT = 3");
  EXPECT_EQ(MwndConfig::ParseSiglus(gameexe).GetWaku(1).object_count, 3);
  gameexe.SetAt("WAKU.OBJECT.CNT", {0});
  EXPECT_EQ(MwndConfig::ParseSiglus(gameexe).GetWaku(0).object_count, 0);
  EXPECT_EQ(MwndConfig::ParseReallive(gameexe).GetWaku(0).object_count, 0);
}

TEST(MwndConfigTest, ParsesSiglusWithoutSynthesizingRealliveKeys) {
  Gameexe gameexe;
  gameexe.parseLine("SCREENSIZE_MOD = 999,1920,1080");
  gameexe.parseLine("MWND.CNT = 1");
  gameexe.parseLine("MWND.DEFAULT_MWND_NO = 0");
  gameexe.parseLine("MWND.DEFAULT_SEL_MWND_NO = 1");
  gameexe.parseLine("MWND.000.NOVEL_MODE = 1");
  gameexe.parseLine("MWND.000.WINDOW_POS = 12,34");
  gameexe.parseLine("MWND.000.WINDOW_SIZE = 800,600");
  gameexe.parseLine("MWND.000.MESSAGE_POS = 20,30");
  gameexe.parseLine("MWND.000.MOJI_CNT = 10,4");
  gameexe.parseLine("MWND.000.MOJI_SIZE = 30");
  gameexe.parseLine("MWND.000.MOJI_SPACE = 2,3");
  gameexe.parseLine("MWND.000.RUBY_SIZE = 5");
  gameexe.parseLine("MWND.000.WAKU_NO = 3");
  gameexe.parseLine("WAKU.CNT = 4");
  gameexe.parseLine("WAKU.BTN.CNT = 1");
  gameexe.parseLine("WAKU.FACE.CNT = 1");
  gameexe.parseLine("WAKU.003.EXTEND_TYPE = 0");
  gameexe.parseLine("WAKU.003.WAKU_FILE = frame");
  gameexe.parseLine("WAKU.003.FILTER_FILE = mask");
  gameexe.parseLine("WAKU.003.ICON_NO = 0");
  gameexe.parseLine("WAKU.003.BTN.000.FILE = button");
  gameexe.parseLine("WAKU.003.BTN.000.POS = 3,4,5");
  gameexe.parseLine("WAKU.003.BTN.000.ACTION = 2");
  gameexe.parseLine("WAKU.003.BTN.000.TYPE = auto_mode,1");
  gameexe.parseLine("WAKU.003.BTN.000.CALL = _00COM,$$button");
  gameexe.parseLine("WAKU.003.FACE.000.POS = 7,8");
  gameexe.parseLine("ICON.CNT = 1");
  gameexe.parseLine("ICON.000.FILE = wait_icon");
  gameexe.parseLine("ICON.000.CNT = 12");
  gameexe.parseLine("ICON.000.SPEED = 50");
  gameexe.parseLine("BUTTON.SE.CNT = 1");
  gameexe.parseLine("BUTTON.SE.000.HIT = 4");
  gameexe.parseLine("BUTTON.SE.000.PUSH = 5");
  gameexe.parseLine("BUTTON.SE.000.DECIDE = 6");

  const MwndConfig config = MwndConfig::ParseSiglus(gameexe);

  EXPECT_EQ(config.default_window(), 0);
  EXPECT_EQ(config.default_selection_window(), 1);
  EXPECT_TRUE(config.HasWindow(1));
  const auto& window = config.GetWindow(0);
  EXPECT_EQ(window.x_distance_from_origin, 12);
  EXPECT_EQ(window.y_distance_from_origin, 34);
  EXPECT_EQ(window.action_on_pause, 1);
  EXPECT_EQ(window.waku_set, 3);
  EXPECT_EQ(window.layout.GetNormalSize(), Size(320, 152));

  const auto& waku = config.GetWaku(3);
  EXPECT_EQ(waku.main_file, "frame");
  EXPECT_EQ(waku.filter_file, "mask");
  ASSERT_EQ(waku.buttons.size(), 1);
  EXPECT_EQ(waku.buttons[0].position_base, 3);
  EXPECT_EQ(waku.buttons[0].slot_id, 0);
  EXPECT_EQ(waku.buttons[0].se_no, 0);
  EXPECT_EQ(waku.buttons[0].mode, 1);
  EXPECT_EQ(waku.buttons[0].required_mode, 1);
  EXPECT_EQ(waku.buttons[0].action, MwndConfig::ButtonAction::AutoMode);
  ASSERT_TRUE(waku.buttons[0].call);
  EXPECT_EQ(waku.buttons[0].call->scene, "_00COM");
  EXPECT_EQ(waku.buttons[0].call->command, "$$button");
  ASSERT_EQ(waku.face_positions.size(), 1);
  EXPECT_EQ(waku.face_positions[0], Point(7, 8));

  ASSERT_NE(config.GetIcon(0), nullptr);
  EXPECT_EQ(config.GetIcon(0)->file, "wait_icon");
  EXPECT_EQ(config.GetIcon(0)->pattern_count, 12);
  ASSERT_NE(config.GetButtonSound(0), nullptr);
  EXPECT_EQ(*config.GetButtonSound(0),
            (MwndConfig::ButtonSound{.hit = 4, .push = 5, .decide = 6}));
  EXPECT_FALSE(gameexe.Exists("WINDOW.000.MOJI_SIZE"));
  EXPECT_FALSE(gameexe.Exists("DEFAULT_SEL_WINDOW"));
}

TEST(MwndConfigTest, SiglusButtonSoundsUseLegacyDefaults) {
  Gameexe gameexe;
  gameexe.parseLine("BUTTON.SE.CNT = 2");

  const MwndConfig config = MwndConfig::ParseSiglus(gameexe);

  ASSERT_NE(config.GetButtonSound(1), nullptr);
  EXPECT_EQ(*config.GetButtonSound(1),
            (MwndConfig::ButtonSound{.hit = 0, .push = -1, .decide = 1}));
  EXPECT_EQ(config.GetButtonSound(-1), nullptr);
  EXPECT_EQ(config.GetButtonSound(2), nullptr);
}

TEST(MwndConfigTest, SiglusFilterUsesConfiguredColourAndOpacity) {
  Gameexe gameexe;
  gameexe.parseLine("CONFIG.FILTER_COLOR = 11,22,33,76");
  gameexe.parseLine("WINDOW_ATTR = 255,255,255,255,1");
  gameexe.parseLine("WAKU.CNT = 1");
  gameexe.parseLine("WAKU.000.FILTER_FILE = mw_fil");
  gameexe.parseLine("WAKU.000.FILTER_COLOR = 44,55,66,128");
  gameexe.parseLine("WAKU.000.FILTER_CONFIG_COLOR = 0");
  gameexe.parseLine("WAKU.000.FILTER_CONFIG_TR = 1");

  const MwndConfig config = MwndConfig::ParseSiglus(gameexe);

  EXPECT_EQ(config.window_attr(), (std::vector<int>{11, 22, 33, 76, 0}));
  EXPECT_EQ(config.GetWindow(0).colour, RGBAColour(11, 22, 33, 76));
  EXPECT_EQ(config.GetWindow(1).colour, RGBAColour(11, 22, 33, 76));
  EXPECT_FALSE(config.GetWindow(0).is_filter);
  const auto& waku = config.GetWaku(0);
  EXPECT_EQ(waku.filter_rendering, MwndConfig::FilterRendering::AlphaBlend);
  EXPECT_EQ(waku.filter_colour, RGBAColour(44, 55, 66, 128));
  EXPECT_FALSE(waku.use_config_colour);
  EXPECT_TRUE(waku.use_config_opacity);
}

TEST(MwndConfigTest, SiglusFiltersUseLegacyDefaults) {
  Gameexe gameexe;
  gameexe.parseLine("WAKU.CNT = 1");

  const MwndConfig config = MwndConfig::ParseSiglus(gameexe);

  EXPECT_EQ(config.window_attr(), (std::vector<int>{0, 0, 0, 128, 0}));
  EXPECT_EQ(config.GetWindow(0).colour, RGBAColour(0, 0, 0, 128));
  const auto& waku = config.GetWaku(0);
  EXPECT_EQ(waku.filter_rendering, MwndConfig::FilterRendering::AlphaBlend);
  EXPECT_EQ(waku.filter_colour, RGBAColour(0, 0, 255, 128));
  EXPECT_TRUE(waku.use_config_colour);
  EXPECT_TRUE(waku.use_config_opacity);
}

TEST(MwndConfigTest, SiglusFilterColourIsValidated) {
  Gameexe gameexe;
  gameexe.SetAt("CONFIG.FILTER_COLOR", {-1, 256, 33, -4, 1});

  const MwndConfig clamped = MwndConfig::ParseSiglus(gameexe);

  EXPECT_EQ(clamped.window_attr(), (std::vector<int>{0, 255, 33, 0, 0}));
  EXPECT_EQ(clamped.GetWindow(0).colour, RGBAColour(0, 255, 33, 0));

  gameexe.SetAt("CONFIG.FILTER_COLOR", {11, 22, 33});
  const MwndConfig malformed = MwndConfig::ParseSiglus(gameexe);

  EXPECT_EQ(malformed.window_attr(), (std::vector<int>{0, 0, 0, 128, 0}));
  EXPECT_EQ(malformed.GetWindow(0).colour, RGBAColour(0, 0, 0, 128));
  EXPECT_EQ(malformed.GetWaku(0).filter_rendering,
            MwndConfig::FilterRendering::AlphaBlend);
}

TEST(MwndConfigTest, SiglusFilterConfigurationFlagsAreIndependent) {
  Gameexe gameexe;
  gameexe.parseLine("WAKU.CNT = 1");
  gameexe.parseLine("WAKU.000.FILTER_CONFIG_COLOR = 1");
  gameexe.parseLine("WAKU.000.FILTER_CONFIG_TR = 0");

  const MwndConfig config = MwndConfig::ParseSiglus(gameexe);

  EXPECT_TRUE(config.GetWaku(0).use_config_colour);
  EXPECT_FALSE(config.GetWaku(0).use_config_opacity);
}

TEST(MwndConfigTest, ParsesRealliveWindowWakuAndFarcall) {
  Gameexe gameexe;
  gameexe.parseLine("WINDOW_ATTR = 1,2,3,4,1");
  gameexe.parseLine("COLOR_TABLE.000 = 9,8,7");
  gameexe.parseLine("WINDOW.000.MOJI_SIZE = 20");
  gameexe.parseLine("WINDOW.000.MOJI_CNT = 5,2");
  gameexe.parseLine("WINDOW.000.MOJI_REP = 1,2");
  gameexe.parseLine("WINDOW.000.LUBY_SIZE = 3");
  gameexe.parseLine("WINDOW.000.MOJI_POS = 4,5,6,7");
  gameexe.parseLine("WINDOW.000.POS = 0,10,11");
  gameexe.parseLine("WINDOW.000.KEYCUR_MOD = 2,12,13");
  gameexe.parseLine("WINDOW.000.WAKU_SETNO = 2");
  gameexe.parseLine("WAKU.002.TYPE = 5");
  gameexe.parseLine("WAKU.002.000.NAME = main");
  gameexe.parseLine("WAKU.002.000.BACK = back");
  gameexe.parseLine("WAKU.002.000.BTN = buttons");
  gameexe.parseLine("WAKU.002.000.EXBTN_000_BOX = 0,1,2,30,40");
  gameexe.parseLine("WBCALL.000 = 7,8");

  const MwndConfig config = MwndConfig::ParseReallive(gameexe);

  EXPECT_EQ(config.window_attr(), (std::vector<int>{1, 2, 3, 4, 1}));
  const auto& window = config.GetWindow(0);
  EXPECT_EQ(window.colour, RGBAColour(1, 2, 3, 4));
  EXPECT_TRUE(window.is_filter);
  EXPECT_EQ(window.layout.GetNormalSize(), Size(105, 50));
  EXPECT_EQ(window.default_colour, RGBColour(9, 8, 7));
  EXPECT_EQ(window.waku_set, 2);
  EXPECT_EQ(window.keycursor_pos, Point(12, 13));

  const auto& waku = config.GetWaku(2);
  EXPECT_EQ(waku.filter_rendering, MwndConfig::FilterRendering::ColorMask);
  EXPECT_EQ(waku.main_file, "main");
  EXPECT_EQ(waku.filter_file, "back");
  ASSERT_EQ(waku.buttons.size(), 1);
  EXPECT_EQ(waku.buttons[0].action, MwndConfig::ButtonAction::RealliveFarcall);
  ASSERT_TRUE(waku.buttons[0].call);
  EXPECT_EQ(waku.buttons[0].call->scene, "7");
  EXPECT_EQ(waku.buttons[0].call->entrypoint, 8);
}

TEST(MwndConfigTest, SiglusWindowOrderAndCapacityAreExplicit) {
  Gameexe gameexe;
  const auto defaults = MwndConfig::ParseSiglus(gameexe);
  EXPECT_TRUE(defaults.layered_windows());
  EXPECT_EQ(defaults.order_, 1);
  gameexe.parseLine("MWND.ORDER = -3");
  gameexe.parseLine("MWND.CNT = 4");
  const auto config = MwndConfig::ParseSiglus(gameexe);
  EXPECT_EQ(config.order_, -3);
  EXPECT_EQ(config.window_capacity(), 4);
  EXPECT_FALSE(MwndConfig::ParseReallive(gameexe).layered_windows());
}
