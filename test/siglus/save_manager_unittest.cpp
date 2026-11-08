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

#include "libsiglus/save_manager.hpp"

#include <gtest/gtest.h>

#include "utilities/file.hpp"

#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>

namespace siglus_test {
namespace fs = std::filesystem;
using namespace libsiglus;

static_assert(!std::is_copy_constructible_v<SaveManager>);
static_assert(!std::is_copy_assignable_v<SaveManager>);
static_assert(!std::is_move_constructible_v<SaveManager>);
static_assert(!std::is_move_assignable_v<SaveManager>);

class SiglusSaveManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    directory = scoped_tmp.dir();
    manager = std::make_unique<SaveManager>(
        directory,
        SaveCounts{
            .normal = 3, .quick = 2, .end = 1, .inner = 2, .selection = 3},
        [this] { return date; });
  }

  ScopedTemporary scoped_tmp;
  fs::path directory;
  SaveHeader date{.year = 2026, .month = 11, .day = 3, .weekday = 2};
  std::unique_ptr<SaveManager> manager;
};

TEST_F(SiglusSaveManagerTest, CountsClampToConfiguredBounds) {
  // Invalid configured capacities are clamped to the legacy ranges.
  const auto counts =
      SaveCounts{
          .normal = -1, .quick = 10001, .end = 2, .inner = 101, .selection = 0}
          .Clamped();
  EXPECT_EQ(counts.normal, 0);
  EXPECT_EQ(counts.quick, 10000);
  EXPECT_EQ(counts.end, 1);
  EXPECT_EQ(counts.inner, 100);
  EXPECT_EQ(counts.selection, 1);
}

TEST_F(SiglusSaveManagerTest, SlotTranslationUsesCombinedNamespace) {
  // Quick/end addresses offset into the combined range without narrower gates.
  EXPECT_EQ(manager->NormalSlot(5), 5);
  EXPECT_EQ(manager->QuickSlot(0), 3);
  EXPECT_EQ(manager->QuickSlot(2), 5);
  EXPECT_EQ(manager->EndSlot(), 5);
  EXPECT_FALSE(manager->QuickSlot(std::numeric_limits<int>::max()));
  EXPECT_FALSE(manager->NormalSlot(-1));
  EXPECT_FALSE(manager->EndSlot(1));
}

TEST_F(SiglusSaveManagerTest, SelectionHistoryUsesConfiguredCapacity) {
  // The manager's three-entry capacity retains only the newest three marks.
  for (int i = 0; i < 4; ++i) {
    manager->Capture("state");
    manager->selection_points().Mark();
  }
  const SaveManager& saved = *manager;
  const auto history = saved.selection_points().history();
  ASSERT_EQ(history.size(), 3);
  EXPECT_EQ(history.front().id, 2);
  EXPECT_EQ(history.back().id, 4);
}

TEST_F(SiglusSaveManagerTest, CapturePreservesBinaryRuntimeState) {
  // A checkpoint stores opaque bytes without losing embedded nulls.
  const std::string bytes("runtime\0state", 13);
  const auto id = manager->Capture(bytes, "Scene");
  EXPECT_EQ(manager->Blob(id).runtime_state, bytes);
  EXPECT_EQ(manager->active().title, "Scene");
}

TEST_F(SiglusSaveManagerTest, EmptyRuntimeCannotReplaceCheckpoint) {
  // Rejecting an empty capture leaves the prior checkpoint available.
  manager->Capture("valid");
  const auto before = manager->active();
  EXPECT_THROW(manager->Capture(""), std::invalid_argument);
  EXPECT_EQ(manager->active(), before);
}

TEST_F(SiglusSaveManagerTest, SelectionCopiesFreezeMetadata) {
  // Stock metadata freezes before subsequent output updates the live reference.
  manager->Capture("state");
  manager->AppendMessage("first");
  manager->selection_points().Stack();
  manager->AppendMessage("second");
  manager->selection_points().Drop();
  ASSERT_TRUE(manager->LatestSelection());
  EXPECT_EQ(manager->LatestSelection()->message, "first");
  EXPECT_EQ(manager->active().message, "firstsecond");
}

