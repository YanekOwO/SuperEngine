// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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

#include "core/message_back.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <vector>

using Entry = MessageBack::MsgbkEntry;

void ExpectDefaultEntry(const Entry& entry) {
  EXPECT_TRUE(entry.msg.empty());
  EXPECT_TRUE(entry.original_name.empty());
  EXPECT_TRUE(entry.display_name.empty());
  EXPECT_TRUE(entry.debug_msg.empty());
  EXPECT_TRUE(entry.koe_no_list.empty());
  EXPECT_TRUE(entry.chara_no_list.empty());
  EXPECT_EQ(entry.scene_no, -1);
  EXPECT_EQ(entry.line_no, -1);
  EXPECT_FALSE(entry.pct_flag);
}

std::string Message(int number) { return "message " + std::to_string(number); }

void AddCompletedMessage(MessageBack& message_back, int number) {
  message_back.AddMessage(Message(number));
  message_back.GoNextMsg();
}

TEST(MessageBackTest, StartsEmptyAndResetRestoresAnEmptyUsableState) {
  MessageBack message_back;
  EXPECT_TRUE(message_back.GetEntries().empty());

  message_back.Reset();
  message_back.Reset();
  EXPECT_TRUE(message_back.GetEntries().empty());

  message_back.AddMessage("after reset");
  ASSERT_EQ(message_back.GetEntries().size(), 1U);
  EXPECT_EQ(message_back.GetEntries()[0].msg, "after reset");

  message_back.Reset();
  EXPECT_TRUE(message_back.GetEntries().empty());
}

TEST(MessageBackTest, SelectionSeparatorIsASeparateImageEntry) {
  MessageBack back;
  back.AddMessage("before");
  back.InsertImage("sys_bk_selline00", 12);
  back.AddMessage("after");
  const auto entries = back.GetEntries();
  ASSERT_EQ(entries.size(), 3u);
  EXPECT_EQ(entries[0].msg, "before");
  EXPECT_TRUE(entries[1].pct_flag);
  EXPECT_EQ(entries[1].image_file, "sys_bk_selline00");
  EXPECT_EQ(entries[1].image_x, 12);
  EXPECT_EQ(entries[2].msg, "after");
}

TEST(MessageBackTest, NewEntriesUseExpectedDefaults) {
  MessageBack message_back;
  message_back.ReadyMsg();

  const auto entries = message_back.GetEntries();
  ASSERT_EQ(entries.size(), 1U);
  ExpectDefaultEntry(entries[0]);
}

TEST(MessageBackTest, MessagesConcatenateAndTrackTheLastDebugFragment) {
  MessageBack message_back;
  message_back.AddMessage("Hello");
  message_back.AddMessage(", ");
  message_back.AddMessage("world");
  message_back.AddMessage("");

  const auto entries = message_back.GetEntries();
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_EQ(entries[0].msg, "Hello, world");
  EXPECT_EQ(entries[0].debug_msg, "world");
  EXPECT_EQ(entries[0].scene_no, -1);
  EXPECT_EQ(entries[0].line_no, -1);
  EXPECT_FALSE(entries[0].pct_flag);
}

TEST(MessageBackTest, EmptyTextAndNamesNeverCreateOrChangeEntries) {
  MessageBack message_back;
  message_back.AddMessage("");
  message_back.AddNamae("");
  EXPECT_TRUE(message_back.GetEntries().empty());

  message_back.AddMessage("First");
  message_back.GoNextMsg();
  message_back.AddMessage("");
  message_back.AddNamae("");

  const auto entries = message_back.GetEntries();
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_EQ(entries[0].msg, "First");
  EXPECT_EQ(entries[0].debug_msg, "First");
}

TEST(MessageBackTest, PreservesMessageContentsExactly) {
  MessageBack message_back;
  const std::string first = "  line one\n";
  const std::string second("line\0two", 8);

  message_back.AddMessage(first);
  message_back.AddMessage(second);

  const auto entries = message_back.GetEntries();
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_EQ(entries[0].msg, first + second);
  EXPECT_EQ(entries[0].debug_msg, second);
}

