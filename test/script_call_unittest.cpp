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

#include "core/script_call.hpp"
#include "serialization_test_helpers.hpp"

namespace {
class ScriptCallArchiveTest : public ::testing::Test {};

TEST_F(ScriptCallArchiveTest, RetainsEveryTargetAlternative) {
  // Calls to command, numbered scene and named scene retain arguments and
  // farcall mode.
  serialization_test::ForEachFormat([&](auto format) {
    for (auto target : {decltype(ScriptCall::target){CmdTarget{3, 7}},
                        decltype(ScriptCall::target){SceneTarget{17, 9}},
                        decltype(ScriptCall::target){
                            SceneTarget{std::string("scene"), 12}}}) {
      ScriptCall source{.target = target,
                        .integers = {-5, 42},
                        .strings = {"", "hello"},
                        .farcall = true},
          restored;
      serialization_test::RoundTrip(source, restored, format);
      EXPECT_EQ(restored, source);
    }
  });
}

}  // namespace
