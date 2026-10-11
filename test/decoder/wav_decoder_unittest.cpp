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

#include <gtest/gtest.h>

#include "core/avdec/wav.hpp"
#include "test_utils.hpp"
#include "utilities/mapped_file.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <random>
#include <regex>
#include <string>
#include <type_traits>

template <class T>
struct remove_cvref {
  using type = std::remove_const_t<std::remove_reference_t<T>>;
};
template <class T>
using remove_cvref_t = typename remove_cvref<T>::type;

namespace fs = std::filesystem;

inline constexpr auto pi = std::numbers::pi;

class WavCodecTest : public ::testing::TestWithParam<std::string> {
 public:
  void SetUp() override {
    if (GetParam() == "NONE") {
      GTEST_SKIP() << "[WARNING] Test skipped due to uninitialized parameter.";
    }

    const std::string filepath = GetParam() + ".param";
    std::ifstream param_fs(filepath);
    if (!param_fs)
      FAIL() << "Failed to open parameter file: " << filepath;

    param_fs >> sample_rate >> channel >> sample_width >> frequency >> duration;
  }

  AV_SAMPLE_FMT DetermineSampleFormat() const {
    switch (sample_width) {
      case 1:
        return AV_SAMPLE_FMT::U8;
      case 2:
        return AV_SAMPLE_FMT::S16;
      case 4:
        return AV_SAMPLE_FMT::S32;
      case 8:
        return AV_SAMPLE_FMT::S64;
      default:
        return AV_SAMPLE_FMT::NONE;
    }
  }

  AVSpec DetermineSpecification() const {
    return {.sample_rate = sample_rate,
            .sample_format = DetermineSampleFormat(),
            .channel_count = channel};
  }

  std::vector<double> ReproduceAudio() const {
    std::vector<double> wav;
    int sample_count = std::round(sample_rate * duration);
    for (int i = 0; i < sample_count; ++i) {
      float t = static_cast<double>(i) / sample_rate;
      double sample = sin(2 * pi * frequency * t);
      for (int j = 0; j < channel; ++j)
        wav.push_back(sample);
    }
    return wav;
  }

  std::vector<double> Normalize(const avsample_buffer_t& a) const {
    return std::visit(
        [](auto& a) -> std::vector<double> {
          using sample_t = typename remove_cvref_t<decltype(a)>::value_type;
          const auto max_value =
              static_cast<double>(std::numeric_limits<sample_t>::max());

          std::vector<double> result;
          for (const auto& it : a) {
            if constexpr (std::is_same_v<sample_t, uint8_t>)
              result.push_back((it - 127.5) / 127.5);
            else
              result.push_back(static_cast<double>(it) / max_value);
          }
          return result;
        },
        a);
  }

  double Deviation(const std::vector<double>& a,
                   const std::vector<double>& b) const {
    auto sqr = [](auto x) { return x * x; };
    size_t n = a.size();
    double var = 0;
    for (size_t i = 0; i < n; ++i)
      var += sqr(a[i] - b[i]) / n;
    return sqrt(var);
  }

  int sample_rate, channel, sample_width;
  int frequency;
  float duration;
};

TEST_P(WavCodecTest, DecodeWav) {
  const double max_std = 0.075 * exp(-sample_width);

  auto file = MappedFile(GetParam());
  WavDecoder decoder(file.Read());
  AudioData result = decoder.DecodeAll();

  auto expect_wav = ReproduceAudio();
  ASSERT_EQ(DetermineSpecification(), result.spec);
  ASSERT_EQ(expect_wav.size(), result.SampleCount());

  auto result_wav = Normalize(result.data);
  EXPECT_LE(Deviation(expect_wav, result_wav), max_std);
}

