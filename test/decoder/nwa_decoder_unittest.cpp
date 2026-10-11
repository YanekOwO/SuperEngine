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

#include "core/avdec/nwa.hpp"
#include "test_utils.hpp"
#include "utilities/mapped_file.hpp"

#include <cstdlib>
#include <filesystem>
#include <numbers>
#include <random>
#include <regex>
#include <string>

namespace fs = std::filesystem;

inline constexpr auto pi = std::numbers::pi;

class NwaDecoderTest : public ::testing::TestWithParam<fs::path> {
 protected:
  static constexpr float duration = 0.2;
  static constexpr int channels = 2;
  static constexpr int freq = 22050;
  static constexpr int samplesPerChannel = freq * duration;

  inline static int16_t GetSampleAt(float t) {
    static const float freq[] = {440, 523.25, 349.23};
    static const float amp[] = {0.5, 0.3, 0.2};
    static const float phase[] = {0, 0, pi / 2};
    float sample = 0;
    for (int i = 0; i < 3; ++i)
      sample += amp[i] * sin(2 * pi * freq[i] * t + phase[i]);
    return sample * INT16_MAX;
  };

  inline static std::vector<int16_t> GetExpectedPcm() {
    static std::vector<int16_t> expect_wav;
    if (expect_wav.empty()) {
      expect_wav.reserve(samplesPerChannel);
      for (int i = 0; i < samplesPerChannel; ++i) {
        float t = 1.0 / freq * i;
        expect_wav.push_back(GetSampleAt(t));
      }
    }
    return expect_wav;
  }

  inline static std::vector<int16_t> ToInt16Vec(const avsample_buffer_t& data) {
    return std::get<std::vector<int16_t>>(data);
  }

  inline static std::tuple<std::vector<int16_t>, std::vector<int16_t>>
  SplitChannels(std::vector<int16_t> samples) {
    std::vector<int16_t> ch[2];
    int current_channel = 0;
    for (const auto& it : samples) {
      ch[current_channel].push_back(it);
      current_channel ^= 1;
    }
    return std::make_tuple(std::move(ch[0]), std::move(ch[1]));
  }

  inline static double Deviation(std::vector<int16_t> a,
                                 std::vector<int16_t> b) {
    auto sqr = [](double x) { return x * x; };
    size_t n = a.size();
    double var = 0;
    for (size_t i = 0; i < n; ++i)
      var += sqr(a[i] - b[i]) / n;
    return sqrt(var);
  }

  void SetUp() override {
    file_content = std::make_unique<MappedFile>(GetParam());
  }

  float DeviationThreshold() {
    std::string filename = GetParam().stem().string();
    if (filename == "BGM01")
      return 1e-4;
    else if (filename == "BGM02")
      return 0.02;
    else if (filename == "BGM03")
      return 0.05;
    else if (filename == "BGM04")
      return 0.025;
    else if (filename == "BGM05")
      return 0.0035;
    else if (filename == "BGM06")
      return 0.001;
    else if (filename == "BGM07")
      return 0.0007;
    else {
      ADD_FAILURE() << "Unknown data file: " << filename;
      return 0;
    }
  }

  std::unique_ptr<MappedFile> file_content;
};

TEST_P(NwaDecoderTest, DecodeAll) {
  const float maxstd = DeviationThreshold() * INT16_MAX;
  NwaDecoder decoder(file_content->Read());

  AudioData result = decoder.DecodeAll();
  auto [lch, rch] = SplitChannels(ToInt16Vec(result.data));
  auto expect_wav = GetExpectedPcm();

  size_t n = expect_wav.size();
  ASSERT_EQ(lch.size(), n);
  ASSERT_EQ(rch.size(), n);

  EXPECT_LE(Deviation(lch, expect_wav), maxstd);
  EXPECT_LE(Deviation(rch, expect_wav), maxstd);
}

