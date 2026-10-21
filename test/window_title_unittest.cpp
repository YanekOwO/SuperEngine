// -----------------------------------------------------------------------
//
// This file is part of RLVM
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

#include "core/window_title.hpp"

#include "core/gameexe.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <tuple>

namespace {

using ::testing::MockFunction;
using ::testing::StrictMock;

class WindowTitleTest : public ::testing::Test {
 protected:
  StrictMock<MockFunction<void(const std::string&)>> publisher_;
  WindowTitle title_{publisher_.AsStdFunction()};
};

TEST_F(WindowTitleTest, ComposesVisibleSubtitleWithoutExtraSeparators) {
  for (const auto& [base, subtitle, visible, expected] : {
           std::tuple{"Game", "Scene", false, "Game"},
           std::tuple{"Game", "Scene", true, "Game: Scene"},
           std::tuple{"Game", "", true, "Game"},
           std::tuple{"", "Scene", true, "Scene"},
           std::tuple{"", "Scene", false, ""},
           std::tuple{"", "", true, ""},
           std::tuple{"ゲーム", "場面", true, "ゲーム: 場面"},
       }) {
    SCOPED_TRACE(expected);
    title_.base_title_ = base;
    title_.subtitle_ = subtitle;
    title_.subtitle_visible_ = visible;
    EXPECT_EQ(title_.GetTitle(), expected);
  }
}

TEST_F(WindowTitleTest, ApplyPublishesCurrentStateImmediately) {
  title_.base_title_ = "ゲーム";
  title_.subtitle_ = "場面";
  title_.subtitle_visible_ = true;
  EXPECT_CALL(publisher_, Call("ゲーム: 場面"));
  title_.Apply();

  title_.subtitle_visible_ = false;
  EXPECT_CALL(publisher_, Call("ゲーム"));
  title_.Apply();
}

TEST_F(WindowTitleTest, LoadRealliveDecodesCaptionAndClearsOldSubtitle) {
  Gameexe gexe;
  gexe.SetStringAt("CAPTION", "\x82\xa0");  // CP932 hiragana a.
  gexe.SetIntAt("SUBTITLE", 1);
  title_.subtitle_ = "old scene";

  title_.LoadReallive(gexe);

  EXPECT_EQ(title_.base_title_, "あ");
  EXPECT_TRUE(title_.subtitle_.empty());
  EXPECT_TRUE(title_.subtitle_visible_);
  EXPECT_EQ(title_.GetTitle(), "あ");
}

TEST_F(WindowTitleTest, LoadRealliveDefaultsSubtitleToHidden) {
  Gameexe gexe;
  gexe.SetStringAt("CAPTION", "Game");
  title_.subtitle_visible_ = true;
  title_.LoadReallive(gexe);
  EXPECT_FALSE(title_.subtitle_visible_);
}

TEST_F(WindowTitleTest, LoadSiglusDecodesCaptionAndDisablesSubtitle) {
  Gameexe gexe;
  gexe.SetStringAt("CAPTION", "\x82\xa0");
  gexe.SetIntAt("SUBTITLE", 1);
  title_.subtitle_ = "old scene";
  title_.subtitle_visible_ = true;

  title_.LoadSiglus(gexe);

  EXPECT_EQ(title_.base_title_, "あ");
  EXPECT_TRUE(title_.subtitle_.empty());
  EXPECT_FALSE(title_.subtitle_visible_);
}

TEST_F(WindowTitleTest, BothLoadersHonorNameEncodingWithoutPublishing) {
  Gameexe gexe;
  gexe.SetStringAt("CAPTION", "\x80");
  gexe.SetIntAt("NAME_ENC", 2);  // Western encoding: 0x80 is the euro sign.

  title_.LoadReallive(gexe);
  EXPECT_EQ(title_.GetTitle(), "€");
  title_.LoadSiglus(gexe);
  EXPECT_EQ(title_.GetTitle(), "€");
}

TEST_F(WindowTitleTest, InvalidNameEncodingValueFallsBackToCp932) {
  Gameexe gexe;
  gexe.SetStringAt("CAPTION", "\x82\xa0");
  gexe.SetStringAt("NAME_ENC", "invalid");

  title_.LoadReallive(gexe);
  EXPECT_EQ(title_.GetTitle(), "あ");
  title_.LoadSiglus(gexe);
  EXPECT_EQ(title_.GetTitle(), "あ");
}

TEST_F(WindowTitleTest, MissingCaptionPropagatesConfigurationError) {
  Gameexe gexe;
  EXPECT_THROW(title_.LoadReallive(gexe), std::runtime_error);
  EXPECT_THROW(title_.LoadSiglus(gexe), std::runtime_error);
}

}  // namespace
