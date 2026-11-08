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

#include <gtest/gtest.h>

#include "libsiglus/script_call.hpp"
#include "vm/function.hpp"
#include "vm/gc.hpp"
#include "vm/list.hpp"
#include "vm/string.hpp"

namespace libsiglus {
namespace sr = serilang;
namespace {
class ScriptCallTest : public ::testing::Test {};

TEST_F(ScriptCallTest, PrimitiveResultsRetainTheirTypes) {
  // Primitive results survive capture/restore without conversion.
  sr::GarbageCollector gc;
  for (const sr::Value& value :
       {sr::nil, sr::Value(true), sr::Value(17), sr::Value(3.5)}) {
    const auto restored = TransferValue::Capture(value).Restore(gc);
    EXPECT_EQ(restored.Type(), value.Type());
    EXPECT_EQ(restored, value);
  }
}

TEST_F(ScriptCallTest, StringResultOutlivesItsProducingHeap) {
  // Destroy the source heap; the copied string remains restorable elsewhere.
  TransferValue captured;
  {
    sr::GarbageCollector source;
    captured = TransferValue::Capture(source.Allocate<sr::String>("returned"));
  }
  sr::GarbageCollector destination;
  EXPECT_EQ(captured.Restore(destination), std::string("returned"));
}

TEST_F(ScriptCallTest, NestedListsAllocateInTheReceivingHeap) {
  // Nested return values are recreated as distinct list and string objects.
  sr::GarbageCollector source;
  sr::GarbageCollector destination;
  auto* text = source.Allocate<sr::String>("result");
  auto* nested = source.Allocate<sr::List>(std::vector<sr::Value>{text, 7});
  auto* list = source.Allocate<sr::List>(std::vector<sr::Value>{nested});
  auto result = TransferValue::Capture(list).Restore(destination);
  auto* copied = result.Get<sr::List*>();
  ASSERT_NE(copied, list);
  auto* copied_nested = copied->items[0].Get<sr::List*>();
  EXPECT_NE(copied_nested, nested);
  EXPECT_NE(copied_nested->items[0].Get<sr::String*>(), text);
  EXPECT_EQ(copied_nested->items[0], std::string("result"));
  EXPECT_EQ(copied_nested->items[1], sr::Value(7));
}

TEST_F(ScriptCallTest, UnsupportedObjectsAreRejected) {
  // A function cannot be represented in an independent execution heap.
  sr::GarbageCollector source;
  auto* function = source.Allocate<sr::Function>(nullptr);
  EXPECT_THROW(TransferValue::Capture(function), sr::RuntimeError);
}

TEST_F(ScriptCallTest, CyclicListsAreRejected) {
  // A self-referencing result fails rather than recursively copying forever.
  sr::GarbageCollector source;
  auto* list = source.Allocate<sr::List>();
  list->items.emplace_back(list);
  EXPECT_THROW(TransferValue::Capture(list), sr::RuntimeError);
}

TEST_F(ScriptCallTest, RepeatedListReferencesAreCopied) {
  // Reusing a list is valid when no ancestor cycle exists.
  sr::GarbageCollector source;
  sr::GarbageCollector destination;
  auto* inner = source.Allocate<sr::List>(std::vector<sr::Value>{5});
  auto* outer = source.Allocate<sr::List>(std::vector<sr::Value>{inner, inner});
  const auto result = TransferValue::Capture(outer).Restore(destination);
  const auto* copied = result.Get<sr::List*>();
  ASSERT_EQ(copied->items.size(), 2);
  EXPECT_EQ(copied->items[0].Get<sr::List*>()->items[0], sr::Value(5));
  EXPECT_EQ(copied->items[1].Get<sr::List*>()->items[0], sr::Value(5));
}

TEST_F(ScriptCallTest, FarcallArgumentsAreCreatedFromCopiedBanks) {
  // L/K argument lists are receiver-local wrappers over copied primitive data.
  sr::GarbageCollector gc;
  ScriptCall call;
  call.farcall = true;
  call.integers = {7, 11};
  call.strings = {"argument"};
  const auto arguments = MakeScriptArguments(gc, call);
  ASSERT_EQ(arguments.size(), 2);
  EXPECT_EQ(arguments[0].Get<sr::List*>()->items,
            (std::vector<sr::Value>{7, 11}));
  EXPECT_EQ(arguments[1].Get<sr::List*>()->items[0], std::string("argument"));
}

TEST_F(ScriptCallTest, EngineEntryHasNoImplicitArguments) {
  // Ordinary engine entries keep their zero-argument calling convention.
  sr::GarbageCollector gc;
  EXPECT_TRUE(MakeScriptArguments(gc, ScriptCall{}).empty());
}
}  // namespace
}  // namespace libsiglus
