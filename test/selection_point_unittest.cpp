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
  CheckpointRef current;
  SelectionPoint state{current, 3};
};

TEST_F(SelectionPointTest, MissingSavepointCannotCreateHistory) {
  // With no source snapshot, marking leaves history empty and unavailable.
  state.Mark();
  EXPECT_FALSE(state.HasPoint());
  EXPECT_THAT(state.history(), IsEmpty());
}

TEST_F(SelectionPointTest, MarkCopiesReferenceMetadata) {
  // Updating the same checkpoint leaves the marked metadata, including nulls,
  // intact.
  current = {.id = 1,
             .title = "Title",
             .message = std::string("a\0b", 3),
             .full_message = "Full message",
             .append_dir = "directory",
             .append_name = "name"};
  const auto expected = current;
  state.Mark();
  current.message = "later";
  EXPECT_THAT(state.history(), ElementsAre(expected));
}

TEST_F(SelectionPointTest, MarkReadsUpdatedBorrowedMetadata) {
  // Updating the owner directly makes the latest metadata visible to a mark.
  current = {.id = 1, .message = "before"};
  current.message = "after";
  state.Mark();
  EXPECT_THAT(state.history(),
              ElementsAre(CheckpointRef{.id = 1, .message = "after"}));
}

TEST_F(SelectionPointTest, InvalidReferenceClearsTheSource) {
  // A zero ID clears the source even when metadata is present, so marking is a
  // no-op.
  current = {.id = 1};
  current = {.title = "Invalid checkpoint"};
  state.Mark();
  EXPECT_FALSE(state.HasPoint());
}

TEST_F(SelectionPointTest, ClearingTheSourcePreservesHistory) {
  // An existing history entry survives source clearing and a subsequent mark.
  current = {.id = 1};
  state.Mark();
  current = {};
  state.Mark();
  EXPECT_THAT(state.history(), ElementsAre(CheckpointRef{.id = 1}));
}

TEST_F(SelectionPointTest, FullHistoryEvictsTheOldestEntry) {
  // Four marks in a three-entry history retain the newest three in order.
  for (CheckpointId id : {1, 2, 3, 4}) {
    current = {.id = id};
    state.Mark();
  }
  EXPECT_THAT(state.history(),
              ElementsAre(CheckpointRef{.id = 2}, CheckpointRef{.id = 3},
                          CheckpointRef{.id = 4}));
}

TEST_F(SelectionPointTest, DefaultCapacityRetainsOneEntry) {
  // The default configuration replaces the oldest point on the second mark.
  SelectionPoint points(current);
  current = {.id = 1};
  points.Mark();
  current = {.id = 2};
  points.Mark();
  EXPECT_THAT(points.history(), ElementsAre(CheckpointRef{.id = 2}));
}

TEST_F(SelectionPointTest, CapacityIsClampedToAtLeastOne) {
  // A zero capacity still allows exactly one history entry.
  SelectionPoint points(current, 0);
  current = {.id = 1};
  points.Mark();
  current = {.id = 2};
  points.Mark();
  EXPECT_THAT(points.history(), ElementsAre(CheckpointRef{.id = 2}));
}

TEST_F(SelectionPointTest, RepeatedMarksRecordTheSameSavepoint) {
  // Marking twice without changing the source appends two history entries.
  current = {.id = 1};
  state.Mark();
  state.Mark();
  EXPECT_THAT(state.history(),
              ElementsAre(CheckpointRef{.id = 1}, CheckpointRef{.id = 1}));
}

TEST_F(SelectionPointTest, ClearRemovesHistory) {
  // Clearing a populated history makes the existence check false.
  current = {.id = 1};
  state.Mark();
  state.Clear();
  EXPECT_FALSE(state.HasPoint());
  EXPECT_THAT(state.history(), IsEmpty());
}

TEST_F(SelectionPointTest, ClearPreservesTheCurrentSavepoint) {
  // A mark after history clearing still copies the existing source.
  current = {.id = 1};
  state.Mark();
  state.Clear();
  state.Mark();
  EXPECT_THAT(state.history(), ElementsAre(CheckpointRef{.id = 1}));
}

TEST_F(SelectionPointTest, StackDoesNotMakeHistoryAvailable) {
  // Stocking a valid source alone must not enable the history query.
  current = {.id = 1};
  state.Stack();
  EXPECT_FALSE(state.HasPoint());
}

TEST_F(SelectionPointTest, DropCopiesStockedMetadata) {
  // Updating the same checkpoint after stocking leaves the dropped metadata
  // frozen at the stocked value.
  current = {.id = 1, .message = "stocked"};
  state.Stack();
  current = {.id = 1, .message = "current"};
  state.Drop();
  EXPECT_THAT(state.history(),
              ElementsAre(CheckpointRef{.id = 1, .message = "stocked"}));
}

TEST_F(SelectionPointTest, RepeatedStackOverwritesOneSlot) {
  // Two stock operations followed by a drop append only the latest stock.
  current = {.id = 1};
  state.Stack();
  current = {.id = 2};
  state.Stack();
  state.Drop();
  EXPECT_THAT(state.history(), ElementsAre(CheckpointRef{.id = 2}));
}

