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

#include "utilities/file.hpp"

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
namespace fs = std::filesystem;

class FileTransactionTest : public ::testing::Test {
 protected:
  fs::path Path(std::string_view name) const { return temporary_.dir() / name; }

  std::set<fs::path> Entries() const {
    std::set<fs::path> entries;
    for (const auto& entry : fs::directory_iterator(temporary_.dir()))
      entries.insert(entry.path().filename());
    return entries;
  }

  void StageRenameFailure() {
    // The destination fits, but appending a backup suffix makes rename fail.
    const auto name_limit = ::pathconf(temporary_.dir().c_str(), _PC_NAME_MAX);
    ASSERT_GT(name_limit, 0);
    rename_failure_ = Path(std::string(name_limit, 'x'));
    WriteFile(rename_failure_, "unchanged");
    transaction_.Stage(rename_failure_, std::nullopt);
  }

  ScopedTemporary temporary_;
  FileTransaction transaction_;
  fs::path rename_failure_;
};

TEST_F(FileTransactionTest, StagingLeavesFilesUntouched) {
  // With existing files, staging writes and deletion leaves all paths
  // unchanged.
  WriteFile(Path("replace"), "old");
  WriteFile(Path("delete"), "keep");

  transaction_.Stage(Path("replace"), "new");
  transaction_.Stage(Path("delete"), std::nullopt);
  transaction_.Stage(Path("create"), "new");

  EXPECT_EQ(LoadFileStr(Path("replace")), "old");
  EXPECT_EQ(LoadFileStr(Path("delete")), "keep");
  EXPECT_EQ(Entries(), (std::set<fs::path>{"replace", "delete"}));
}

TEST_F(FileTransactionTest, DestructionDiscardsUncommittedOperations) {
  // With a pending replacement, destruction leaves the original file intact.
  WriteFile(Path("file"), "old");
  {
    FileTransaction transaction;
    transaction.Stage(Path("file"), "new");
  }

  EXPECT_EQ(LoadFileStr(Path("file")), "old");
  EXPECT_EQ(Entries(), (std::set<fs::path>{"file"}));
}

TEST_F(FileTransactionTest, CreatesFileWithBinaryContent) {
  // With a missing destination, commit writes every byte, including nulls.
  const std::string content("first\0second", 12);
  transaction_.Stage(Path("file"), content);

  transaction_.Commit();

  EXPECT_EQ(LoadFileStr(Path("file")), content);
}

TEST_F(FileTransactionTest, ReplacesExistingFile) {
  // With an existing destination, commit replaces its complete contents.
  WriteFile(Path("file"), "long original content");
  transaction_.Stage(Path("file"), "new");

  transaction_.Commit();

  EXPECT_EQ(LoadFileStr(Path("file")), "new");
}

TEST_F(FileTransactionTest, EmptyContentWritesAnEmptyFile) {
  // With an existing destination, empty content truncates rather than deletes
  // it.
  WriteFile(Path("file"), "old");
  transaction_.Stage(Path("file"), std::string());

  transaction_.Commit();

  EXPECT_EQ(LoadFileStr(Path("file")), "");
}

TEST_F(FileTransactionTest, DeletesExistingFile) {
  // With an existing destination, committing nullopt removes the file.
  WriteFile(Path("file"), "old");
  transaction_.Stage(Path("file"), std::nullopt);

  transaction_.Commit();

  EXPECT_FALSE(fs::exists(Path("file")));
}

TEST_F(FileTransactionTest, DeletingMissingFileSucceeds) {
  // With a missing destination, committing deletion succeeds without artifacts.
  transaction_.Stage(Path("missing"), std::nullopt);

  EXPECT_NO_THROW(transaction_.Commit());

  EXPECT_TRUE(Entries().empty());
}

TEST_F(FileTransactionTest, CommitsMixedOperationsTogether) {
  // With mixed destinations, one commit applies every staged write and
  // deletion.
  WriteFile(Path("replace"), "old");
  WriteFile(Path("delete"), "old");
  transaction_.Stage(Path("replace"), "replacement");
  transaction_.Stage(Path("delete"), std::nullopt);
  transaction_.Stage(Path("create"), "created");

  transaction_.Commit();

  EXPECT_EQ(LoadFileStr(Path("replace")), "replacement");
  EXPECT_EQ(LoadFileStr(Path("create")), "created");
  EXPECT_FALSE(fs::exists(Path("delete")));
}

