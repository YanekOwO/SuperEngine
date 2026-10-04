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
//
// -----------------------------------------------------------------------

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::_;
using ::testing::Return;

#include "core/audio_player.hpp"
#include "core/avdec/audio_decoder.hpp"

#include <atomic>
#include <future>
#include <limits>
#include <random>

constexpr auto sample_rate = 44100;
constexpr auto frequency = 440;
constexpr auto duration = 0.2;
constexpr auto channel_count = 2;

constexpr auto tot_samples = sample_rate * duration * channel_count;

class NoiseGenerator : public IAudioDecoder {
 public:
  NoiseGenerator()
      : position_(0),
        sample_rate_(sample_rate),
        frequency_(frequency),
        total_samples_(sample_rate_ * duration * channel_count) {
    std::generate_n(std::back_inserter(buffer_), total_samples_, []() {
      static std::mt19937 rng;
      return static_cast<float>(rng()) /
             static_cast<float>(std::mt19937::max());
    });
  }

  std::string DecoderName() const override { return "Noise Generator"; }

  AVSpec GetSpec() override {
    return {.sample_rate = sample_rate_,
            .sample_format = AV_SAMPLE_FMT::FLT,
            .channel_count = channel_count};
  }

  AudioData DecodeAll() override { return AudioData{GetSpec(), buffer_}; }

  AudioData DecodeNext() override {
    if (++decode_count_ > max_decode_calls_)
      throw std::runtime_error("DecodeNext call limit exceeded");

    static constexpr int chunk_size = 1024;
    int end_position = std::min(position_ + chunk_size, total_samples_);

    std::vector<avsample_flt_t> chunk(buffer_.begin() + position_,
                                      buffer_.begin() + end_position);
    position_ = end_position;

    return AudioData{GetSpec(), std::move(chunk)};
  }

  bool HasNext() override { return position_ < total_samples_; }

  SEEK_RESULT Seek(pcm_count_t offset, SEEKDIR whence = SEEKDIR::CUR) override {
    offset *= channel_count;

    if (whence == SEEKDIR::BEG) {
      if (offset >= 0 && offset < total_samples_) {
        position_ = offset;
        return SEEK_RESULT::PRECISE_SEEK;
      }
    } else if (whence == SEEKDIR::CUR) {
      pcm_count_t new_pos = position_ + offset;
      if (new_pos >= 0 && new_pos < total_samples_) {
        position_ = new_pos;
        return SEEK_RESULT::PRECISE_SEEK;
      }
    } else if (whence == SEEKDIR::END) {
      pcm_count_t new_pos = total_samples_ + offset;
      if (new_pos >= 0 && new_pos < total_samples_) {
        position_ = new_pos;
        return SEEK_RESULT::PRECISE_SEEK;
      }
    }
    return SEEK_RESULT::ERROR;
  }

  pcm_count_t Tell() override { return position_ / channel_count; }

  std::vector<avsample_flt_t> buffer_;
  pcm_count_t position_;
  size_t decode_count_ = 0;
  size_t max_decode_calls_ = std::numeric_limits<size_t>::max();
  const int sample_rate_;
  const double frequency_;
  const pcm_count_t total_samples_;
};

static double Deviation(const std::vector<float>& a,
                        const std::vector<float>& b) {
  double var = 0;
  auto sqr = [](double x) { return x * x; };
  auto n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i)
    var += sqr(a[i] - b[i]) / n;
  return std::sqrt(var);
}

static long long GetTicks(double time_s) {
  return static_cast<long long>(time_s * 1000);
}

class AudioPlayerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    decoder = std::make_shared<NoiseGenerator>();
    player = std::make_unique<AudioPlayer>(AudioDecoder(decoder));
  }

  std::shared_ptr<NoiseGenerator> decoder;
  std::unique_ptr<AudioPlayer> player;
};

