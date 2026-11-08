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

#include "libsiglus/bindings/memory.hpp"
#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "vm/object.hpp"
#include "vm/string.hpp"

#include <gtest/gtest.h>

namespace {
namespace sr = serilang;
using namespace libsiglus;

class SiglusMemoryTest : public ::testing::Test {
 protected:
  SiglusRuntime runtime;
  void SetUp() override {
    runtime.archive = std::make_shared<Archive>(
        std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
    runtime.binding_names = {"0_memory"};
    runtime.GameplayMachine().Initialize();
  }
  void Run(std::string code) {
    binding::Execute(*runtime.GameplayMachine().vm, std::move(code));
  }
  sr::Value Global(const char* name) {
    return runtime.GameplayMachine().vm->globals_->at(name);
  }
};

TEST_F(SiglusMemoryTest, IntegerStorageIsAllocatedAtConstruction) {
  // Construct without accessing elements; the owned cell already has its size.
  Run("items = make_intlist(3);");
  const auto* handle = Global("items")
                           .Get<sr::NativeInstance*>()
                           ->GetForeign<binding::IntListHandle>();
  EXPECT_EQ(std::get<PropertyStorage::Integers>(handle->cell->value),
            (std::vector<int>{0, 0, 0}));
}

TEST_F(SiglusMemoryTest, StringStorageIsAllocatedAtConstruction) {
  // Construct without reading the list; empty strings are allocated eagerly.
  Run("items = make_strlist(2);");
  const auto* handle = Global("items")
                           .Get<sr::NativeInstance*>()
                           ->GetForeign<binding::StrListHandle>();
  EXPECT_EQ(std::get<PropertyStorage::Strings>(handle->cell->value),
            (std::vector<std::string>{"", ""}));
}

TEST_F(SiglusMemoryTest, IntegerInitRestoresOriginalSizeAndValues) {
  // Resize and write an owned list; init restores its original zeroed shape.
  Run("items = make_intlist(2); items.resize(4); items[0] = 7; "
      "items.init(); size = items.size(); value = items[0];");
  EXPECT_EQ(Global("size"), sr::Value(2));
  EXPECT_EQ(Global("value"), sr::Value(0));
}

TEST_F(SiglusMemoryTest, StringInitRestoresOriginalSizeAndValues) {
  // Resize and write an owned string list; init restores empty elements.
  Run("items = make_strlist(2); items.resize(4); items[0] = \"changed\"; "
      "items.init(); size = items.size(); value = items[0];");
  EXPECT_EQ(Global("size"), sr::Value(2));
  EXPECT_EQ(Global("value"), "");
}

TEST_F(SiglusMemoryTest, IntegerFillIncludesTheEnd) {
  // Fill one integer index; inclusive endpoints write exactly that element.
  Run("items = make_intlist(3); items.fill(1, 1, 9); "
      "left = items[0]; value = items[1]; right = items[2];");
  EXPECT_EQ(Global("left"), sr::Value(0));
  EXPECT_EQ(Global("value"), sr::Value(9));
  EXPECT_EQ(Global("right"), sr::Value(0));
}

TEST_F(SiglusMemoryTest, StringFillExcludesTheEnd) {
  // Fill a string range; its end remains unchanged under existing semantics.
  Run("items = make_strlist(3); items.fill(0, 2, \"filled\"); "
      "value = items[1]; end = items[2];");
  EXPECT_EQ(Global("value"), "filled");
  EXPECT_EQ(Global("end"), "");
}

TEST_F(SiglusMemoryTest, SetRejectsOutOfRangeWithoutPartialWrites) {
  // A multi-item write exceeds the list; rejection leaves its contents intact.
  Run("items = make_intlist(2); items[1] = 7; caught = 0; "
      "try { items.Set(1, 11, 13); } catch (error) { caught = 1; } "
      "value = items[1]; size = items.size();");
  EXPECT_EQ(Global("caught"), sr::Value(1));
  EXPECT_EQ(Global("value"), sr::Value(7));
  EXPECT_EQ(Global("size"), sr::Value(2));
}

TEST_F(SiglusMemoryTest, SetRejectsInvalidValueWithoutPartialWrites) {
  // Conversion fails after a valid item; no elements are written.
  Run("items = make_intlist(2); caught = 0; "
      "try { items.Set(0, 11, \"invalid\"); } catch (error) { caught = 1; } "
      "value = items[0];");
  EXPECT_EQ(Global("caught"), sr::Value(1));
  EXPECT_EQ(Global("value"), sr::Value(0));
}

TEST_F(SiglusMemoryTest, IndexedAccessStillGrowsStorage) {
  // Index beyond the eager allocation; proxy access grows and zero-fills it.
  Run("items = make_intlist(1); items[3] = 17; "
      "size = items.size(); gap = items[2]; value = items[3];");
  EXPECT_EQ(Global("size"), sr::Value(4));
  EXPECT_EQ(Global("gap"), sr::Value(0));
  EXPECT_EQ(Global("value"), sr::Value(17));
}

TEST_F(SiglusMemoryTest, NegativeSizeIsRejected) {
  // Construct with a negative size; allocation fails as a catchable error.
  Run("caught = 0; try { make_intlist(-1); } catch (error) { caught = 1; }");
  EXPECT_EQ(Global("caught"), sr::Value(1));
}

TEST_F(SiglusMemoryTest, NegativeIndexIsRejected) {
  // A negative index must fail without growing or modifying the list.
  Run("items = make_strlist(1); caught = 0; "
      "try { items[-1] = \"bad\"; } catch (error) { caught = 1; } "
      "size = items.size();");
  EXPECT_EQ(Global("caught"), sr::Value(1));
  EXPECT_EQ(Global("size"), sr::Value(1));
}

TEST_F(SiglusMemoryTest, BitViewKeepsOwnedCellAliveAcrossCollection) {
  // Drop the list wrapper and collect it; the view still owns its backing cell.
  Run("items = make_intlist(1); bits = items.b4; items = nil;");
  runtime.GameplayMachine().vm->CollectGarbage();
  Run("bits[0] = 7; value = bits[0];");
  EXPECT_EQ(Global("value"), sr::Value(7));
}

TEST_F(SiglusMemoryTest, BankBitViewUsesTheExecutingFiber) {
  // Retain a stack-bank view across separate calls; each fiber uses its own L.
  Run("bits = L.b1; bits[0] = 1;");
  runtime.GameplayMachine().vm->CollectGarbage();
  Run("before = bits[0]; bits[1] = 1; value = L[0];");
  EXPECT_EQ(Global("before"), sr::Value(0));
  EXPECT_EQ(Global("value"), sr::Value(2));
}

}  // namespace