TEST(MessageBackTest, NamesOverwriteAndCombineWithMessages) {
  MessageBack message_back;
  message_back.AddNamae("Alice");
  message_back.AddNamae("Bob");
  message_back.AddMessage("Hello");

  const auto entries = message_back.GetEntries();
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_EQ(entries[0].original_name, "Bob");
  EXPECT_EQ(entries[0].display_name, "Bob");
  EXPECT_EQ(entries[0].msg, "Hello");
}

TEST(MessageBackTest, VoiceRecordsArePairedAndAcceptAllIntegerValues) {
  MessageBack message_back;
  message_back.AddKoe(10, 20);
  message_back.AddKoe(11, 21);
  message_back.AddKoe(std::numeric_limits<int>::min(),
                      std::numeric_limits<int>::max());

  const auto entries = message_back.GetEntries();
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_EQ(entries[0].koe_no_list,
            (std::vector<int>{10, 11, std::numeric_limits<int>::min()}));
  EXPECT_EQ(entries[0].chara_no_list,
            (std::vector<int>{20, 21, std::numeric_limits<int>::max()}));
}

TEST(MessageBackTest, NameVoiceAndTextShareTheCurrentEntry) {
  MessageBack message_back;
  message_back.AddNamae("Alice");
  message_back.AddKoe(100, 5);
  message_back.AddMessage("Hello");

  const auto entries = message_back.GetEntries();
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_EQ(entries[0].original_name, "Alice");
  EXPECT_EQ(entries[0].display_name, "Alice");
  EXPECT_EQ(entries[0].koe_no_list, (std::vector<int>{100}));
  EXPECT_EQ(entries[0].chara_no_list, (std::vector<int>{5}));
  EXPECT_EQ(entries[0].msg, "Hello");
}

TEST(MessageBackTest, AdvancingCompletedMessagesStartsOneNewEntry) {
  MessageBack message_back;
  message_back.GoNextMsg();
  message_back.AddMessage("First");
  message_back.GoNextMsg();
  message_back.GoNextMsg();
  message_back.AddMessage("Second");

  const auto entries = message_back.GetEntries();
  ASSERT_EQ(entries.size(), 2U);
  EXPECT_EQ(entries[0].msg, "First");
  EXPECT_EQ(entries[1].msg, "Second");
}

TEST(MessageBackTest, AdvancingNameOrVoiceOnlyEntriesIsRejected) {
  MessageBack message_back;
  message_back.AddNamae("Alice");
  message_back.AddKoe(10, 20);
  message_back.GoNextMsg();
  message_back.AddMessage("Hello");
  message_back.GoNextMsg();
  message_back.AddMessage("Second");

  const auto entries = message_back.GetEntries();
  ASSERT_EQ(entries.size(), 2U);
  EXPECT_EQ(entries[0].original_name, "Alice");
  EXPECT_EQ(entries[0].koe_no_list, (std::vector<int>{10}));
  EXPECT_EQ(entries[0].msg, "Hello");
  EXPECT_EQ(entries[1].msg, "Second");
}

TEST(MessageBackTest, ReadyMsgCreatesOneEntryAndRespectsAdvancement) {
  MessageBack message_back;
  message_back.ReadyMsg();
  message_back.ReadyMsg();
  message_back.GoNextMsg();
  message_back.AddNamae("Alice");
  message_back.AddKoe(10, 20);
  message_back.AddMessage("First");
  message_back.GoNextMsg();
  message_back.ReadyMsg();
  message_back.ReadyMsg();

  const auto entries = message_back.GetEntries();
  ASSERT_EQ(entries.size(), 2U);
  EXPECT_EQ(entries[0].original_name, "Alice");
  EXPECT_EQ(entries[0].koe_no_list, (std::vector<int>{10}));
  EXPECT_EQ(entries[0].chara_no_list, (std::vector<int>{20}));
  EXPECT_EQ(entries[0].msg, "First");
  ExpectDefaultEntry(entries[1]);
}