TEST_F(AudioPlayerTest, LoadPCM) {
  const int samples_quarter = sample_rate * channel_count * duration / 4;

  for (int i = 0; i < 4; ++i) {
    auto result = player->LoadPCM(samples_quarter);
    ASSERT_EQ(result.SampleCount(), samples_quarter);
    EXPECT_EQ(player->GetCurrentTime(), GetTicks(duration * (i + 1) / 4.0));

    auto expect = std::vector<float>(
        decoder->buffer_.begin() + i * samples_quarter,
        decoder->buffer_.begin() + (i + 1) * samples_quarter);
    auto actual = std::get<std::vector<float>>(result.data);
    EXPECT_LE(Deviation(expect, actual), 1e-4);
  }

  EXPECT_FALSE(player->IsLoopingEnabled());
  EXPECT_FALSE(player->IsPlaying());

  auto result = player->LoadPCM(samples_quarter * 10);
  EXPECT_LE(Deviation(std::get<std::vector<float>>(result.data),
                      std::vector<float>(samples_quarter * 10)),
            1e-4);
}

TEST_F(AudioPlayerTest, LoadInvalid) {
  EXPECT_THROW(player->LoadPCM(0), std::invalid_argument);
  EXPECT_THROW(player->LoadPCM(-123), std::invalid_argument);
}

TEST_F(AudioPlayerTest, LoadAll) {
  const int samples = sample_rate * channel_count * duration;
  auto result = player->LoadPCM(samples - 3);
  EXPECT_NO_THROW(result.Append(player->LoadRemain()));
  EXPECT_EQ(result.SampleCount(), decoder->buffer_.size());
  EXPECT_LE(
      Deviation(std::get<std::vector<float>>(result.data), decoder->buffer_),
      1e-4);
}

TEST_F(AudioPlayerTest, LoopPlayback) {
  player->SetLoopTimes(2);
  ASSERT_TRUE(player->IsLoopingEnabled());

  auto result = player->LoadPCM(tot_samples * 1.5);
  EXPECT_EQ(player->GetCurrentTime(), GetTicks(duration * 0.5));

  EXPECT_TRUE(player->IsPlaying());
  result.Append(player->LoadPCM(tot_samples));
  EXPECT_EQ(player->GetCurrentTime(), GetTicks(duration * 0.5));

  result.Append(player->LoadPCM(tot_samples * 0.5));
  // EXPECT_FALSE(player->IsPlaying());
  // EXPECT_EQ(player->GetCurrentTime(), GetTicks(duration));

  auto expect = decoder->buffer_;
  expect.insert(expect.end(), decoder->buffer_.cbegin(),
                decoder->buffer_.cend());
  expect.insert(expect.end(), decoder->buffer_.cbegin(),
                decoder->buffer_.cend());
  EXPECT_EQ(expect.size(), result.SampleCount());
  EXPECT_LE(Deviation(std::get<std::vector<float>>(result.data), expect), 1e-4);
}

TEST_F(AudioPlayerTest, Fadein) {
  const float fadein_ms = duration * 1000 / 2.0;
  player->FadeIn(fadein_ms);

  auto result = player->LoadPCM(tot_samples);
  auto first =
      std::get<std::vector<float>>(result.Slice(0, tot_samples / 2).data);
  auto second = std::get<std::vector<float>>(
      result.Slice(tot_samples / 2, tot_samples).data);

  auto actual = std::vector<float>(decoder->buffer_.begin(),
                                   decoder->buffer_.begin() + tot_samples / 2);
  float last_volume = -0.01, current_volume = 0;
  ASSERT_EQ(actual.size(), first.size());
  for (size_t i = 0; i < actual.size(); ++i) {
    if (fabs(actual[i]) < 1e-5) {  // avoid divide by zero
      EXPECT_LT(fabs(first[i]), 1e-5);
      continue;
    }

    current_volume = first[i] / actual[i];
    EXPECT_GT(current_volume, last_volume);
    last_volume = current_volume;
  }

  actual = std::vector<float>(decoder->buffer_.begin() + tot_samples / 2,
                              decoder->buffer_.end());
  EXPECT_LE(Deviation(actual, second), 1e-4);
}

