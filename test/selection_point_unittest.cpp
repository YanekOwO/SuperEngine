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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "core/selection_point.hpp"

#include <string>

namespace {
using ::testing::ElementsAre;
using ::testing::IsEmpty;

class SelectionPointTest : public ::testing::Test {
 protected:
  SelectionPoint state{3};
};

TEST_F(SelectionPointTest, MissingSavepointCannotCreateHistory) {
  // With no source snapshot, marking leaves history empty and unavailable.
  state.Mark();
  EXPECT_FALSE(state.HasPoint());
  EXPECT_THAT(state.history(), IsEmpty());
}

TEST_F(SelectionPointTest, MarkCopiesOpaqueBinaryData) {
  // Recording binary state must retain null bytes after the source is changed.
  const std::string snapshot("a\0b", 3);
  state.SetCurrentSnapshot(snapshot);
  state.Mark();
  state.SetCurrentSnapshot("later");
  EXPECT_TRUE(state.HasPoint());
  EXPECT_THAT(state.history(), ElementsAre(snapshot));
}

TEST_F(SelectionPointTest, EmptySnapshotClearsTheSource) {
  // Replacing a valid source with empty data makes the next mark a no-op.
  state.SetCurrentSnapshot("previous");
  state.SetCurrentSnapshot("");
  state.Mark();
  EXPECT_FALSE(state.HasPoint());
}

TEST_F(SelectionPointTest, ClearingTheSourcePreservesHistory) {
  // An existing history entry survives source clearing and a subsequent mark.
  state.SetCurrentSnapshot("previous");
  state.Mark();
  state.ClearCurrentSnapshot();
  state.Mark();
  EXPECT_THAT(state.history(), ElementsAre("previous"));
}

TEST_F(SelectionPointTest, FullHistoryEvictsTheOldestEntry) {
  // Four marks in a three-entry history retain the newest three in order.
  for (const auto* snapshot : {"first", "second", "third", "fourth"}) {
    state.SetCurrentSnapshot(snapshot);
    state.Mark();
  }
  EXPECT_THAT(state.history(), ElementsAre("second", "third", "fourth"));
}

TEST_F(SelectionPointTest, DefaultCapacityRetainsOneEntry) {
  // The default configuration replaces the oldest point on the second mark.
  SelectionPoint points;
  points.SetCurrentSnapshot("first");
  points.Mark();
  points.SetCurrentSnapshot("second");
  points.Mark();
  EXPECT_THAT(points.history(), ElementsAre("second"));
}

TEST_F(SelectionPointTest, CapacityIsClampedToAtLeastOne) {
  // A negative configuration still allows exactly one history entry.
  SelectionPoint points(-5);
  points.SetCurrentSnapshot("first");
  points.Mark();
  points.SetCurrentSnapshot("second");
  points.Mark();
  EXPECT_THAT(points.history(), ElementsAre("second"));
}

TEST_F(SelectionPointTest, CapacityIsClampedToAtMostOneHundred) {
  // An oversized configuration retains the last hundred of 101 snapshots.
  SelectionPoint points(101);
  for (int i = 0; i < 101; ++i) {
    points.SetCurrentSnapshot(std::to_string(i));
    points.Mark();
  }
  ASSERT_EQ(points.history().size(), 100);
  EXPECT_EQ(points.history().front(), "1");
  EXPECT_EQ(points.history().back(), "100");
}

TEST_F(SelectionPointTest, RepeatedMarksRecordTheSameSavepoint) {
  // Marking twice without changing the source appends two history entries.
  state.SetCurrentSnapshot("same");
  state.Mark();
  state.Mark();
  EXPECT_THAT(state.history(), ElementsAre("same", "same"));
}

TEST_F(SelectionPointTest, ClearRemovesHistory) {
  // Clearing a populated history makes the existence check false.
  state.SetCurrentSnapshot("point");
  state.Mark();
  state.Clear();
  EXPECT_FALSE(state.HasPoint());
  EXPECT_THAT(state.history(), IsEmpty());
}

TEST_F(SelectionPointTest, ClearPreservesTheCurrentSavepoint) {
  // A mark after history clearing still copies the existing source.
  state.SetCurrentSnapshot("point");
  state.Mark();
  state.Clear();
  state.Mark();
  EXPECT_THAT(state.history(), ElementsAre("point"));
}

TEST_F(SelectionPointTest, StackDoesNotMakeHistoryAvailable) {
  // Stocking a valid source alone must not enable the history query.
  state.SetCurrentSnapshot("point");
  state.Stack();
  EXPECT_FALSE(state.HasPoint());
}

TEST_F(SelectionPointTest, DropCopiesTheStockedSavepoint) {
  // Replacing the source after stocking must not change what drop records.
  state.SetCurrentSnapshot("stocked");
  state.Stack();
  state.SetCurrentSnapshot("current");
  state.Drop();
  EXPECT_THAT(state.history(), ElementsAre("stocked"));
}

TEST_F(SelectionPointTest, RepeatedStackOverwritesOneSlot) {
  // Two stock operations followed by a drop append only the latest stock.
  state.SetCurrentSnapshot("first");
  state.Stack();
  state.SetCurrentSnapshot("second");
  state.Stack();
  state.Drop();
  EXPECT_THAT(state.history(), ElementsAre("second"));
}

TEST_F(SelectionPointTest, DropConsumesStock) {
  // Dropping the same stocked point twice records it only once.
  state.SetCurrentSnapshot("point");
  state.Stack();
  state.Drop();
  state.Drop();
  EXPECT_THAT(state.history(), ElementsAre("point"));
}

TEST_F(SelectionPointTest, EmptySourceOverwritesPreviousStock) {
  // Stocking an absent source discards the previously stocked snapshot.
  state.SetCurrentSnapshot("old stock");
  state.Stack();
  state.ClearCurrentSnapshot();
  state.Stack();
  state.Drop();
  EXPECT_FALSE(state.HasPoint());
}

TEST_F(SelectionPointTest, DropWithoutStockPreservesHistory) {
  // A current source and history do not substitute for missing stock on drop.
  state.SetCurrentSnapshot("point");
  state.Mark();
  state.Drop();
  EXPECT_THAT(state.history(), ElementsAre("point"));
}

TEST_F(SelectionPointTest, ClearPreservesStock) {
  // Clearing history leaves the temporary slot available for a later drop.
  state.SetCurrentSnapshot("stocked");
  state.Stack();
  state.Mark();
  state.Clear();
  state.Drop();
  EXPECT_THAT(state.history(), ElementsAre("stocked"));
}

TEST_F(SelectionPointTest, DropUsesTheHistoryCapacity) {
  // Appending stocked data to a full history evicts its oldest entry.
  SelectionPoint points(1);
  points.SetCurrentSnapshot("stocked");
  points.Stack();
  points.SetCurrentSnapshot("marked");
  points.Mark();
  points.Drop();
  EXPECT_THAT(points.history(), ElementsAre("stocked"));
}

TEST_F(SelectionPointTest, ResetClearsAllSnapshots) {
  // A full reset removes history, its current source, and stocked data.
  state.SetCurrentSnapshot("point");
  state.Mark();
  state.Stack();
  state.Reset();
  state.Mark();
  state.Drop();
  EXPECT_FALSE(state.HasPoint());
}

TEST_F(SelectionPointTest, ResetRetainsConfiguredCapacity) {
  // Resetting a three-entry history still permits three subsequent marks.
  state.Reset();
  for (const auto* snapshot : {"first", "second", "third", "fourth"}) {
    state.SetCurrentSnapshot(snapshot);
    state.Mark();
  }
  EXPECT_THAT(state.history(), ElementsAre("second", "third", "fourth"));
}

}  // namespace