TEST_F(SiglusSaveManagerTest, MetadataLimitCountsUtf8Characters) {
  // Message limits preserve 256 complete Japanese characters in memory.
  manager->Capture("state");
  std::string text;
  for (int i = 0; i < 300; ++i)
    text += "名";
  manager->AppendMessage(text);
  EXPECT_EQ(manager->active().message.size(), 256 * 3);
  EXPECT_EQ(manager->active().full_message, manager->active().message);
}

TEST_F(SiglusSaveManagerTest, ShortAndFullMessagesCanDiffer) {
  // Separate displayed/full metadata supplied by the host remains independent.
  manager->Capture("state");
  manager->AppendMessage("display", "full");
  EXPECT_EQ(manager->active().message, "display");
  EXPECT_EQ(manager->active().full_message, "full");
}

TEST_F(SiglusSaveManagerTest, MessageResetPreservesCopiedSelectionMetadata) {
  // Replay resets live metadata while historical checkpoint copies stay frozen.
  manager->Capture("state");
  manager->AppendMessage("previous");
  manager->selection_points().Mark();
  manager->ResetMessages("restored prefix");
  EXPECT_TRUE(manager->active().message.empty());
  EXPECT_EQ(manager->active().full_message, "restored prefix");
  EXPECT_EQ(manager->LatestSelection()->message, "previous");
}

TEST_F(SiglusSaveManagerTest, SelectionReadsResetMessageMetadata) {
  // New marks read the reset active metadata without synchronizing another
  // copy.
  manager->Capture("state");
  manager->AppendMessage("previous");
  manager->ResetMessages("restored prefix");
  manager->selection_points().Mark();
  const auto latest = manager->LatestSelection();
  ASSERT_TRUE(latest);
  EXPECT_TRUE(latest->message.empty());
  EXPECT_EQ(latest->full_message, "restored prefix");
}

TEST_F(SiglusSaveManagerTest, ClearCheckpointPreventsNewSelectionPoints) {
  // Clearing the owner's active reference makes subsequent marking a no-op.
  manager->Capture("state");
  manager->ClearCheckpoint();
  manager->selection_points().Mark();
  EXPECT_FALSE(manager->LatestSelection());
}

TEST_F(SiglusSaveManagerTest, ResetClearsActiveCheckpointAndSelectionState) {
  // A manager reset removes the source, stock, and history together.
  manager->Capture("state");
  manager->selection_points().Mark();
  manager->selection_points().Stack();
  manager->Reset();
  manager->selection_points().Mark();
  manager->selection_points().Drop();
  EXPECT_FALSE(manager->HasCheckpoint());
  EXPECT_FALSE(manager->LatestSelection());
}

TEST_F(SiglusSaveManagerTest, ClearPreservesInnerCheckpoint) {
  // Clearing the source leaves its copied inner save and state blob alive.
  const auto id = manager->Capture("state");
  ASSERT_TRUE(manager->SaveInner(0));
  manager->ClearCheckpoint();
  EXPECT_FALSE(manager->HasCheckpoint());
  EXPECT_TRUE(manager->HasInner(0));
  EXPECT_EQ(manager->Blob(id).runtime_state, "state");
}

TEST_F(SiglusSaveManagerTest, EmptyInnerCopyClearsDestination) {
  // Copying an allocated empty source removes the occupied destination.
  manager->Capture("state");
  ASSERT_TRUE(manager->SaveInner(0));
  ASSERT_TRUE(manager->CopyInner(1, 0));
  EXPECT_FALSE(manager->HasInner(0));
}

TEST_F(SiglusSaveManagerTest, InvalidInnerIndexDoesNotChangeSlots) {
  // Invalid storage indices report failure without modifying a valid inner
  // save.
  manager->Capture("state");
  manager->SaveInner(0);
  const auto before = manager->GetInner(0);
  EXPECT_FALSE(manager->CopyInner(-1, 0));
  EXPECT_FALSE(manager->ClearInner(2));
  EXPECT_FALSE(manager->SaveInner(2));
  EXPECT_EQ(manager->GetInner(0), before);
}

TEST_F(SiglusSaveManagerTest, RepeatedReferencesShareOneStateBlob) {
  // Multiple marks and inner copies retain one payload for a captured state.
  manager->Capture("state");
  manager->selection_points().Mark();
  manager->selection_points().Mark();
  manager->SaveInner(0);
  manager->SaveInner(1);
  EXPECT_EQ(manager->MakePayload().blobs.size(), 1);
}