TEST_F(AudioPlayerTest, Fadeout) {
  const float fadeout_ms = duration * 1000 / 2.0;
  player->FadeOut(fadeout_ms);
  EXPECT_TRUE(player->IsPlaying());

  const auto fadeout_samples = fadeout_ms * sample_rate * channel_count / 1000;
  auto result =
      std::get<std::vector<float>>(player->LoadPCM(fadeout_samples).data);
  EXPECT_FALSE(player->IsPlaying());

  float last_volume = 1.01, current_volume = 0;
  for (size_t i = 0; i < result.size(); ++i) {
    const auto& actual = decoder->buffer_;
    if (fabs(actual[i]) < 1e-5) {
      EXPECT_LT(fabs(result[i]), 1e-5);
      continue;
    }

    current_volume = result[i] / actual[i];
    EXPECT_LT(current_volume, last_volume);
    last_volume = current_volume;
  }
}

TEST_F(AudioPlayerTest, LoopingRewind) {
  player->SetLoopTimes(2);
  ASSERT_TRUE(player->IsLoopingEnabled());

  auto result = player->LoadPCM(tot_samples * 2);

  EXPECT_EQ(player->GetCurrentTime(), GetTicks(0.0));
  EXPECT_TRUE(player->IsPlaying());
}

TEST_F(AudioPlayerTest, TerminateLoop) {
  player->SetLoopTimes(10);
  auto result = player->LoadPCM(tot_samples * 2 - 5);
  EXPECT_TRUE(player->IsPlaying());

  player->SetLoopTimes(0);
  result.Append(player->LoadRemain());
  EXPECT_EQ(player->LoadRemain().SampleCount(), 0);

  EXPECT_EQ(result.SampleCount(), tot_samples * 2);
  auto expect = decoder->buffer_;
  expect.insert(expect.end(), decoder->buffer_.begin(), decoder->buffer_.end());

  EXPECT_LE(Deviation(std::get<std::vector<float>>(result.data), expect), 1e-4);
  EXPECT_FALSE(player->IsPlaying());
}

TEST_F(AudioPlayerTest, LoadRemainStopsAtLoopBoundary) {
  player->SetLoopTimes(-1);

  auto first = player->LoadRemain();
  ASSERT_EQ(first.SampleCount(), tot_samples);
  EXPECT_TRUE(player->IsPlaying());
  EXPECT_EQ(player->GetCurrentTime(), GetTicks(0.0));

  auto second = player->LoadRemain();
  ASSERT_EQ(second.SampleCount(), tot_samples);
  EXPECT_TRUE(player->IsPlaying());

  EXPECT_LE(Deviation(std::get<std::vector<float>>(first.data),
                      decoder->buffer_),
            1e-4);
  EXPECT_LE(Deviation(std::get<std::vector<float>>(second.data),
                      decoder->buffer_),
            1e-4);
}

TEST_F(AudioPlayerTest, StartTerminated) {
  EXPECT_TRUE(player->IsPlaying());

  player->Terminate();
  EXPECT_FALSE(player->IsPlaying());

  auto result = std::get<std::vector<float>>(player->LoadPCM(tot_samples).data);
  EXPECT_EQ(result.size(), tot_samples);
  EXPECT_LE(Deviation(result, std::vector<float>(tot_samples)), 1e-4);
  EXPECT_FALSE(player->IsPlaying());
}

