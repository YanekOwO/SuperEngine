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

#include "core/gameexe.hpp"
#include "core/tone_curve.hpp"
#include "utilities/bytestream.hpp"
#include "utilities/file.hpp"

#include <algorithm>
#include <cstdint>
#include <random>

class ToneCurveTest : public ::testing::Test {
 protected:
  std::string EncodeToneCurve(std::vector<ToneCurveRGBMap> curves) {
    oBytestream obs;
    obs << 1000 << static_cast<uint32_t>(curves.size());

    int offset = 4008;
    for (size_t i = 0; i < 1000; ++i) {
      if (i < curves.size()) {
        obs << offset;
        offset += 832;
      } else {
        obs << 0;
      }
    }

    for (size_t i = 0; i < curves.size(); ++i) {
      obs << 0 << 768;
      for (int j = 0; j < 14; ++j)
        obs << 0;
      obs << curves[i][0] << curves[i][1] << curves[i][2];
    }

    auto data = obs.Get();
    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
  }

  inline static auto random_uint8 = []() {
    static std::mt19937 rng;
    static std::uniform_int_distribution<> dist(
        0, std::numeric_limits<uint8_t>::max());
    return dist(rng);
  };
};

TEST_F(ToneCurveTest, SingleEffect) {
  std::array<uint8_t, 256> r, g, b;
  std::generate(r.begin(), r.end(), random_uint8);
  std::generate(g.begin(), g.end(), random_uint8);
  std::generate(b.begin(), b.end(), random_uint8);

  auto src = EncodeToneCurve({{r, g, b}});
  ToneCurve tcc(src);

  EXPECT_EQ(tcc.GetEffectCount(), 1);
  auto result = tcc.GetEffect(0);
  EXPECT_EQ(result[0], r);
  EXPECT_EQ(result[1], g);
  EXPECT_EQ(result[2], b);
}

TEST_F(ToneCurveTest, CreateFromCaseInsensitiveDataDirectory) {
  ToneCurveRGBMap effect{};
  effect[0].fill(17);
  effect[1].fill(42);
  effect[2].fill(255);
  const auto data = EncodeToneCurve({effect});

  for (const char* directory : {"dat", "DAT", "Dat"}) {
    SCOPED_TRACE(directory);
    ScopedTemporary root;
    WriteFile(root.dir() / directory / "ToneCurve.TCC", data);

    Gameexe gameexe;
    gameexe("__GAMEPATH") = root.dir().string();
    gameexe("TONECURVE_FILENAME") = "tonecurve.tcc";

    const auto curve = CreateToneCurve(gameexe);
    EXPECT_EQ(curve.GetEffectCount(), 1);
    EXPECT_EQ(curve.GetEffect(0), effect);
  }
}

TEST_F(ToneCurveTest, NoEffects) {
  {
    auto src = EncodeToneCurve({});
    ToneCurve tcc(src);

    EXPECT_EQ(tcc.GetEffectCount(), 0);
  }

  {
    ToneCurve tcc;
    EXPECT_EQ(tcc.GetEffectCount(), 0);
  }
}

TEST_F(ToneCurveTest, InvalidEffectIndex) {
  std::array<uint8_t, 256> r, g, b;
  std::generate(r.begin(), r.end(), random_uint8);
  std::generate(g.begin(), g.end(), random_uint8);
  std::generate(b.begin(), b.end(), random_uint8);

  auto src = EncodeToneCurve({{r, g, b}});
  ToneCurve tcc(src);

  EXPECT_EQ(tcc.GetEffectCount(), 1);
  EXPECT_THROW(tcc.GetEffect(-1), std::out_of_range);
  EXPECT_THROW(tcc.GetEffect(1), std::out_of_range);
}

TEST_F(ToneCurveTest, CorruptedSourceData) {
  std::string corrupted_src = "invalid data";
  EXPECT_THROW(ToneCurve tcc(corrupted_src), std::exception);
}

TEST_F(ToneCurveTest, LargeNumberOfEffects) {
  std::vector<std::array<std::array<uint8_t, 256>, 3>> effects(1000);
  for (auto& effect : effects) {
    std::generate(effect[0].begin(), effect[0].end(), random_uint8);
    std::generate(effect[1].begin(), effect[1].end(), random_uint8);
    std::generate(effect[2].begin(), effect[2].end(), random_uint8);
  }

  auto src = EncodeToneCurve(effects);
  ToneCurve tcc(src);

  EXPECT_EQ(tcc.GetEffectCount(), 1000);
  for (int i = 0; i < 1000; ++i) {
    auto result = tcc.GetEffect(i);
    EXPECT_EQ(result[0], effects[i][0]);
    EXPECT_EQ(result[1], effects[i][1]);
    EXPECT_EQ(result[2], effects[i][2]);
  }
}