TEST_F(SiglusSaveManagerTest, StrongInnerReferencesRetainOlderState) {
  // A later snapshot references its saved inner state by ID, preserving both.
  const auto old = manager->Capture("old");
  manager->SaveInner(0);
  const auto current = manager->Capture("current");
  const auto payload = manager->MakePayload();
  EXPECT_EQ(payload.blobs.size(), 2);
  EXPECT_EQ(payload.blobs.at(current).inner[0].id, old);
}

TEST_F(SiglusSaveManagerTest, WeakHistoryIdsDoNotRetainEvictedState) {
  // An inner snapshot remembering old history does not retain evicted blobs.
  manager->Capture("first");
  manager->selection_points().Mark();
  const auto second = manager->Capture("second");
  manager->SaveInner(0);
  manager->selection_points().Clear();
  manager->Capture("third");
  const auto payload = manager->MakePayloadFor(manager->GetInner(0));
  EXPECT_EQ(payload.active.id, second);
  EXPECT_EQ(payload.blobs.size(), 1);
}

TEST_F(SiglusSaveManagerTest, RestoreIntersectsCapturedSelectionHistory) {
  // Restoring the second point keeps only history available before its capture.
  const auto first = manager->Capture("first");
  manager->selection_points().Mark();
  manager->Capture("second");
  manager->selection_points().Mark();
  const auto selected = *manager->LatestSelection();
  manager->Capture("third");
  const auto payload = manager->MakePayloadFor(selected);
  manager->RestorePayload(payload);
  ASSERT_EQ(manager->selection_points().history().size(), 1);
  EXPECT_EQ(manager->selection_points().history().front().id, first);
  EXPECT_EQ(manager->active(), selected);
}

TEST_F(SiglusSaveManagerTest, InvalidRestorePreservesStorage) {
  // Missing strong references fail validation before active storage is
  // replaced.
  manager->Capture("valid");
  auto payload = manager->MakePayload();
  payload.blobs.at(payload.active.id).stock.id = 99;
  EXPECT_THROW(manager->RestorePayload(payload), std::runtime_error);
  EXPECT_EQ(manager->Blob(manager->active().id).runtime_state, "valid");
}

TEST_F(SiglusSaveManagerTest, InvalidRestorePreservesSelectionState) {
  // A missing checkpoint blob is rejected before replacing selection state.
  manager->Capture("valid");
  manager->selection_points().Mark();
  manager->selection_points().Stack();
  const auto before = manager->selection_points().GetState();
  auto payload = manager->MakePayload();
  payload.blobs.clear();
  EXPECT_THROW(manager->RestorePayload(payload), std::runtime_error);
  const auto after = manager->selection_points().GetState();
  EXPECT_EQ(after.history, before.history);
  EXPECT_EQ(after.stock, before.stock);
}

TEST_F(SiglusSaveManagerTest, CyclicCheckpointGraphIsRejected) {
  // A malformed self-reference is rejected rather than traversed recursively.
  manager->Capture("valid");
  auto payload = manager->MakePayload();
  payload.blobs.at(payload.active.id).stock = payload.active;
  EXPECT_THROW(SaveManager::Validate(payload), std::runtime_error);
}

TEST_F(SiglusSaveManagerTest, DiskRoundTripIncludesLocalExtraAtWriteTime) {
  // Disk storage copies the established checkpoint and current extra settings.
  manager->Capture(std::string("old\0bytes", 9), "Title", "append", "Name");
  SaveLocalExtra extra;
  extra.modes[1].mode = 7;
  ASSERT_TRUE(manager->SaveSlot(0, extra)) << manager->last_error();
  const auto loaded = manager->LoadSlot(0);
  ASSERT_TRUE(loaded) << manager->last_error();
  EXPECT_EQ(*loaded, manager->MakePayload(extra));
}