TEST_F(AudioPlayerTest, Ploop) {
  const auto quarter_samples = sample_rate * channel_count * duration / 4;
  auto result = player->LoadPCM(2 * quarter_samples);
  player->SetLoop(2 * quarter_samples / channel_count,
                  3 * quarter_samples / channel_count);
  result.Append(player->LoadPCM(2 * quarter_samples));

  std::vector<float> expect(decoder->buffer_.cbegin(),
                            decoder->buffer_.cbegin() + 3 * quarter_samples);
  expect.insert(expect.end(), decoder->buffer_.cbegin() + 2 * quarter_samples,
                decoder->buffer_.cbegin() + 3 * quarter_samples);
  EXPECT_LE(Deviation(std::get<std::vector<float>>(result.data), expect), 1e-4);
}

TEST_F(AudioPlayerTest, ABloop) {
  const auto quarter_samples = sample_rate * channel_count * duration / 4;
  std::ignore = player->LoadPCM(quarter_samples);

  player->SetLoop(2 * quarter_samples / channel_count,
                  3 * quarter_samples / channel_count);
  ASSERT_TRUE(player->IsPlaying());
  EXPECT_EQ(player->GetCurrentTime(), GetTicks(duration / 2.0))
      << "Setting an AB loop should move the playback location to the "
         "beginning of the loop, when it is outside looping range.";

  auto result = player->LoadPCM(quarter_samples);
  std::vector<float> expect(decoder->buffer_.cbegin() + 2 * quarter_samples,
                            decoder->buffer_.cbegin() + 3 * quarter_samples);
  EXPECT_LE(Deviation(std::get<std::vector<float>>(result.data), expect), 1e-4);
}

TEST_F(AudioPlayerTest, PausePlayback) {
  const auto tot_samples = sample_rate * channel_count * duration;

  auto result = player->LoadPCM(tot_samples * 5 / 8);
  player->Pause();
  EXPECT_EQ(player->GetStatus(), AudioPlayer::STATUS::PAUSED);

  auto silent = player->LoadPCM(tot_samples * 3 / 8);
  EXPECT_EQ(silent.SampleCount(), tot_samples * 3 / 8);
  EXPECT_LE(Deviation(std::get<std::vector<float>>(silent.data),
                      std::vector<float>(tot_samples * 3 / 8)),
            1e-4);
  EXPECT_EQ(player->LoadRemain().SampleCount(), 0);

  player->Unpause();
  EXPECT_TRUE(player->IsPlaying());
  result.Append(player->LoadPCM(tot_samples * 3 / 8));
}

TEST_F(AudioPlayerTest, AujustVolume) {
  const auto quarter_samples = sample_rate * channel_count * duration / 4;
  player->SetLoop(quarter_samples / channel_count,
                  quarter_samples * 2 / channel_count);
  std::vector<float> orig_pcm(decoder->buffer_.begin() + quarter_samples,
                              decoder->buffer_.begin() + quarter_samples * 2);
  for (int i = 0; i < 4; ++i) {
    float volume = static_cast<float>(i) / 4.0f;
    player->SetVolume(volume);
    auto pcm =
        std::get<std::vector<float>>(player->LoadPCM(quarter_samples).data);
    EXPECT_EQ(player->GetVolume(), volume);

    for (int j = 0; j < quarter_samples; ++j)
      EXPECT_NEAR(pcm[j], orig_pcm[j] * volume, 1e-4);
  }
}

TEST_F(AudioPlayerTest, SetPLoop) {
  const auto quarter_samples = sample_rate * channel_count * duration / 4;
  player->SetPLoop(quarter_samples / channel_count,
                   3 * quarter_samples / channel_count,
                   2 * quarter_samples / channel_count);
  player->SetLoopTimes(0);
  auto result = player->LoadPCM(3 * quarter_samples);
  std::vector<float> expect(decoder->buffer_.begin() + quarter_samples,
                            decoder->buffer_.begin() + 3 * quarter_samples);
  expect.resize(3 * quarter_samples);

  ASSERT_EQ(result.SampleCount(), expect.size());
  EXPECT_LE(Deviation(std::get<std::vector<float>>(result.data), expect), 1e-4);
}

