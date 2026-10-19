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

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

TEST(WriteFileTest, RelativeFilenameWithoutParent) {
  ScopedTemporary root;
  ScopedCurrentPath cwd(root.dir());

  ASSERT_NO_THROW(WriteFile("file.txt", "test content"));
  EXPECT_EQ(LoadFileStr("file.txt"), "test content");
}

class CorrectPathTest : public ::testing::Test {
 protected:
  fs::path root_;

  void SetUp() override {
    // Unique temporary directory for every test.
    static std::atomic<unsigned long long> counter{0};

    const auto timestamp =
        std::chrono::steady_clock::now().time_since_epoch().count();

    const std::string name = "CorrectPathTest_" + std::to_string(timestamp) +
                             "_" + std::to_string(counter.fetch_add(1));

    root_ = fs::temp_directory_path() / name;

    ASSERT_TRUE(fs::create_directory(root_));
  }

  void TearDown() override {
    if (!root_.empty()) {
      std::error_code ec;
      fs::remove_all(root_, ec);
      EXPECT_FALSE(static_cast<bool>(ec)) << ec.message();
    }
  }

  // Creates a real file inside the temporary root.
  void MakeFile(const fs::path& relativePath,
                std::string content = "test content") {
    ASSERT_NO_THROW({ WriteFile(root_ / relativePath, content); });
  }
};

TEST_F(CorrectPathTest, ExactCase) {
  const auto expected = root_ / "Assets" / "Textures" / "Player.PNG";
  MakeFile(fs::path("Assets") / "Textures" / "Player.PNG");
  EXPECT_EQ(CorrectPath(root_, "Assets/Textures/Player.PNG"), expected);
}

TEST_F(CorrectPathTest, IncorrectCase) {
  const auto expected = root_ / "Assets" / "Textures" / "Player.PNG";
  MakeFile(fs::path("Assets") / "Textures" / "Player.PNG");
  EXPECT_EQ(CorrectPath(root_, "assets/textures/player.png"), expected);
}

TEST_F(CorrectPathTest, WindowsSeparators) {
  const auto expected = root_ / "Assets" / "Textures" / "Player.PNG";
  MakeFile(fs::path("Assets") / "Textures" / "Player.PNG");
  EXPECT_EQ(CorrectPath(root_, R"(assets\textures\player.png)"), expected);
}

TEST_F(CorrectPathTest, MixedSeparators) {
  const auto expected = root_ / "Assets" / "Textures" / "Player.PNG";
  MakeFile(fs::path("Assets") / "Textures" / "Player.PNG");
  EXPECT_EQ(CorrectPath(root_, R"(assets\textures/player.png)"), expected);
}

TEST_F(CorrectPathTest, NestedDirectories) {
  const auto expected =
      root_ / "Assets" / "Models" / "Characters" / "Hero" / "Body.MESH";
  MakeFile(fs::path("Assets") / "Models" / "Characters" / "Hero" / "Body.MESH");
  EXPECT_EQ(CorrectPath(root_, R"(assets\models/characters\hero/body.mesh)"),
            expected);
}

TEST_F(CorrectPathTest, DotAndRepeatedSeparators) {
  const auto expected = root_ / "Assets" / "Textures" / "Player.PNG";
  MakeFile(fs::path("Assets") / "Textures" / "Player.PNG");
  EXPECT_EQ(CorrectPath(root_, R"(./assets//textures\\player.png)"), expected);
}

TEST_F(CorrectPathTest, MissingFile) {
  MakeFile(fs::path("Assets") / "Textures" / "Player.PNG");
  EXPECT_TRUE(CorrectPath(root_, "assets/textures/missing.png").empty());
}

TEST_F(CorrectPathTest, MissingIntermediateDirectory) {
  MakeFile(fs::path("Assets") / "Textures" / "Player.PNG");
  EXPECT_TRUE(CorrectPath(root_, "assets/models/player.png").empty());
}

TEST_F(CorrectPathTest, EmptyFilename) {
  EXPECT_TRUE(CorrectPath(root_, "").empty());
}

TEST_F(CorrectPathTest, MissingRoot) {
  MakeFile(fs::path("file.txt"));
  EXPECT_TRUE(CorrectPath(root_ / "DoesNotExist", "file.txt").empty());
}

TEST_F(CorrectPathTest, DirectoryIsNotFile) {
  MakeFile(fs::path("Assets") / "Textures" / "Player.PNG");
  EXPECT_TRUE(CorrectPath(root_, "assets/textures").empty());
}

TEST_F(CorrectPathTest, IntermediateComponentIsFile) {
  MakeFile("Data.txt");
  EXPECT_TRUE(CorrectPath(root_, "data.txt/child.txt").empty());
}

TEST_F(CorrectPathTest, RejectsAbsoluteFilename) {
  MakeFile("Player.PNG");
  const auto absolute = root_ / "Player.PNG";
  EXPECT_TRUE(CorrectPath(root_, absolute.string()).empty());
}

TEST_F(CorrectPathTest, RejectsWindowsDrivePath) {
  EXPECT_TRUE(CorrectPath(root_, R"(C:\Games\Assets\Player.PNG)").empty());
}

TEST_F(CorrectPathTest, RejectWindowsUNCPath) {
  EXPECT_TRUE(CorrectPath(root_, R"(\\server\share\Player.PNG)").empty());
}

TEST_F(CorrectPathTest, RejectParentTraversalBeyondRoot) {
  EXPECT_TRUE(CorrectPath(root_, "../outside.txt").empty());
  EXPECT_TRUE(CorrectPath(root_, "Assets/../../outside.txt").empty());
}

TEST_F(CorrectPathTest, RootIsNotDirectory) {
  MakeFile("Root.txt");
  EXPECT_TRUE(CorrectPath(root_ / "Root.txt", "child.txt").empty());
}

TEST_F(CorrectPathTest, UnicodeFilename) {
  const auto relative =
      fs::path("Assets") / fs::path(u8"資料") / fs::path(u8"ファイル.txt");
  MakeFile(relative);
  EXPECT_EQ(CorrectPath(root_, "assets\\資料\\ファイル.txt"), root_ / relative);
}

TEST_F(CorrectPathTest, PreferExactCaseMatch) {
  ASSERT_TRUE(fs::create_directory(root_ / "data"));

  std::error_code ec;
  const bool created = fs::create_directory(root_ / "Data", ec);

  ASSERT_FALSE(static_cast<bool>(ec)) << ec.message();

  if (!created) {
    GTEST_SKIP() << "Filesystem does not distinguish "
                    "directories by capitalization";
  }

  MakeFile(fs::path("data") / "lower.txt");
  MakeFile(fs::path("Data") / "UPPER.TXT");

  EXPECT_EQ(CorrectPath(root_, "Data/UPPER.TXT"), root_ / "Data" / "UPPER.TXT");
  EXPECT_EQ(CorrectPath(root_, "data/lower.txt"), root_ / "data" / "lower.txt");
}