TEST(MessageBackTest, EntriesStaySeparatedAndChronological) {
  MessageBack message_back;
  message_back.AddNamae("Alice");
  message_back.AddKoe(1, 10);
  message_back.AddMessage("A");
  message_back.AddMessage("B");
  message_back.GoNextMsg();
  message_back.AddNamae("Bob");
  message_back.AddMessage("C");
  message_back.GoNextMsg();
  message_back.AddKoe(2, 20);
  message_back.AddMessage("D");

  const auto entries = message_back.GetEntries();
  ASSERT_EQ(entries.size(), 3U);
  EXPECT_EQ(entries[0].original_name, "Alice");
  EXPECT_EQ(entries[0].msg, "AB");
  EXPECT_EQ(entries[0].debug_msg, "B");
  EXPECT_EQ(entries[0].koe_no_list, (std::vector<int>{1}));
  EXPECT_EQ(entries[1].original_name, "Bob");
  EXPECT_EQ(entries[1].msg, "C");
  EXPECT_TRUE(entries[1].koe_no_list.empty());
  EXPECT_EQ(entries[2].msg, "D");
  EXPECT_EQ(entries[2].koe_no_list, (std::vector<int>{2}));
  EXPECT_EQ(entries[2].chara_no_list, (std::vector<int>{20}));
}

TEST(MessageBackTest, CircularBufferRetainsTheMostRecent256Entries) {
  MessageBack message_back;
  for (int number = 1; number <= 300; ++number)
    AddCompletedMessage(message_back, number);

  const auto entries = message_back.GetEntries();
  ASSERT_EQ(entries.size(), 256U);
  for (std::size_t index = 0; index < entries.size(); ++index)
    EXPECT_EQ(entries[index].msg, Message(static_cast<int>(index) + 45));
}

TEST(MessageBackTest, CurrentEntryAndIgnoredInputDoNotEvictHistory) {
  MessageBack message_back;
  for (int number = 1; number < 256; ++number)
    AddCompletedMessage(message_back, number);
  message_back.AddMessage(Message(256));

  const auto before = message_back.GetEntries();
  ASSERT_EQ(before.size(), 256U);

  message_back.AddMessage(" extended");
  message_back.AddKoe(42, 7);
  message_back.GoNextMsg();
  message_back.AddMessage("");
  message_back.AddNamae("");

  const auto entries = message_back.GetEntries();
  ASSERT_EQ(entries.size(), 256U);
  EXPECT_EQ(entries.front().msg, "message 1");
  EXPECT_EQ(entries.back().msg, "message 256 extended");
  EXPECT_EQ(entries.back().koe_no_list, (std::vector<int>{42}));
  EXPECT_EQ(before.front().msg, "message 1");
  EXPECT_EQ(before.back().msg, "message 256");
}

TEST(MessageBackTest, OverflowUpdatesTheNewestEntryAndResetClearsItsState) {
  MessageBack message_back;
  for (int number = 1; number <= 256; ++number)
    AddCompletedMessage(message_back, number);

  message_back.AddNamae("Newest");
  message_back.AddKoe(99, 8);
  message_back.AddMessage("message 257");

  auto entries = message_back.GetEntries();
  ASSERT_EQ(entries.size(), 256U);
  EXPECT_EQ(entries.front().msg, "message 2");
  EXPECT_EQ(entries.back().original_name, "Newest");
  EXPECT_EQ(entries.back().koe_no_list, (std::vector<int>{99}));
  EXPECT_EQ(entries.back().msg, "message 257");
  EXPECT_EQ(entries[entries.size() - 2].msg, "message 256");

  message_back.Reset();
  message_back.AddMessage("after overflow reset");
  entries = message_back.GetEntries();
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_EQ(entries[0].msg, "after overflow reset");
  EXPECT_TRUE(entries[0].original_name.empty());
  EXPECT_TRUE(entries[0].koe_no_list.empty());
  EXPECT_EQ(entries[0].scene_no, -1);
  EXPECT_EQ(entries[0].line_no, -1);
  EXPECT_FALSE(entries[0].pct_flag);
}

TEST(MessageBackTest, GetEntriesReturnsIndependentCopies) {
  MessageBack message_back;
  message_back.AddMessage("First");

  const auto saved_copy = message_back.GetEntries();
  auto modified_copy = saved_copy;
  modified_copy[0].msg = "changed";
  modified_copy.clear();

  message_back.GoNextMsg();
  message_back.AddMessage("Second");

  const auto entries = message_back.GetEntries();
  ASSERT_EQ(entries.size(), 2U);
  EXPECT_EQ(entries[0].msg, "First");
  EXPECT_EQ(entries[1].msg, "Second");
  ASSERT_EQ(saved_copy.size(), 1U);
  EXPECT_EQ(saved_copy[0].msg, "First");
  EXPECT_TRUE(modified_copy.empty());
}