TEST_F(FileTransactionTest, CreatesMissingParentDirectories) {
  // With missing ancestors, committing a write creates the destination's
  // parents.
  transaction_.Stage(Path("nested/directory/file"), "content");

  transaction_.Commit();

  EXPECT_EQ(LoadFileStr(Path("nested/directory/file")), "content");
}

TEST_F(FileTransactionTest, AcceptsRelativeFilenameWithoutParent) {
  // With a temporary current directory, a bare filename commits successfully.
  ScopedCurrentPath cwd(temporary_.dir());
  transaction_.Stage("file", "content");

  transaction_.Commit();

  EXPECT_EQ(LoadFileStr(Path("file")), "content");
}

TEST_F(FileTransactionTest, ResolvesRelativePathsWhenStaged) {
  // With a staged relative path, changing directories cannot retarget the
  // write.
  ScopedCurrentPath cwd(temporary_.dir());
  transaction_.Stage("file", "content");
  fs::create_directory(Path("other"));
  ScopedCurrentPath other_cwd(Path("other"));

  transaction_.Commit();

  EXPECT_EQ(LoadFileStr(Path("file")), "content");
  EXPECT_FALSE(fs::exists(Path("other/file")));
}

TEST_F(FileTransactionTest, EmptyCommitLeavesFilesUntouched) {
  // With an existing file and no operations, committing leaves it untouched.
  WriteFile(Path("file"), "old");

  EXPECT_NO_THROW(transaction_.Commit());

  EXPECT_EQ(LoadFileStr(Path("file")), "old");
  EXPECT_EQ(Entries(), (std::set<fs::path>{"file"}));
}

TEST_F(FileTransactionTest, DuplicateDestinationPreservesStagedOperation) {
  // With a staged replacement, rejecting a duplicate preserves the first write.
  WriteFile(Path("file"), "old");
  transaction_.Stage(Path("file"), "first");

  EXPECT_THROW(transaction_.Stage(Path("file"), std::nullopt),
               std::invalid_argument);

  EXPECT_EQ(LoadFileStr(Path("file")), "old");
  transaction_.Commit();
  EXPECT_EQ(LoadFileStr(Path("file")), "first");
}

TEST_F(FileTransactionTest, RejectsLexicallyEquivalentDestinations) {
  // With a staged path, an equivalent dotted spelling is rejected as a
  // duplicate.
  transaction_.Stage(Path("file"), "first");

  EXPECT_THROW(transaction_.Stage(Path("./file"), "second"),
               std::invalid_argument);
}

TEST_F(FileTransactionTest, RejectsRelativeAndAbsoluteAliases) {
  // With a relative destination, staging its absolute spelling is also
  // rejected.
  ScopedCurrentPath cwd(temporary_.dir());
  transaction_.Stage("file", "first");

  EXPECT_THROW(transaction_.Stage(Path("file"), "second"),
               std::invalid_argument);
}

TEST_F(FileTransactionTest, PreparationFailurePreservesOriginalFiles) {
  // With a later directory destination, failed preparation preserves prior
  // files.
  WriteFile(Path("file"), "old");
  fs::create_directory(Path("blocked"));
  transaction_.Stage(Path("file"), "new");
  transaction_.Stage(Path("blocked"), "invalid");

  EXPECT_THROW(transaction_.Commit(), std::runtime_error);

  EXPECT_EQ(LoadFileStr(Path("file")), "old");
  EXPECT_TRUE(fs::is_directory(Path("blocked")));
}

TEST_F(FileTransactionTest, PreparationFailureLeavesNewDestinationsAbsent) {
  // With a later directory destination, failed preparation installs no new
  // files.
  fs::create_directory(Path("blocked"));
  transaction_.Stage(Path("new"), "content");
  transaction_.Stage(Path("blocked"), "invalid");

  EXPECT_THROW(transaction_.Commit(), std::runtime_error);

  EXPECT_FALSE(fs::exists(Path("new")));
}

TEST_F(FileTransactionTest, PreparationFailureRemovesTemporaryFiles) {
  // With an earlier prepared write, failed preparation removes its temporary
  // file.
  fs::create_directory(Path("blocked"));
  transaction_.Stage(Path("new"), "content");
  transaction_.Stage(Path("blocked"), "invalid");

  EXPECT_THROW(transaction_.Commit(), std::runtime_error);

  EXPECT_EQ(Entries(), (std::set<fs::path>{"blocked"}));
}

