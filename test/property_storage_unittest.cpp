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

#include "core/property_storage.hpp"

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

#include <sstream>
#include <utility>

namespace {
PropertyStorage::Cell MakeCell(PropertyStorage::Value value,
                               int initial_size = 0) {
  return std::make_shared<PropertyStorage::Entry>(std::move(value),
                                                  initial_size);
}

template <class Output = boost::archive::text_oarchive>
std::string Save(const PropertyStorage& storage) {
  std::ostringstream stream(std::ios::binary);
  {
    Output archive(stream);
    archive << storage;
  }
  return stream.str();
}

template <class Input = boost::archive::text_iarchive>
void Load(const std::string& bytes, PropertyStorage& storage) {
  std::istringstream stream(bytes, std::ios::binary);
  Input archive(stream);
  archive >> storage;
}

class PropertyStorageTest : public ::testing::Test {
 protected:
  PropertyStorage storage;

  template <class Output, class Input>
  void CheckRoundTrip() {
    const std::vector<PropertyStorage::Value> values = {
        std::monostate{},
        true,
        47,
        2.5,
        std::string("saved"),
        PropertyStorage::Integers{3, 5, 7, 9},
        PropertyStorage::Strings{"first", "second", "third"},
        PropertyStorage::Integers{},
        PropertyStorage::Strings{}};
    PropertyStorage::Table table;
    for (const auto& value : values)
      table.push_back(MakeCell(value));
    table[5]->initial_size = 3;
    table[6]->initial_size = 2;
    table[7]->initial_size = 3;
    table[8]->initial_size = 2;
    storage.Initialize({{-9, std::move(table)}, {12, {MakeCell(false)}}});

    PropertyStorage restored;
    Load<Input>(Save<Output>(storage), restored);
    for (std::size_t i = 0; i < values.size(); ++i) {
      SCOPED_TRACE(i);
      EXPECT_EQ(restored.Get(-9, i).value, values[i]);
      EXPECT_EQ(restored.Get(-9, i).initial_size,
                storage.Get(-9, i).initial_size);
    }
    EXPECT_EQ(restored.Get(12, 0).value, PropertyStorage::Value(false));
  }