TEST_P(NwaDecoderTest, Rewind) {
  const float maxstd = DeviationThreshold() * INT16_MAX;
  NwaDecoder decoder(file_content->Read());

  // decode the first 3 units
  AudioData result_front;
  {
    auto a = decoder.DecodeNext();
    auto b = decoder.DecodeNext();
    auto c = decoder.DecodeNext();
    result_front = AudioData::Concat(std::move(a), std::move(b), std::move(c));
  }
  EXPECT_TRUE(decoder.HasNext());
  auto [lch_front, rch_front] = SplitChannels(ToInt16Vec(result_front.data));
  EXPECT_EQ(lch_front.size(), rch_front.size());

  // rewind then decode all
  EXPECT_EQ(decoder.Seek(0, SEEKDIR::BEG), SEEK_RESULT::PRECISE_SEEK);
  AudioData result = decoder.DecodeAll();
  EXPECT_FALSE(decoder.HasNext());

  auto [lch, rch] = SplitChannels(ToInt16Vec(result.data));
  auto expect_wav = GetExpectedPcm();
  size_t n = expect_wav.size();
  EXPECT_LE(Deviation(lch, expect_wav), maxstd);
  EXPECT_LE(Deviation(rch, expect_wav), maxstd);

  n = lch_front.size();
  expect_wav.resize(n);
  EXPECT_LE(Deviation(lch_front, expect_wav), maxstd);
  EXPECT_LE(Deviation(rch_front, expect_wav), maxstd);
}

TEST_P(NwaDecoderTest, RandomAccess) {
  const float maxstd = DeviationThreshold() * INT16_MAX;
  NwaDecoder decoder(file_content->Read());
  auto expect_wav = GetExpectedPcm();
  auto n = expect_wav.size();

  std::vector<int16_t> actual_wav(n);
  std::vector<bool> has_value(n);
  std::mt19937 rng;
  std::fill(has_value.begin(), has_value.end(), false);
  std::generate(actual_wav.begin(), actual_wav.end(), [&rng]() -> int16_t {
    static std::uniform_int_distribution<> dist(
        std::numeric_limits<int16_t>::min(),
        std::numeric_limits<int16_t>::max());
    return dist(rng);
  });

  auto handle_decoded_chunk = [&](int offset, AudioData ad) {
    auto [lch, rch] = SplitChannels(ToInt16Vec(ad.data));
    for (size_t i = 0; i < lch.size(); ++i) {
      EXPECT_EQ(lch[i], rch[i]);
      has_value[i + offset] = true;
      actual_wav[i + offset] = lch[i];
    }
  };

  std::uniform_int_distribution<> dist(0, n - 1);
  for (int i = 0; i < 16; ++i) {
    int idx = dist(rng);
    auto seek_result = decoder.Seek(idx, SEEKDIR::BEG);
    ASSERT_NE(seek_result, SEEK_RESULT::FAIL);

    idx = decoder.Tell();
    handle_decoded_chunk(idx, decoder.DecodeNext());
  }

  ASSERT_NE(decoder.Seek(0, SEEKDIR::BEG), SEEK_RESULT::FAIL);
  size_t idx = 0;
  for (size_t i = 0; i < n; ++i) {
    if (has_value[i])
      continue;
    decoder.Seek(i - idx, SEEKDIR::CUR);
    idx = decoder.Tell();
    handle_decoded_chunk(idx, decoder.DecodeNext());
    idx = decoder.Tell();
  }

  EXPECT_LE(Deviation(actual_wav, expect_wav), maxstd);
}

std::vector<fs::path> GetTestNwaFiles() {
  static const std::string testdir = LocateTestDirectory("Gameroot/BGM");
  static const std::regex pattern(".*BGM[0-9]+\\.nwa");

  std::vector<fs::path> testFiles;
  for (const auto& entry : fs::directory_iterator(testdir)) {
    auto entryPath = entry.path().string();
    if (entry.is_regular_file() && std::regex_match(entryPath, pattern))
      testFiles.push_back(entry);
  }
  return testFiles;
}

using ::testing::ValuesIn;
INSTANTIATE_TEST_SUITE_P(NwaDecoderDataTest,
                         NwaDecoderTest,
                         ValuesIn(GetTestNwaFiles()),
                         ([](const auto& info) {
                           return info.param.stem().string();
                         }));
