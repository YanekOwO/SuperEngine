// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
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
//
// -----------------------------------------------------------------------

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "core/avdec/audio_decoder.hpp"
#include "core/avdec/wav.hpp"
#include "utilities/file.hpp"

#include <string_view>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using std::string_literals::operator""s;
using std::string_view_literals::operator""sv;

class MockAudioDecoder : public IAudioDecoder {
 public:
  MOCK_METHOD(AVSpec, GetSpec, (), (override));
  MOCK_METHOD(AudioData, DecodeAll, (), (override));
  MOCK_METHOD(AudioData, DecodeNext, (), (override));
  MOCK_METHOD(bool, HasNext, (), (override));
  MOCK_METHOD(SEEK_RESULT,
              Seek,
              (pcm_count_t offset, SEEKDIR whence),
              (override));
  MOCK_METHOD(pcm_count_t, Tell, (), (override));
};

class DecoderA : public MockAudioDecoder {
 public:
  DecoderA() {}
  DecoderA(std::string_view data) {
    if (data != "decoderA")
      throw -1;
  }
  std::string DecoderName() const override { return "DecoderA"; }
};
class DecoderB : public MockAudioDecoder {
 public:
  DecoderB() {}
  DecoderB(std::string_view data) {
    if (data != "decoderB")
      throw -1;
  }

  std::string DecoderName() const override { return "DecoderB"; }
};
class DecoderC : public MockAudioDecoder {
 public:
  DecoderC() {}
  DecoderC(std::string_view data) {
    if (data != "decoderC")
      throw -1;
  }
  std::string DecoderName() const override { return "DecoderC"; }
};
class DecoderD : public MockAudioDecoder {
 public:
  DecoderD() {}
  DecoderD(std::string_view data) {
    if (data != "decoderD")
      throw -1;
  }
  std::string DecoderName() const override { return "DecoderD"; }
};

using decoder_constructor_t = ADecoderFactory::decoder_constructor_t;

class FactoryHandler : public ADecoderFactory {
 public:
  FactoryHandler() : ADecoderFactory() {}

  using ADecoderFactory::Create;

  void SupercedeDecoderMap(
      const std::unordered_map<std::string, decoder_constructor_t>* map) {
    decoder_map_ = map;
  }
};

class ADecFactoryTest : public ::testing::Test {
 protected:
  const std::unordered_map<std::string, decoder_constructor_t> mymap = {
      {"mp3",
       [](std::string_view data) -> decoder_t {
         return std::make_shared<DecoderA>(data);
       }},
      {"aac",
       [](std::string_view data) -> decoder_t {
         return std::make_shared<DecoderB>(data);
       }},
      {"wav",
       [](std::string_view data) -> decoder_t {
         return std::make_shared<DecoderC>(data);
       }},
      {"ogg", [](std::string_view data) -> decoder_t {
         return std::make_shared<DecoderD>(data);
       }}};

  void SetUp() override { factory.SupercedeDecoderMap(&mymap); };

  FactoryHandler factory;
};

TEST_F(ADecFactoryTest, Create) {
  auto aacdec = factory.Create("decoderB"sv, "aac"s);
  EXPECT_NE(std::dynamic_pointer_cast<DecoderB>(aacdec), nullptr)
      << aacdec->DecoderName();
  auto wavdec = factory.Create("decoderC"sv, "wav"s);
  EXPECT_NE(std::dynamic_pointer_cast<DecoderC>(wavdec), nullptr)
      << wavdec->DecoderName();
  EXPECT_THROW(factory.Create("doesnt matter"sv, "pdf"s), std::runtime_error);
}

TEST_F(ADecFactoryTest, CreateNohint) {
  auto mp3dec = factory.Create("decoderA"sv);
  EXPECT_NE(std::dynamic_pointer_cast<DecoderA>(mp3dec), nullptr)
      << mp3dec->DecoderName();
  auto aacdec = factory.Create("decoderB"sv);
  EXPECT_NE(std::dynamic_pointer_cast<DecoderB>(aacdec), nullptr)
      << aacdec->DecoderName();
  auto wavdec = factory.Create("decoderC"sv);
  EXPECT_NE(std::dynamic_pointer_cast<DecoderC>(wavdec), nullptr)
      << wavdec->DecoderName();
  EXPECT_THROW(factory.Create("doesnt matter"sv), std::runtime_error);
}

TEST_F(ADecFactoryTest, InvalidData) {
  EXPECT_THROW(factory.Create("invalid data"sv, "aac"s), std::runtime_error);
}

TEST(AudioDecoderPathTest, UsesCaseInsensitiveExtensionAsFormatHint) {
  const AVSpec spec{.sample_rate = 8000,
                    .sample_format = AV_SAMPLE_FMT::S16,
                    .channel_count = 1};
  const std::vector<avsample_s16_t> samples{1, -2, 3, -4};
  const AudioData source{.spec = spec, .data = samples};
  const ScopedTemporary tmp;
  const fs::path path = tmp.dir() / "audio.WAV";

  auto wav = EncodeWav(source);
  WriteFile(path, wav);
  AudioDecoder decoder(path);

  EXPECT_EQ(decoder.GetSpec(), spec);
  EXPECT_EQ(decoder.Seek(0, SEEKDIR::BEG), SEEK_RESULT::PRECISE_SEEK);
  const AudioData decoded = decoder.DecodeAll();
  EXPECT_EQ(std::get<std::vector<avsample_s16_t>>(decoded.data), samples);
}