  template <class Output, class Input>
  void CheckSharedCellRoundTrip() {
    auto cell = MakeCell(PropertyStorage::Integers{3, 5}, 4);
    storage.Initialize({{0, {cell}}, {4, {cell}}});
    PropertyStorage restored;
    Load<Input>(Save<Output>(storage), restored);
    EXPECT_EQ(restored.GetCell(0, 0), restored.GetCell(4, 0));
    EXPECT_NE(restored.GetCell(0, 0), cell);
  }
};

TEST_F(PropertyStorageTest, DefaultStorageHasNoTables) {
  // A default storage has no entries; looking up any table must fail.
  EXPECT_THROW(storage.GetCell(-1, 0), std::out_of_range);
  EXPECT_THROW(storage.GetCell(0, 0), std::out_of_range);
}

TEST_F(PropertyStorageTest, InitializationAcceptsArbitraryTableIds) {
  // Install negative and positive table IDs; each resolves its own value.
  storage.Initialize({{-9, {MakeCell(47)}}, {12, {MakeCell(59)}}});
  EXPECT_EQ(storage.Get(-9, 0).value, PropertyStorage::Value(47));
  EXPECT_EQ(storage.Get(12, 0).value, PropertyStorage::Value(59));
}

TEST_F(PropertyStorageTest, InitializationPreservesDeclaredArraySize) {
  // Install a resized array; its declared size remains independent of length.
  storage.Initialize({{0, {MakeCell(PropertyStorage::Integers{3, 5}, 4)}}});
  EXPECT_EQ(storage.Get(0, 0).initial_size, 4);
}

TEST_F(PropertyStorageTest, InitializationReplacesExistingTables) {
  // Replace populated tables; overlapping IDs use new cells and stale IDs go.
  auto original = MakeCell(47);
  auto replacement = MakeCell(59);
  storage.Initialize({{0, {original}}, {1, {MakeCell(71)}}});
  storage.Initialize({{0, {replacement}}});
  EXPECT_EQ(storage.GetCell(0, 0), replacement);
  EXPECT_THROW(storage.Get(1, 0), std::out_of_range);
}

TEST_F(PropertyStorageTest, EmptyInitializationClearsExistingTables) {
  // Replace populated storage with no tables; the previous entry disappears.
  storage.Initialize({{0, {MakeCell(47)}}});
  storage.Initialize({});
  EXPECT_THROW(storage.Get(0, 0), std::out_of_range);
}

TEST_F(PropertyStorageTest, NullCellInitializationPreservesExistingState) {
  // A null replacement cell is rejected without changing the installed cell.
  auto original = MakeCell(47);
  storage.Initialize({{0, {original}}});
  EXPECT_THROW(storage.Initialize({{0, {MakeCell(59), nullptr}}}),
               std::invalid_argument);
  EXPECT_EQ(storage.GetCell(0, 0), original);
  EXPECT_EQ(storage.Get(0, 0).value, PropertyStorage::Value(47));
  EXPECT_THROW(storage.Get(0, 1), std::out_of_range);
}

TEST_F(PropertyStorageTest, NegativeInitialSizePreservesExistingState) {
  // A negative declared size is rejected without replacing installed values.
  auto original = MakeCell(47);
  storage.Initialize({{0, {original}}});
  EXPECT_THROW(
      storage.Initialize({{0, {MakeCell(PropertyStorage::Integers{}, -1)}}}),
      std::invalid_argument);
  EXPECT_EQ(storage.GetCell(0, 0), original);
  EXPECT_EQ(storage.Get(0, 0).value, PropertyStorage::Value(47));
}

TEST_F(PropertyStorageTest, MutableLookupUpdatesTheInstalledCell) {
  // Mutate the returned entry; the supplied backing cell sees the new value.
  auto cell = MakeCell(47);
  storage.Initialize({{0, {cell}}});
  storage.Get(0, 0).value = 59;
  EXPECT_EQ(cell->value, PropertyStorage::Value(59));
}

TEST_F(PropertyStorageTest, ConstLookupReturnsTheInstalledEntry) {
  // Read through a const storage; the reference identifies the supplied entry.
  auto cell = MakeCell(47);
  storage.Initialize({{0, {cell}}});
  EXPECT_EQ(&std::as_const(storage).Get(0, 0), cell.get());
}

TEST_F(PropertyStorageTest, CellLookupRetainsSharedIdentity) {
  // Install a shared cell; lookup returns that exact cell.
  auto cell = MakeCell(47);
  storage.Initialize({{0, {cell}}});
  EXPECT_EQ(storage.GetCell(0, 0), cell);
}

TEST_F(PropertyStorageTest, RetainedCellFollowsValueReplacement) {
  // Retain a cell before changing its value type; the same cell sees the text.
  storage.Initialize({{0, {MakeCell(47)}}});
  auto cell = storage.GetCell(0, 0);
  storage.Get(0, 0).value = std::string("replacement");
  EXPECT_EQ(cell->value, PropertyStorage::Value(std::string("replacement")));
}

TEST_F(PropertyStorageTest, RetainedCellSurvivesStorageDestruction) {
  // Keep a cell beyond its storage's lifetime; its value remains available.
  PropertyStorage::Cell retained;
  {
    PropertyStorage temporary;
    temporary.Initialize({{0, {MakeCell(47)}}});
    retained = temporary.GetCell(0, 0);
  }
  EXPECT_EQ(retained->value, PropertyStorage::Value(47));
}

TEST_F(PropertyStorageTest, MissingTableLookupDoesNotCreateState) {
  // Repeated missing-table lookups fail and leave existing values intact.
  storage.Initialize({{0, {MakeCell(47)}}});
  EXPECT_THROW(storage.GetCell(99, 0), std::out_of_range);
  EXPECT_THROW(storage.Get(99, 0), std::out_of_range);
  EXPECT_EQ(storage.Get(0, 0).value, PropertyStorage::Value(47));
}

TEST_F(PropertyStorageTest, NegativeIndexIsRejected) {
  // A populated table still rejects a negative index without changing its cell.
  storage.Initialize({{0, {MakeCell(47)}}});
  EXPECT_THROW(storage.Get(0, -1), std::out_of_range);
  EXPECT_EQ(storage.Get(0, 0).value, PropertyStorage::Value(47));
}

TEST_F(PropertyStorageTest, IndexAtTableSizeIsRejected) {
  // A one-entry table rejects index one; its length is not grown by lookup.
  storage.Initialize({{0, {MakeCell(47)}}});
  EXPECT_THROW(storage.GetCell(0, 1), std::out_of_range);
  EXPECT_THROW(std::as_const(storage).Get(0, 1), std::out_of_range);
}

TEST_F(PropertyStorageTest, EmptyTableHasNoValidIndex) {
  // Install an empty table; its first index is out of range.
  storage.Initialize({{0, {}}});
  EXPECT_THROW(storage.Get(0, 0), std::out_of_range);
}

TEST_F(PropertyStorageTest, TextSerializationRoundTrip) {
  // Round-trip populated tables as text; all values and declared sizes survive.
  CheckRoundTrip<boost::archive::text_oarchive,
                 boost::archive::text_iarchive>();
}

TEST_F(PropertyStorageTest, BinarySerializationRoundTrip) {
  // Round-trip populated tables as binary; values and declared sizes survive.
  CheckRoundTrip<boost::archive::binary_oarchive,
                 boost::archive::binary_iarchive>();
}

TEST_F(PropertyStorageTest, SerializationPreservesSharedCells) {
  // Archive two aliases; both restored tables share one newly allocated cell.
  CheckSharedCellRoundTrip<boost::archive::text_oarchive,
                           boost::archive::text_iarchive>();
  CheckSharedCellRoundTrip<boost::archive::binary_oarchive,
                           boost::archive::binary_iarchive>();
}

TEST_F(PropertyStorageTest, DeserializationReplacesExistingCells) {
  // Load over an occupied table; it acquires the saved value in a new cell.
  auto original = MakeCell(47);
  storage.Initialize({{0, {original}}});
  PropertyStorage source;
  source.Initialize({{0, {MakeCell(59)}}});
  Load(Save(source), storage);
  EXPECT_EQ(storage.Get(0, 0).value, PropertyStorage::Value(59));
  EXPECT_NE(storage.GetCell(0, 0), original);
}

TEST_F(PropertyStorageTest, EmptyDeserializationRemovesExistingTables) {
  // Load empty storage over populated state; stale tables disappear.
  storage.Initialize({{-1, {MakeCell(47)}}, {0, {MakeCell(59)}}});
  Load(Save(PropertyStorage{}), storage);
  EXPECT_THROW(storage.Get(-1, 0), std::out_of_range);
  EXPECT_THROW(storage.Get(0, 0), std::out_of_range);
}

TEST_F(PropertyStorageTest, FailedDeserializationPreservesExistingState) {
  // Truncate saved data; failed loading retains the destination cell and value.
  storage.Initialize({{0, {MakeCell(47), MakeCell(std::string("saved"))}}});
  const auto bytes = Save<boost::archive::binary_oarchive>(storage);
  storage.Get(0, 0).value = 151;
  const auto original = storage.GetCell(0, 0);
  EXPECT_THROW(Load<boost::archive::binary_iarchive>(
                   bytes.substr(0, bytes.size() - 1), storage),
               boost::archive::archive_exception);
  EXPECT_EQ(storage.GetCell(0, 0), original);
  EXPECT_EQ(storage.Get(0, 0).value, PropertyStorage::Value(151));
}

TEST_F(PropertyStorageTest, DeserializationRejectsNegativeInitialSize) {
  // Save an invalid declared size; loading it leaves valid destination state.
  auto original = MakeCell(47);
  storage.Initialize({{0, {original}}});
  PropertyStorage source;
  source.Initialize({{0, {MakeCell(PropertyStorage::Integers{}, 3)}}});
  source.Get(0, 0).initial_size = -1;
  EXPECT_THROW(Load(Save(source), storage), std::runtime_error);
  EXPECT_EQ(storage.GetCell(0, 0), original);
  EXPECT_EQ(storage.Get(0, 0).value, PropertyStorage::Value(47));
}

}  // namespace