TEST_F(SiglusSaveManagerTest, DiskRoundTripPreservesReferenceMetadata) {
  // All metadata fields, including embedded nulls, survive a disk round trip
  // and restoration of the current reference.
  manager->Capture("state", "Title", "directory", "name");
  manager->AppendMessage(std::string("a\0b", 3), "Full message");
  const auto expected = manager->active();
  ASSERT_TRUE(manager->SaveSlot(0)) << manager->last_error();
  const auto loaded = manager->LoadSlot(0);
  ASSERT_TRUE(loaded) << manager->last_error();
  manager->Reset();
  manager->RestorePayload(*loaded);
  manager->selection_points().Mark();
  EXPECT_EQ(manager->LatestSelection(), expected);
}

TEST_F(SiglusSaveManagerTest, DiskRoundTripRestoresSelectionMetadata) {
  // History retains its own metadata when saved after later output and restored
  // into empty storage.
  manager->Capture("first", "Title");
  manager->AppendMessage("marked", "Full message");
  manager->selection_points().Mark();
  const auto expected = manager->active();
  manager->AppendMessage("later");
  manager->Capture("second");
  ASSERT_TRUE(manager->SaveSlot(0)) << manager->last_error();
  const auto loaded = manager->LoadSlot(0);
  ASSERT_TRUE(loaded) << manager->last_error();
  manager->Reset();
  manager->RestorePayload(*loaded);
  ASSERT_EQ(manager->selection_points().history().size(), 1);
  EXPECT_EQ(manager->selection_points().history().front(), expected);
}

TEST_F(SiglusSaveManagerTest, DiskRoundTripRestoresStockedMetadata) {
  // A later checkpoint captures stocked metadata; loading it makes that frozen
  // reference available to drop again.
  manager->Capture("first", "Title");
  manager->AppendMessage("stocked", "Full message");
  manager->selection_points().Stack();
  const auto expected = manager->active();
  manager->AppendMessage("later");
  manager->Capture("second");
  ASSERT_TRUE(manager->SaveSlot(0)) << manager->last_error();
  const auto loaded = manager->LoadSlot(0);
  ASSERT_TRUE(loaded) << manager->last_error();
  manager->Reset();
  manager->RestorePayload(*loaded);
  manager->selection_points().Drop();
  EXPECT_EQ(manager->LatestSelection(), expected);
}

TEST_F(SiglusSaveManagerTest, ReadHeaderUsesWriteTimeCalendar) {
  // The slot timestamp is read from the clock when written, after capture.
  manager->Capture("state");
  date.hour = 13;
  date.millisecond = 123;
  ASSERT_TRUE(manager->SaveSlot(0));
  const auto header = manager->ReadHeader(0);
  ASSERT_TRUE(header);
  EXPECT_EQ(header->hour, 13);
  EXPECT_EQ(header->millisecond, 123);
}

TEST_F(SiglusSaveManagerTest, OverwriteResetsCustomHeaderFields) {
  // A fresh slot write discards comments and custom values from the old header.
  manager->Capture("state");
  ASSERT_TRUE(manager->SaveSlot(0));
  ASSERT_TRUE(manager->SetComment(0, "comment"));
  const std::array values{4, 5};
  ASSERT_TRUE(manager->SetValues(0, values));
  ASSERT_TRUE(manager->SaveSlot(0));
  const auto header = manager->ReadHeader(0);
  ASSERT_TRUE(header);
  EXPECT_TRUE(header->comment.empty());
  EXPECT_EQ(header->values, (std::array<int, 256>{}));
}

TEST_F(SiglusSaveManagerTest, HeaderEditsPreservePayload) {
  // Comment/value edits rewrite metadata while preserving the compressed state.
  manager->Capture("state");
  ASSERT_TRUE(manager->SaveSlot(0));
  const auto payload = manager->LoadSlot(0);
  ASSERT_TRUE(manager->SetComment(0, "edited"));
  const std::array values{8, 9};
  ASSERT_TRUE(manager->SetValues(0, values));
  EXPECT_EQ(manager->LoadSlot(0), payload);
  const auto header = manager->ReadHeader(0);
  ASSERT_TRUE(header);
  EXPECT_EQ(header->comment, "edited");
  EXPECT_EQ(header->values[1], 9);
}

TEST_F(SiglusSaveManagerTest, NewestQuickRangeReadsSuccessfulSlots) {
  // Ranged quick lookup returns the newest existing quick-relative index.
  manager->Capture("state");
  ASSERT_TRUE(manager->SaveSlot(*manager->QuickSlot(0)));
  date.second = 1;
  ASSERT_TRUE(manager->SaveSlot(*manager->QuickSlot(1)));
  EXPECT_EQ(manager->Newest(0, 2, true), 1);
}

