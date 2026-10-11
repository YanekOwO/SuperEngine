// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------

#include <gtest/gtest.h>

#include "core/gameexe.hpp"
#include "systems/text_system.hpp"

namespace {

TEST(TextSystemGlobalsTest, SiglusUsesConfiguredFilterOpacity) {
  Gameexe gexe;
  gexe.parseLine("CONFIG.FILTER_COLOR = 11,22,33,76");
  gexe.parseLine("WINDOW_ATTR = 255,255,255,255,1");
  const auto config = MwndConfig::ParseSiglus(gexe);

  const TextSystemGlobals globals(gexe, config);

  EXPECT_EQ(globals.window_attr, (std::vector<int>{11, 22, 33, 76, 0}));
}

TEST(TextSystemGlobalsTest, SiglusUsesDefaultFilterOpacity) {
  Gameexe gexe;
  const auto config = MwndConfig::ParseSiglus(gexe);

  const TextSystemGlobals globals(gexe, config);

  EXPECT_EQ(globals.window_attr, (std::vector<int>{0, 0, 0, 128, 0}));
}

TEST(TextSystemGlobalsTest, RealliveRetainsWindowAttributes) {
  Gameexe gexe;
  gexe.parseLine("WINDOW_ATTR = 1,2,3,4,1,7");
  const auto config = MwndConfig::ParseReallive(gexe);

  const TextSystemGlobals globals(gexe, config);

  EXPECT_EQ(globals.window_attr, (std::vector<int>{1, 2, 3, 4, 1, 7}));
}

TEST(TextSystemGlobalsTest, RealliveRetainsDefaultForMalformedAttributes) {
  Gameexe gexe;
  gexe.parseLine("WINDOW_ATTR = 1,2,3,4");
  const auto config = MwndConfig::ParseReallive(gexe);

  const TextSystemGlobals globals(gexe, config);

  EXPECT_EQ(globals.window_attr, (std::vector<int>{255, 255, 255, 255, 0}));
}

TEST(TextSystemKoeReplayConfigTest, MissingBlockIsOptional) {
  Gameexe gexe;

  EXPECT_FALSE(text_system_detail::ReadKoeReplayConfig(gexe));
}

TEST(TextSystemKoeReplayConfigTest, MissingNameIsOptional) {
  Gameexe gexe;
  gexe.SetAt("KOEREPLAYICON.REPPOS", {12, 34});

  EXPECT_FALSE(text_system_detail::ReadKoeReplayConfig(gexe));
}

TEST(TextSystemKoeReplayConfigTest, MissingRepposUsesZeroOffset) {
  Gameexe gexe;
  gexe.SetStringAt("KOEREPLAYICON.NAME", "koe_replay");

  auto config = text_system_detail::ReadKoeReplayConfig(gexe);

  ASSERT_TRUE(config);
  EXPECT_EQ("koe_replay", config->icon_name);
  EXPECT_EQ(0, config->x_offset);
  EXPECT_EQ(0, config->y_offset);
}

TEST(TextSystemKoeReplayConfigTest, MalformedRepposUsesZeroOffset) {
  Gameexe gexe;
  gexe.SetStringAt("KOEREPLAYICON.NAME", "koe_replay");
  gexe.SetStringAt("KOEREPLAYICON.REPPOS", "bad");

  auto config = text_system_detail::ReadKoeReplayConfig(gexe);

  ASSERT_TRUE(config);
  EXPECT_EQ("koe_replay", config->icon_name);
  EXPECT_EQ(0, config->x_offset);
  EXPECT_EQ(0, config->y_offset);
}

TEST(TextSystemKoeReplayConfigTest, ValidConfigLoadsIconAndOffset) {
  Gameexe gexe;
  gexe.SetStringAt("KOEREPLAYICON.NAME", "koe_replay");
  gexe.SetAt("KOEREPLAYICON.REPPOS", {12, 34});

  auto config = text_system_detail::ReadKoeReplayConfig(gexe);

  ASSERT_TRUE(config);
  EXPECT_EQ("koe_replay", config->icon_name);
  EXPECT_EQ(12, config->x_offset);
  EXPECT_EQ(34, config->y_offset);
}

}  // namespace
