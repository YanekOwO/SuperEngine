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

#include "core/game_config.hpp"
#include "headless_resource_host.hpp"
#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/siglus_runtime.hpp"

#include <gtest/gtest.h>

namespace {
using namespace libsiglus;
class SiglusSyscomDefaultsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    runtime.archive = std::make_shared<Archive>(
        std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
    runtime.gameexe = std::make_shared<Gameexe>();
    for (const auto* line :
         {"SCREEN_SIZE = 640,480", "MWND.CNT = 1", "WAKU.CNT = 0",
          "CONFIG.VOLUME.BGM = 140", "CONFIG.MESSAGE_SPEED = 27",
          "CONFIG.FILTER_COLOR = 1,2,3,76"})
      runtime.gameexe->parseLine(line);
    runtime.stage = std::make_unique<Stage>();
    runtime.global_config = MakeSiglusGlobalConfig(*runtime.gameexe);
    runtime.system = std::make_unique<System>(
        *runtime.gameexe, *runtime.stage, runtime.local_config,
        runtime.global_config, std::make_shared<AssetScanner>(),
        MwndConfig::ParseSiglus(*runtime.gameexe));
    runtime.binding_names = {"0_memory", "4_syscom"};
    runtime.GameplayMachine().Initialize();
  }
  void Run(const std::string& source) {
    binding::Execute(*runtime.GameplayMachine().vm, source);
  }
  ScopedDummyDrivers drivers;
  SiglusRuntime runtime;
};

TEST_F(SiglusSyscomDefaultsTest, AllVolumeResetsToDefault) {
  // Resetting a modified master volume restores the default volume.
  runtime.global_config.all_sound_user_volume = 0;
  Run("syscom.set_all_volume_default();");
  EXPECT_EQ(runtime.global_config.all_sound_user_volume, 255);
}

TEST_F(SiglusSyscomDefaultsTest, BgmVolumeResetsToDefault) {
  // Resetting a modified BGM volume restores the configured volume.
  auto& volume =
      runtime.global_config.sound_user_volume[SoundIndex(SoundCategory::Bgm)];
  volume = 0;
  Run("syscom.set_bgm_volume_default();");
  EXPECT_EQ(volume, 140);
}

TEST_F(SiglusSyscomDefaultsTest, KoeVolumeResetsToDefault) {
  // Resetting a modified voice volume restores the default volume.
  auto& volume =
      runtime.global_config.sound_user_volume[SoundIndex(SoundCategory::Voice)];
  volume = 0;
  Run("syscom.set_koe_volume_default();");
  EXPECT_EQ(volume, 255);
}

TEST_F(SiglusSyscomDefaultsTest, PcmVolumeResetsToDefault) {
  // Resetting a modified PCM volume restores the default volume.
  auto& volume =
      runtime.global_config.sound_user_volume[SoundIndex(SoundCategory::Pcm)];
  volume = 0;
  Run("syscom.set_pcm_volume_default();");
  EXPECT_EQ(volume, 255);
}

TEST_F(SiglusSyscomDefaultsTest, SeVolumeResetsToDefault) {
  // Resetting a modified sound-effect volume restores the default volume.
  auto& volume =
      runtime.global_config.sound_user_volume[SoundIndex(SoundCategory::Se)];
  volume = 0;
  Run("syscom.set_se_volume_default();");
  EXPECT_EQ(volume, 255);
}

TEST_F(SiglusSyscomDefaultsTest, MovVolumeResetsToDefault) {
  // Resetting a modified movie volume restores the default volume.
  auto& volume =
      runtime.global_config.sound_user_volume[SoundIndex(SoundCategory::Movie)];
  volume = 0;
  Run("syscom.set_mov_volume_default();");
  EXPECT_EQ(volume, 255);
}

TEST_F(SiglusSyscomDefaultsTest, AllOnOffResetsToDefault) {
  // Resetting disabled master audio restores its enabled default.
  runtime.global_config.play_all_sound_check = false;
  Run("syscom.set_all_onoff_default();");
  EXPECT_TRUE(runtime.global_config.play_all_sound_check);
}