TEST_P(WavCodecTest, RandomAccess) {
  const double max_std = 0.075 * exp(-sample_width);
  auto file = MappedFile(GetParam());
  WavDecoder decoder(file.Read());

  auto expect_wav = ReproduceAudio();
  const auto n = expect_wav.size();
  std::vector<double> result_wav(n);
  std::vector<bool> has_value(n);
  std::mt19937 rng;
  std::fill(has_value.begin(), has_value.end(), false);
  std::generate(result_wav.begin(), result_wav.end(), [&]() {
    static std::uniform_real_distribution<> dist(-1.0, 1.0);
    return dist(rng);
  });

  for (int i = 0; i < 16; ++i) {
    std::uniform_int_distribution<> dist(0, n - 1);
    auto idx = dist(rng);
    idx -= idx & 1;

    EXPECT_EQ(decoder.Seek(idx / channel, SEEKDIR::BEG),
              SEEK_RESULT::PRECISE_SEEK);
    auto audio_data = Normalize(decoder.DecodeNext().data);
    EXPECT_EQ(idx + audio_data.size(), decoder.Tell() * channel);
    std::copy(audio_data.cbegin(), audio_data.cend(), result_wav.begin() + idx);
    std::fill_n(has_value.begin() + idx, audio_data.size(), true);
  }

  for (size_t i = 0; i < n; i += 2) {
    if (has_value[i])
      continue;
    EXPECT_EQ(decoder.Seek(i / channel, SEEKDIR::BEG),
              SEEK_RESULT::PRECISE_SEEK);
    auto ad = decoder.DecodeNext();
    auto audio_data = Normalize(ad.data);
    EXPECT_EQ(i + audio_data.size(), decoder.Tell() * channel);
    std::copy(audio_data.cbegin(), audio_data.cend(), result_wav.begin() + i);
    std::fill_n(has_value.begin() + i, audio_data.size(), true);
  }

  EXPECT_LE(Deviation(expect_wav, result_wav), max_std);
}

TEST_P(WavCodecTest, EncodeRiffHeader) {
  auto spec = DetermineSpecification();
  auto header_raw = MakeRiffHeader(spec, 0);
  ASSERT_EQ(header_raw.size(), 44);

  std::string header = std::string((char*)header_raw.data(), header_raw.size());
  std::string magic =
      header.substr(0, 4) + header.substr(8, 8) + header.substr(36, 4);
  EXPECT_EQ(magic, "RIFFWAVEfmt data");

  fmtHeader* fmt = (fmtHeader*)(header_raw.data() + 20);
  EXPECT_EQ(fmt->wFormatTag, 1);
  EXPECT_EQ(fmt->nChannels, channel);
  EXPECT_EQ(fmt->nSamplesPerSec, sample_rate);
  EXPECT_EQ(fmt->nAvgBytesPerSec, sample_rate * sample_width * channel);
  EXPECT_EQ(fmt->nBlockAlign, sample_width * channel);
  EXPECT_EQ(fmt->wBitsPerSample, (8 * sample_width));
}

TEST_P(WavCodecTest, EncoderTest) {
  auto file = MappedFile(GetParam());
  std::string_view file_content = file.Read();
  AudioData audioData = WavDecoder(file_content).DecodeAll();
  auto encodedWav = EncodeWav(audioData);
  EXPECT_EQ(file_content,
            (std::string_view((char*)encodedWav.data(), encodedWav.size())));
}

TEST(WavCodecStandaloneTest, DecodeAllConsumesRemainingData) {
  AVSpec spec{.sample_rate = 8000,
              .sample_format = AV_SAMPLE_FMT::S16,
              .channel_count = 1};
  AudioData source{.spec = spec,
                   .data = std::vector<avsample_s16_t>{1, 2, 3, 4, 5}};
  auto encoded = EncodeWav(source);
  WavDecoder decoder(
      std::string_view(reinterpret_cast<const char*>(encoded.data()),
                       encoded.size()));

  EXPECT_EQ(decoder.Seek(2, SEEKDIR::BEG), SEEK_RESULT::PRECISE_SEEK);
  AudioData decoded = decoder.DecodeAll();

  EXPECT_EQ(decoded.spec, spec);
  EXPECT_EQ(std::get<std::vector<avsample_s16_t>>(decoded.data),
            (std::vector<avsample_s16_t>{3, 4, 5}));
  EXPECT_FALSE(decoder.HasNext());
}

std::vector<std::string> GetTestWavFiles() {
  static const std::string testdir = LocateTestDirectory("Gameroot/WAV");
  static const std::regex pattern(".*test[0-9]+\\.wav");

  std::vector<std::string> testFiles;
  for (const auto& entry : fs::directory_iterator(testdir)) {
    auto entryPath = entry.path().string();
    if (entry.is_regular_file() && std::regex_match(entryPath, pattern))
      testFiles.push_back(entryPath);
  }

  if (testFiles.empty())
    return {"NONE"};
  return testFiles;
}

using ::testing::ValuesIn;
INSTANTIATE_TEST_SUITE_P(WavDecoderDataTest,
                         WavCodecTest,
                         ValuesIn(GetTestWavFiles()),
                         ([](const auto& info) {
                           return fs::path(info.param).stem().string();
                         }));