TEST_F(AudioPlayerTest, SetPLoopWithOpenEndedTrack) {
  static constexpr size_t requested_samples = 512;
  decoder->max_decode_calls_ = 1;

  player->SetPLoop(0, std::numeric_limits<size_t>::max(), 0);
  auto result = player->LoadPCM(requested_samples);

  ASSERT_EQ(result.SampleCount(), requested_samples);
  EXPECT_EQ(decoder->decode_count_, 1);
  std::vector<float> expect(decoder->buffer_.begin(),
                            decoder->buffer_.begin() + requested_samples);
  EXPECT_LE(Deviation(std::get<std::vector<float>>(result.data), expect), 1e-4);
}

TEST_F(AudioPlayerTest, PlayPLoop) {
  const auto quarter_samples = sample_rate * channel_count * duration / 4;
  player->SetPLoop(quarter_samples / channel_count,
                   3 * quarter_samples / channel_count,
                   2 * quarter_samples / channel_count);
  player->SetLoopTimes(3);
  auto result = player->LoadPCM(2 * quarter_samples - 6);
  result.Append(player->LoadPCM(quarter_samples + 6));
  std::vector<float> expect(decoder->buffer_.begin() + quarter_samples,
                            decoder->buffer_.begin() + 3 * quarter_samples);
  expect.insert(expect.end(), decoder->buffer_.begin() + 2 * quarter_samples,
                decoder->buffer_.begin() + 3 * quarter_samples);

  ASSERT_EQ(result.SampleCount(), expect.size());
  EXPECT_LE(Deviation(std::get<std::vector<float>>(result.data), expect), 1e-4);
}

class APlayerConcurrentTest : public ::testing::Test {
 protected:
  void SetUp() override {
    decoder = std::make_shared<NoiseGenerator>();
    player = std::make_unique<AudioPlayer>(AudioDecoder(decoder));
  }

  std::shared_ptr<NoiseGenerator> decoder;
  std::unique_ptr<AudioPlayer> player;
};

TEST_F(APlayerConcurrentTest, StressTest) {
  const auto quarter_samples = sample_rate * channel_count * duration / 4;
  player->SetLoopTimes(-1);
  std::atomic_bool should_continue = true;

  std::thread t1([&]() {
    std::mt19937 rng;
    std::uniform_int_distribution<> dist(1, 5);
    while (should_continue) {
      auto status = player->GetStatus();
      switch (dist(rng)) {
        case 1:
          if (status == AudioPlayer::STATUS::PAUSED)
            player->Unpause();
          if (status == AudioPlayer::STATUS::PLAYING)
            player->Pause();
          break;
        default:
          break;
      }
    }
  });

  std::thread t2([&]() {
    std::mt19937 rng;
    std::uniform_int_distribution<> dist(0, 3);
    while (should_continue) {
      auto i = dist(rng);
      const auto begin = i * quarter_samples / channel_count;
      const auto end = begin + quarter_samples / channel_count / 2;
      player->SetLoop(begin, end);
    }
  });

  std::vector<float> acceptable_pcm[5];
  acceptable_pcm[4].resize(quarter_samples);
  for (int i = 0; i < 4; ++i) {
    const auto begin = i * quarter_samples;
    const auto end = begin + i * quarter_samples / 2;
    acceptable_pcm[i] = std::vector<float>(decoder->buffer_.begin() + begin,
                                           decoder->buffer_.begin() + end);
  }

  const auto reps = 1000;
  for (int i = 0; i < reps; ++i) {
    bool result = false;
    EXPECT_NO_THROW({
      auto pcm =
          std::get<std::vector<float>>(player->LoadPCM(quarter_samples).data);
      for (int j = 0; j < 5; ++j) {
        if (Deviation(pcm, acceptable_pcm[j]) < 1e-4) {
          result = true;
          break;
        }
      }
    });
    EXPECT_TRUE(result);
  }

  should_continue = false;
  t1.join();
  t2.join();
}
