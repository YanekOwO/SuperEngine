// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test_utils.hpp"

#include "core/avspec.hpp"
#include "systems/sdl/sound_implementor.hpp"
#include "systems/sound_system.hpp"

#include <filesystem>
#include <limits>
#include <memory>
#include <tuple>

avsample_buffer_t LoadForOutput(player_t player,
                                std::size_t output_samples,
                                const AVSpec& output_spec);

class FakeAudioImpl : public SDLSoundImpl {
 public:
  using super = SDLSoundImpl;

  FakeAudioImpl() = default;
  ~FakeAudioImpl() = default;

  using super::FromSDLSoundFormat;
  using super::ToSDLSoundFormat;
};
using ::testing::EndsWith;
using ::testing::Return;

namespace fs = std::filesystem;

class OvershootingFloatDecoder : public IAudioDecoder {
 public:
  std::string DecoderName() const override {
    return "OvershootingFloatDecoder";
  }

  AVSpec GetSpec() override {
    return {.sample_rate = 48000,
            .sample_format = AV_SAMPLE_FMT::FLT,
            .channel_count = 2};
  }

  AudioData DecodeAll() override {
    consumed_ = true;
    return AudioData{GetSpec(), samples_};
  }

  AudioData DecodeNext() override {
    consumed_ = true;
    return AudioData{GetSpec(), samples_};
  }

  bool HasNext() override { return !consumed_; }

  pcm_count_t Tell() override {
    return consumed_ ? static_cast<pcm_count_t>(samples_.size() / 2) : 0;
  }

 private:
  bool consumed_ = false;
  std::vector<avsample_flt_t> samples_{-1.004753f, 1.004753f};
};

TEST(SDLSound, SoundFormat) {
  auto aimpl = std::make_shared<FakeAudioImpl>();
  EXPECT_EQ(aimpl->ToSDLSoundFormat(AV_SAMPLE_FMT::U8), SDL_AUDIO_U8);
  EXPECT_EQ(aimpl->ToSDLSoundFormat(AV_SAMPLE_FMT::S8), SDL_AUDIO_S8);
  EXPECT_EQ(aimpl->ToSDLSoundFormat(AV_SAMPLE_FMT::S16), SDL_AUDIO_S16);
  EXPECT_EQ(aimpl->ToSDLSoundFormat(AV_SAMPLE_FMT::S32), SDL_AUDIO_S32);
  EXPECT_EQ(aimpl->ToSDLSoundFormat(AV_SAMPLE_FMT::FLT), SDL_AUDIO_F32);
  EXPECT_THROW(aimpl->ToSDLSoundFormat(AV_SAMPLE_FMT::S64),
               std::invalid_argument);
  EXPECT_THROW(aimpl->ToSDLSoundFormat(AV_SAMPLE_FMT::DBL),
               std::invalid_argument);
  EXPECT_THROW(aimpl->ToSDLSoundFormat(AV_SAMPLE_FMT::NONE),
               std::invalid_argument);

  EXPECT_EQ(aimpl->FromSDLSoundFormat(SDL_AUDIO_U8), AV_SAMPLE_FMT::U8);
  EXPECT_EQ(aimpl->FromSDLSoundFormat(SDL_AUDIO_S8), AV_SAMPLE_FMT::S8);
  EXPECT_EQ(aimpl->FromSDLSoundFormat(SDL_AUDIO_S16), AV_SAMPLE_FMT::S16);
  EXPECT_EQ(aimpl->FromSDLSoundFormat(SDL_AUDIO_S32), AV_SAMPLE_FMT::S32);
  EXPECT_EQ(aimpl->FromSDLSoundFormat(SDL_AUDIO_F32), AV_SAMPLE_FMT::FLT);

  EXPECT_THROW(aimpl->FromSDLSoundFormat(static_cast<SDL_AudioFormat>(0)),
               std::invalid_argument);
  EXPECT_THROW(aimpl->FromSDLSoundFormat(static_cast<SDL_AudioFormat>(12345)),
               std::invalid_argument);
}

TEST(SDLSound, LoadForOutputClampsFloatOvershoot) {
  auto decoder = std::make_shared<OvershootingFloatDecoder>();
  auto player = std::make_shared<AudioPlayer>(AudioDecoder(decoder));
  AVSpec output_spec{.sample_rate = 48000,
                     .sample_format = AV_SAMPLE_FMT::S16,
                     .channel_count = 2};

  avsample_buffer_t converted = LoadForOutput(player, 2, output_spec);

  ASSERT_TRUE(std::holds_alternative<std::vector<avsample_s16_t>>(converted));
  EXPECT_EQ(std::get<std::vector<avsample_s16_t>>(converted),
            (std::vector<avsample_s16_t>{
                std::numeric_limits<avsample_s16_t>::min(),
                std::numeric_limits<avsample_s16_t>::max()}));
}

TEST(SDLSound, CloseAudioWithoutOpenIsNoop) {
  FakeAudioImpl aimpl;
  EXPECT_NO_THROW(aimpl.CloseAudio());
  EXPECT_NO_THROW(aimpl.CloseAudio());
}

TEST(SDLSound, OpenCloseCycleIsRepeatable) {
  SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
  FakeAudioImpl aimpl;
  aimpl.InitSystem();
  const AVSpec spec{.sample_rate = 44100,
                    .sample_format = AV_SAMPLE_FMT::S16,
                    .channel_count = 2};
  for (int i = 0; i < 3; ++i) {
    aimpl.OpenAudio(spec, 4096);
    aimpl.AllocateChannels(8);
    aimpl.CloseAudio();
    aimpl.CloseAudio();
  }
  aimpl.QuitSystem();
}

TEST(SDLSound, DestructorClosesLeakedDevice) {
  SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
  const AVSpec spec{.sample_rate = 44100,
                    .sample_format = AV_SAMPLE_FMT::S16,
                    .channel_count = 2};
  {
    FakeAudioImpl aimpl;
    aimpl.InitSystem();
    aimpl.OpenAudio(spec, 4096);
    aimpl.AllocateChannels(8);
    // There is no CloseAudio call. This test simulates a SoundSystem
    // constructor that throws after it allocates the channels.
  }
  FakeAudioImpl aimpl2;
  aimpl2.OpenAudio(spec, 4096);
  aimpl2.AllocateChannels(8);
  aimpl2.CloseAudio();
  aimpl2.QuitSystem();
}
