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

#include "vm/transfer_value.hpp"

#include "vm/function.hpp"
#include "vm/gc.hpp"
#include "vm/list.hpp"
#include "vm/string.hpp"

#include <gtest/gtest.h>

namespace sr = serilang;

TEST(TransferValueTest, PrimitiveResultsRetainTheirTypes) {
  // Primitive results survive capture/restore without conversion.
  sr::GarbageCollector gc;
  for (const sr::Value& value :
       {sr::nil, sr::Value(true), sr::Value(17), sr::Value(3.5)}) {
    const auto restored = sr::TransferValue::Capture(value).Restore(gc);
    EXPECT_EQ(restored.Type(), value.Type());
    EXPECT_EQ(restored, value);
  }
}

TEST(TransferValueTest, StringResultOutlivesItsProducingHeap) {
  // Destroy the source heap; the copied string remains restorable elsewhere.
  sr::TransferValue captured;
  {
    sr::GarbageCollector src;
    captured = sr::TransferValue::Capture(src.Allocate<sr::String>("returned"));
  }
  sr::GarbageCollector destination;
  EXPECT_EQ(captured.Restore(destination), std::string("returned"));
}

TEST(TransferValueTest, NestedListsAllocateInTheReceivingHeap) {
  // Nested return values are recreated as distinct list and string objects.
  sr::GarbageCollector src, dst;
  auto* text = src.Allocate<sr::String>("result");
  auto* nested = src.Allocate<sr::List>(std::vector<sr::Value>{text, 7});
  auto* list = src.Allocate<sr::List>(std::vector<sr::Value>{nested});
  auto result = sr::TransferValue::Capture(list).Restore(dst);
  auto* copied = result.Get<sr::List*>();
  ASSERT_NE(copied, list);
  auto* copied_nested = copied->items[0].Get<sr::List*>();
  EXPECT_NE(copied_nested, nested);
  EXPECT_NE(copied_nested->items[0].Get<sr::String*>(), text);
  EXPECT_EQ(copied_nested->items[0], std::string("result"));
  EXPECT_EQ(copied_nested->items[1], sr::Value(7));
}

TEST(TransferValueTest, UnsupportedObjectsAreRejected) {
  // A function cannot be represented in an independent execution heap.
  sr::GarbageCollector src;
  auto* function = src.Allocate<sr::Function>(nullptr);
  EXPECT_THROW(sr::TransferValue::Capture(function), sr::RuntimeError);
}

TEST(TransferValueTest, CyclicListsAreRejected) {
  // A self-referencing result fails rather than recursively copying forever.
  sr::GarbageCollector src;
  auto* list = src.Allocate<sr::List>();
  list->items.emplace_back(list);
  EXPECT_THROW(sr::TransferValue::Capture(list), sr::RuntimeError);
}

TEST(TransferValueTest, RepeatedListReferencesAreCopied) {
  // Reusing a list is valid when no ancestor cycle exists.
  sr::GarbageCollector src, dst;
  auto* inner = src.Allocate<sr::List>(std::vector<sr::Value>{5});
  auto* outer = src.Allocate<sr::List>(std::vector<sr::Value>{inner, inner});
  const auto result = sr::TransferValue::Capture(outer).Restore(dst);
  const auto* copied = result.Get<sr::List*>();
  ASSERT_EQ(copied->items.size(), 2);
  EXPECT_EQ(copied->items[0].Get<sr::List*>()->items[0], sr::Value(5));
  EXPECT_EQ(copied->items[1].Get<sr::List*>()->items[0], sr::Value(5));
}