TEST_F(SiglusSyscomDefaultsTest, BgmOnOffResetsToDefault) {
  // Resetting disabled BGM restores its enabled default.
  auto& enabled =
      runtime.global_config.play_sound_check[SoundIndex(SoundCategory::Bgm)];
  enabled = false;
  Run("syscom.set_bgm_onoff_default();");
  EXPECT_TRUE(enabled);
}

TEST_F(SiglusSyscomDefaultsTest, KoeOnOffResetsToDefault) {
  // Resetting disabled voice playback restores its enabled default.
  auto& enabled =
      runtime.global_config.play_sound_check[SoundIndex(SoundCategory::Voice)];
  enabled = false;
  Run("syscom.set_koe_onoff_default();");
  EXPECT_TRUE(enabled);
}

TEST_F(SiglusSyscomDefaultsTest, PcmOnOffResetsToDefault) {
  // Resetting disabled PCM playback restores its enabled default.
  auto& enabled =
      runtime.global_config.play_sound_check[SoundIndex(SoundCategory::Pcm)];
  enabled = false;
  Run("syscom.set_pcm_onoff_default();");
  EXPECT_TRUE(enabled);
}

TEST_F(SiglusSyscomDefaultsTest, SeOnOffResetsToDefault) {
  // Resetting disabled sound effects restores their enabled default.
  auto& enabled =
      runtime.global_config.play_sound_check[SoundIndex(SoundCategory::Se)];
  enabled = false;
  Run("syscom.set_se_onoff_default();");
  EXPECT_TRUE(enabled);
}

TEST_F(SiglusSyscomDefaultsTest, MovOnOffResetsToDefault) {
  // Resetting disabled movie audio restores its enabled default.
  auto& enabled =
      runtime.global_config.play_sound_check[SoundIndex(SoundCategory::Movie)];
  enabled = false;
  Run("syscom.set_mov_onoff_default();");
  EXPECT_TRUE(enabled);
}

TEST_F(SiglusSyscomDefaultsTest, BgmFadeVolumeResetsToDefault) {
  // Resetting a modified voice-ducking volume restores the default volume.
  runtime.global_config.bgmfade_volume = 0;
  Run("syscom.set_bgmfade_volume_default();");
  EXPECT_EQ(runtime.global_config.bgmfade_volume, 192);
}

TEST_F(SiglusSyscomDefaultsTest, BgmFadeOnOffResetsToDefault) {
  // Resetting disabled voice ducking restores its enabled default.
  runtime.global_config.bgmfade_use_check = false;
  Run("syscom.set_bgmfade_onoff_default();");
  EXPECT_TRUE(runtime.global_config.bgmfade_use_check);
}

TEST_F(SiglusSyscomDefaultsTest, MessageSpeedResetsToDefault) {
  // Resetting a modified message speed restores the configured delay.
  runtime.global_config.message_speed = 0;
  Run("syscom.set_message_speed_default();");
  EXPECT_EQ(runtime.global_config.message_speed, 27);
}

TEST_F(SiglusSyscomDefaultsTest, MessageNowaitResetsToDefault) {
  // Resetting enabled message nowait restores its disabled default.
  runtime.global_config.message_speed_nowait = true;
  Run("syscom.set_message_nowait_default();");
  EXPECT_FALSE(runtime.global_config.message_speed_nowait);
}

TEST_F(SiglusSyscomDefaultsTest, AutoModeMojiWaitResetsToDefault) {
  // Resetting a modified per-character auto wait restores the default delay.
  runtime.global_config.auto_mode_moji_wait = 0;
  Run("syscom.set_auto_mode_moji_wait_default();");
  EXPECT_EQ(runtime.global_config.auto_mode_moji_wait, 70);
}

TEST_F(SiglusSyscomDefaultsTest, AutoModeMinWaitResetsToDefault) {
  // Resetting a modified minimum auto wait restores the default delay.
  runtime.global_config.auto_mode_min_wait = 0;
  Run("syscom.set_auto_mode_min_wait_default();");
  EXPECT_EQ(runtime.global_config.auto_mode_min_wait, 300);
}

TEST_F(SiglusSyscomDefaultsTest, FilterColorAlphaResetsToDefault) {
  // Resetting a modified filter alpha restores the configured alpha.
  runtime.global_config.filter_color.set_alpha(0);
  Run("syscom.set_filter_color_a_default();");
  EXPECT_EQ(runtime.global_config.filter_color.a(), 76);
}
}  // namespace