TEST_F(FileTransactionTest, ParentCreationFailurePreservesOriginalFiles) {
  // With a file blocking a later parent directory, failure preserves originals.
  WriteFile(Path("file"), "old");
  WriteFile(Path("blocked"), "parent is a file");
  transaction_.Stage(Path("file"), "new");
  transaction_.Stage(Path("blocked/child"), "invalid");

  EXPECT_THROW(transaction_.Commit(), fs::filesystem_error);

  EXPECT_EQ(LoadFileStr(Path("file")), "old");
  EXPECT_EQ(LoadFileStr(Path("blocked")), "parent is a file");
}

TEST_F(FileTransactionTest, RollbackRestoresReplacedFiles) {
  // With an installed replacement, a later rename failure restores original
  // bytes.
  WriteFile(Path("file"), "old");
  transaction_.Stage(Path("file"), "new");
  ASSERT_NO_FATAL_FAILURE(StageRenameFailure());

  EXPECT_THROW(transaction_.Commit(), fs::filesystem_error);

  EXPECT_EQ(LoadFileStr(Path("file")), "old");
}

TEST_F(FileTransactionTest, RollbackRestoresDeletedFiles) {
  // With an applied deletion, a later rename failure restores the deleted file.
  WriteFile(Path("file"), "old");
  transaction_.Stage(Path("file"), std::nullopt);
  ASSERT_NO_FATAL_FAILURE(StageRenameFailure());

  EXPECT_THROW(transaction_.Commit(), fs::filesystem_error);

  EXPECT_EQ(LoadFileStr(Path("file")), "old");
}

TEST_F(FileTransactionTest, RollbackRemovesCreatedFiles) {
  // With an installed new file, a later rename failure restores its absence.
  transaction_.Stage(Path("file"), "new");
  ASSERT_NO_FATAL_FAILURE(StageRenameFailure());

  EXPECT_THROW(transaction_.Commit(), fs::filesystem_error);

  EXPECT_FALSE(fs::exists(Path("file")));
}

TEST_F(FileTransactionTest, RollbackRemovesTemporaryAndBackupFiles) {
  // With installed and pending writes, rollback removes both kinds of
  // artifacts.
  WriteFile(Path("file"), "old");
  transaction_.Stage(Path("file"), "new");
  ASSERT_NO_FATAL_FAILURE(StageRenameFailure());
  transaction_.Stage(Path("pending"), "not installed");

  EXPECT_THROW(transaction_.Commit(), fs::filesystem_error);

  EXPECT_EQ(Entries(),
            (std::set<fs::path>{"file", rename_failure_.filename()}));
}

TEST_F(FileTransactionTest, SuccessfulCommitRemovesTemporaryAndBackupFiles) {
  // With new and replaced files, success leaves only the requested
  // destinations.
  WriteFile(Path("replace"), "old");
  transaction_.Stage(Path("replace"), "new");
  transaction_.Stage(Path("create"), "created");

  transaction_.Commit();

  EXPECT_EQ(Entries(), (std::set<fs::path>{"replace", "create"}));
}

TEST_F(FileTransactionTest, SuccessfulCommitClearsStagedOperations) {
  // After a successful commit, an empty second commit cannot replay stale
  // writes.
  transaction_.Stage(Path("file"), "first");
  transaction_.Commit();
  WriteFile(Path("file"), "external change");

  transaction_.Commit();

  EXPECT_EQ(LoadFileStr(Path("file")), "external change");
}

TEST_F(FileTransactionTest, CanStageAnotherBatchAfterSuccess) {
  // After success, the same destination can be staged again for a later commit.
  transaction_.Stage(Path("file"), "first");
  transaction_.Commit();

  ASSERT_NO_THROW(transaction_.Stage(Path("file"), "second"));
  transaction_.Commit();

  EXPECT_EQ(LoadFileStr(Path("file")), "second");
}

TEST_F(FileTransactionTest, FailedCommitCanBeRetried) {
  // After fixing a preparation failure, retry applies the retained operations.
  WriteFile(Path("file"), "old");
  fs::create_directory(Path("blocked"));
  transaction_.Stage(Path("file"), "new");
  transaction_.Stage(Path("blocked"), "now writable");
  ASSERT_THROW(transaction_.Commit(), std::runtime_error);
  fs::remove(Path("blocked"));

  transaction_.Commit();

  EXPECT_EQ(LoadFileStr(Path("file")), "new");
  EXPECT_EQ(LoadFileStr(Path("blocked")), "now writable");
}

}  // namespace