TEST_F(SelectionPointTest, DropConsumesStock) {
  // Dropping the same stocked point twice records it only once.
  current = {.id = 1};
  state.Stack();
  state.Drop();
  state.Drop();
  EXPECT_THAT(state.history(), ElementsAre(CheckpointRef{.id = 1}));
}

TEST_F(SelectionPointTest, EmptySourceOverwritesPreviousStock) {
  // Stocking an absent source discards the previously stocked snapshot.
  current = {.id = 1};
  state.Stack();
  current = {};
  state.Stack();
  state.Drop();
  EXPECT_FALSE(state.HasPoint());
}

TEST_F(SelectionPointTest, DropWithoutStockPreservesHistory) {
  // A current source and history do not substitute for missing stock on drop.
  current = {.id = 1};
  state.Mark();
  state.Drop();
  EXPECT_THAT(state.history(), ElementsAre(CheckpointRef{.id = 1}));
}

TEST_F(SelectionPointTest, ClearPreservesStock) {
  // Clearing history leaves the temporary slot available for a later drop.
  current = {.id = 1};
  state.Stack();
  state.Mark();
  state.Clear();
  state.Drop();
  EXPECT_THAT(state.history(), ElementsAre(CheckpointRef{.id = 1}));
}

TEST_F(SelectionPointTest, DropUsesTheHistoryCapacity) {
  // Appending stocked data to a full history evicts its oldest entry.
  SelectionPoint points(current, 1);
  current = {.id = 1};
  points.Stack();
  current = {.id = 2};
  points.Mark();
  points.Drop();
  EXPECT_THAT(points.history(), ElementsAre(CheckpointRef{.id = 1}));
}

TEST_F(SelectionPointTest, ResetClearsHistoryAndStock) {
  // Reset clears recorded entries and stock, so a later drop adds nothing.
  current = {.id = 1};
  state.Mark();
  state.Stack();
  state.Reset();
  state.Drop();
  EXPECT_FALSE(state.HasPoint());
}

TEST_F(SelectionPointTest, ResetPreservesTheBorrowedCheckpoint) {
  // Reset leaves the owner's active checkpoint available for subsequent marks.
  current = {.id = 1};
  state.Reset();
  state.Mark();
  EXPECT_THAT(state.history(), ElementsAre(CheckpointRef{.id = 1}));
}

TEST_F(SelectionPointTest, ResetRetainsConfiguredCapacity) {
  // Resetting a three-entry history still permits three subsequent marks.
  state.Reset();
  for (CheckpointId id : {1, 2, 3, 4}) {
    current = {.id = id};
    state.Mark();
  }
  EXPECT_THAT(state.history(),
              ElementsAre(CheckpointRef{.id = 2}, CheckpointRef{.id = 3},
                          CheckpointRef{.id = 4}));
}

TEST_F(SelectionPointTest, RestorePreservesTheBorrowedCheckpoint) {
  // Restoring recorded state leaves the live source available for new marks.
  current = {.id = 2};
  state.RestoreState({.history = {{.id = 1}}});
  state.Mark();
  EXPECT_THAT(state.history(),
              ElementsAre(CheckpointRef{.id = 1}, CheckpointRef{.id = 2}));
}

TEST_F(SelectionPointTest, RestoreClearsInvalidStockReference) {
  // Metadata alone does not make restored stock valid; its optional is cleared.
  state.RestoreState({.stock = CheckpointRef{.message = "Invalid"}});
  EXPECT_FALSE(state.GetState().stock);
}

TEST_F(SelectionPointTest, RestoreDiscardsInvalidHistoryReferences) {
  // Invalid IDs, including ones with metadata, are removed without reordering
  // the valid history entries.
  state.RestoreState(
      {.history = {{.id = 1}, {}, {.title = "Invalid"}, {.id = 2}}});
  EXPECT_THAT(state.history(),
              ElementsAre(CheckpointRef{.id = 1}, CheckpointRef{.id = 2}));
}

TEST_F(SelectionPointTest, RestoreEmptyHistoryRetainsConfiguredCapacity) {
  // Restoring no history still allows the configured three most recent marks.
  state.RestoreState({});
  for (CheckpointId id : {1, 2, 3, 4}) {
    current = {.id = id};
    state.Mark();
  }
  EXPECT_THAT(state.history(),
              ElementsAre(CheckpointRef{.id = 2}, CheckpointRef{.id = 3},
                          CheckpointRef{.id = 4}));
}

TEST_F(SelectionPointTest, RestoreShortHistoryRetainsConfiguredCapacity) {
  // One restored entry must not shrink the three-entry history on later marks.
  state.RestoreState({.history = {{.id = 1}}});
  for (CheckpointId id : {2, 3}) {
    current = {.id = id};
    state.Mark();
  }
  EXPECT_THAT(state.history(),
              ElementsAre(CheckpointRef{.id = 1}, CheckpointRef{.id = 2},
                          CheckpointRef{.id = 3}));
}

TEST_F(SelectionPointTest, RestoreOversizedHistoryRetainsNewestEntries) {
  // Four saved entries restored into a three-entry history keep the newest
  // three in their original order.
  state.RestoreState({.history = {{.id = 1}, {.id = 2}, {.id = 3}, {.id = 4}}});
  EXPECT_THAT(state.history(),
              ElementsAre(CheckpointRef{.id = 2}, CheckpointRef{.id = 3},
                          CheckpointRef{.id = 4}));
}

}  // namespace
