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

#include "core/counter.hpp"
#include "mock_clock.hpp"
#include "serialization_test_helpers.hpp"

namespace {
class SerializationContextArchiveTest : public ::testing::Test {};

TEST_F(SerializationContextArchiveTest, ArchivesKeepIndependentContexts) {
  // Nested archives retain their own clock service and shared-pointer helper.
  serialization_test::ForEachFormat([&](auto format) {
    auto first = std::make_shared<MockClock>(),
         second = std::make_shared<MockClock>();
    serialization_test::Write(
        format,
        [&](auto& outer) {
          EXPECT_EQ(SerializationContext::Get(outer).GetClock(), first);
          auto bytes = serialization_test::Write(
              format,
              [&](auto& inner) {
                EXPECT_EQ(SerializationContext::Get(inner).GetClock(), second);
                auto value = std::make_shared<Counter>();
                value->Set(42);
                inner << value << value;
              },
              {.clock = second});
          EXPECT_EQ(SerializationContext::Get(outer).GetClock(), first);
          serialization_test::Read(
              bytes, format,
              [&](auto& ar) {
                std::shared_ptr<Counter> a, b;
                ar >> a >> b;
                EXPECT_EQ(a, b);
                ASSERT_TRUE(a);
                EXPECT_EQ(a->Get(), 42);
                EXPECT_EQ(SerializationContext::Get(ar).GetClock(), second);
              },
              {.clock = second});
        },
        {.clock = first});
  });
}

TEST_F(SerializationContextArchiveTest, PathsRelocateOnlyInsideGameRoot) {
  // Relative game assets use the loading root while external paths stay
  // absolute.
  serialization_test::ForEachFormat([&](auto format) {
    SerializationContext source{.game_root = "/games/source"},
        target{.game_root = "/games/target"};
    for (const auto& path : {std::filesystem::path("/games/source/movie.omv"),
                             std::filesystem::path("/external/movie.omv")}) {
      bool relative = false;
      auto saved = std::pair{source.SavePath(path, relative), relative};
      decltype(saved) restored;
      serialization_test::RoundTrip(saved, restored, format);
      EXPECT_EQ(target.LoadPath(restored.first, restored.second),
                path == "/games/source/movie.omv"
                    ? std::filesystem::path("/games/target/movie.omv")
                    : path);
    }
  });
}

}  // namespace