TEST_F(SiglusSaveManagerTest, EmptyNewestRangeReturnsMinusOne) {
  // Missing candidates and nonpositive counts never manufacture a slot number.
  EXPECT_EQ(manager->Newest(), -1);
  EXPECT_EQ(manager->Newest(-1, 0, true), -1);
}

TEST_F(SiglusSaveManagerTest, CorruptLoadDoesNotMutateActiveCheckpoint) {
  // Truncated disk bytes fail loading while the running checkpoint stays
  // intact.
  manager->Capture("state");
  ASSERT_TRUE(manager->SaveSlot(0));
  const auto before = manager->active();
  auto bytes = LoadFileStr(manager->SlotPath(0));
  bytes.resize(bytes.size() / 2);
  WriteFile(manager->SlotPath(0), bytes);
  EXPECT_FALSE(manager->LoadSlot(0));
  EXPECT_EQ(manager->active(), before);
  EXPECT_FALSE(manager->last_error().empty());
}

TEST_F(SiglusSaveManagerTest, ForeignNativeSaveIsRejected) {
  // Files without the RLVM signature are never interpreted as this archive.
  WriteFile(manager->SlotPath(0), "native engine save");
  EXPECT_FALSE(manager->ReadHeader(0));
  EXPECT_FALSE(manager->LoadSlot(0));
}

TEST_F(SiglusSaveManagerTest, CopyIncludesMetadataAndThumbnail) {
  // Slot copies preserve the header and prepared thumbnail bytes together.
  manager->SetThumbnailExtension("png");
  manager->Capture("state");
  ASSERT_TRUE(manager->SaveSlot(0, {}, "image"));
  ASSERT_TRUE(manager->SetComment(0, "copied"));
  ASSERT_TRUE(manager->CopySlot(0, 1)) << manager->last_error();
  EXPECT_EQ(manager->ReadHeader(0), manager->ReadHeader(1));
  EXPECT_EQ(LoadFileStr(manager->ThumbnailPath(1)), "image");
}

TEST_F(SiglusSaveManagerTest, SwapExchangesOccupiedSlots) {
  // Two occupied slots exchange complete payloads rather than only headers.
  manager->Capture("first");
  ASSERT_TRUE(manager->SaveSlot(0));
  manager->Capture("second");
  ASSERT_TRUE(manager->SaveSlot(1));
  const auto first = manager->LoadSlot(0);
  const auto second = manager->LoadSlot(1);
  ASSERT_TRUE(manager->SwapSlots(0, 1));
  EXPECT_EQ(manager->LoadSlot(0), second);
  EXPECT_EQ(manager->LoadSlot(1), first);
}

TEST_F(SiglusSaveManagerTest, SwapMovesAnOccupiedSlotIntoAnEmptyOne) {
  // Changing one occupied slot and one empty slot moves the source file.
  manager->Capture("state");
  ASSERT_TRUE(manager->SaveSlot(0));
  ASSERT_TRUE(manager->SwapSlots(0, 1));
  EXPECT_FALSE(manager->Exists(0));
  EXPECT_TRUE(manager->Exists(1));
}

TEST_F(SiglusSaveManagerTest, DeleteRemovesSlotAndThumbnail) {
  // Deleting an existing slot removes both artifacts and invalidates existence.
  manager->SetThumbnailExtension("png");
  manager->Capture("state");
  ASSERT_TRUE(manager->SaveSlot(0, {}, "image"));
  ASSERT_TRUE(manager->DeleteSlot(0));
  EXPECT_FALSE(manager->Exists(0));
  EXPECT_FALSE(fs::exists(manager->ThumbnailPath(0)));
}

TEST_F(SiglusSaveManagerTest, InvalidDiskSaveDoesNotCreateFiles) {
  // Saving outside the combined range fails without creating a destination.
  manager->Capture("state");
  EXPECT_FALSE(manager->SaveSlot(6));
  EXPECT_FALSE(fs::exists(manager->SlotPath(6)));
}

}  // namespace siglus_test
