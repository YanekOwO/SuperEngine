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

#include "core/group.hpp"
#include "serialization_test_helpers.hpp"

namespace {

class GroupArchiveTest : public ::testing::Test {};

TEST_F(GroupArchiveTest, PreservesSelectionInteraction) {
  // An active group retains optional button state and cancellation policy.
  serialization_test::ForEachFormat([&](auto format) {
    Group source, target;
    source.order = 9;
    source.layer = -3;
    source.cancel_priority = 4;
    source.cancel_se = 0;
    source.hit_button_no = 2;
    source.pushed_button_no = 3;
    source.decided_button_no = 4;
    source.result = Group::Result::Decided;
    source.result_button_no = 4;
    source.status = Group::Status::Waiting;
    source.cancel_enabled = true;
    source.pressed_button_no = 5;
    serialization_test::RoundTrip(source, target, format);
    EXPECT_EQ(target.order, 9);
    EXPECT_EQ(target.layer, -3);
    EXPECT_EQ(target.cancel_priority, 4);
    EXPECT_EQ(target.cancel_se, 0);
    EXPECT_EQ(target.hit_button_no, 2);
    EXPECT_EQ(target.pushed_button_no, 3);
    EXPECT_EQ(target.decided_button_no, 4);
    EXPECT_EQ(target.result, Group::Result::Decided);
    EXPECT_EQ(target.result_button_no, 4);
    EXPECT_EQ(target.status, Group::Status::Waiting);
    EXPECT_TRUE(target.cancel_enabled);
    EXPECT_EQ(target.pressed_button_no, 5);
  });
}

TEST_F(GroupArchiveTest, AbsentButtonsReplaceExistingValues) {
  // An empty group clears optional values in an already populated destination.
  serialization_test::ForEachFormat([&](auto format) {
    Group source, target;
    target.cancel_se = 9;
    target.pressed_button_no = 3;
    target.result_button_no = 4;
    serialization_test::RoundTrip(source, target, format);
    EXPECT_FALSE(target.cancel_se);
    EXPECT_FALSE(target.pressed_button_no);
    EXPECT_FALSE(target.result_button_no);
  });
}

}  // namespace
