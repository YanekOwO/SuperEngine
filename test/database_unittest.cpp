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

#include "core/database.hpp"
#include "core/gameexe.hpp"
#include "database_test_support.hpp"

using ::testing::HasSubstr;

class DatabaseTest : public ::testing::Test {
 protected:
  DatabaseFixture fixture;
  Database table = Database::Decode(fixture.File());
};

TEST_F(DatabaseTest, NumericReadUsesFirstItemHeader) {
  // Duplicate item 42 has different values; resolve its first stored row.
  EXPECT_EQ(table.GetNum(42, 3), -17);
}

TEST_F(DatabaseTest, StringReadUsesFirstColumnHeader) {
  // Column ID 8 appears twice; the first header selects its string cell.
  EXPECT_EQ(table.GetStr(42, 8), "AbCde");
}

TEST_F(DatabaseTest, UnicodeStringsUseByteOffsets) {
  // A nonzero offset follows another string; decode Japanese and Latin text.
  EXPECT_EQ(table.GetStr(7, 8), "日本Ä");
}

TEST_F(DatabaseTest, MultibyteJapaneseStringsDecodeToUtf8) {
  // A CP932 pool starts at byte offset 1; preserve the Japanese cell text.
  fixture.rows = {10};
  fixture.columns = {{2, 'S'}};
  fixture.cells = {1};
  fixture.strings = std::string("\0\x93\xfa\x96\x7b\0", 6);
  EXPECT_EQ(Database::Decode(fixture.File(0)).GetStr(10, 2), "日本");
}

TEST_F(DatabaseTest, NonzeroSelectorsUseUnicode) {
  // Unicode is selected by any nonzero selector, including an unknown version.
  EXPECT_EQ(Database::Decode(fixture.File(37)).GetStr(7, 8), "日本Ä");
}
TEST_F(DatabaseTest, MaskRepeatsAcrossMapRowsAndTileBoundaries) {
  // A long numeric table crosses five map rows; every word retains its
  // position.
  fixture.rows.clear();
  fixture.columns = {{13, 'V'}};
  fixture.cells.clear();
  for (int i = 0; i < 100; ++i) {
    fixture.rows.push_back(1000 - i);
    fixture.cells.push_back(i * 12345);
  }
  const auto decoded = Database::Decode(fixture.File());
  for (int i = 0; i < 100; ++i)
    EXPECT_EQ(decoded.GetNum(1000 - i, 13), i * 12345);
}

TEST_F(DatabaseTest, OverlappingBackReferencesDecode) {
  // Repeated encrypted string bytes admit overlapping copies; recover all text.
  fixture.rows = {1};
  fixture.columns = {{1, 'S'}};
  fixture.cells = {0};
  const std::u16string repeated(400, u'a');
  fixture.strings = Utf16Bytes(repeated) + std::string(2, '\0');
  EXPECT_EQ(Database::Decode(fixture.File(1, true)).GetStr(1, 1),
            std::string(400, 'a'));
}

TEST_F(DatabaseTest, NumericReadsRequireNumericColumns) {
  // The first header for ID 8 is a string; return a numeric default.
  EXPECT_EQ(table.GetNum(42, 8), 0);
}

TEST_F(DatabaseTest, StringReadsRequireStringColumns) {
  // Reading numeric column 3 as text produces an actual empty string.
  EXPECT_EQ(table.GetStr(42, 3), "");
}

TEST_F(DatabaseTest, MissingNumericItemReturnsZero) {
  // A valid numeric column has no item 123; return zero.
  EXPECT_EQ(table.GetNum(123, 3), 0);
}

TEST_F(DatabaseTest, MissingStringItemReturnsEmpty) {
  // A valid string column has no item 123; return empty text.
  EXPECT_EQ(table.GetStr(123, 8), "");
}

TEST_F(DatabaseTest, MissingNumericColumnReturnsZero) {
  // A present item has no column 123; return zero.
  EXPECT_EQ(table.GetNum(42, 123), 0);
}

TEST_F(DatabaseTest, MissingStringColumnReturnsEmpty) {
  // A present item has no column 123; return empty text.
  EXPECT_EQ(table.GetStr(42, 123), "");
}

TEST_F(DatabaseTest, CheckItemUsesStoredIdentifiers) {
  // Sparse item IDs are unrelated to row positions; report only stored IDs.
  EXPECT_EQ(table.CheckItem(7), 1);
  EXPECT_EQ(table.CheckItem(1), 0);
}

TEST_F(DatabaseTest, CheckColumnReportsNumericType) {
  // Numeric header 3 reports the numeric type code.
  EXPECT_EQ(table.CheckColumn(3), 1);
}

TEST_F(DatabaseTest, CheckColumnReportsFirstStringType) {
  // Duplicate ID 8 resolves to its first header and reports string type.
  EXPECT_EQ(table.CheckColumn(8), 2);
}

TEST_F(DatabaseTest, CheckColumnRejectsUnknownType) {
  // An unknown type remains a valid header but has no supported type code.
  EXPECT_EQ(table.CheckColumn(9), 0);
}

TEST_F(DatabaseTest, CheckColumnRejectsAbsentIdentifier) {
  // An absent column has no type code.
  EXPECT_EQ(table.CheckColumn(123), 0);
}

TEST_F(DatabaseTest, NumericSearchReturnsFirstMatchingItemIdentifier) {
  // Three rows contain 5; the first match is item 7 rather than row 1.
  EXPECT_EQ(table.FindNum(3, 5), 7);
}

TEST_F(DatabaseTest, NumericSearchRejectsWrongType) {
  // Column 8's first header is a string even though its duplicate is numeric.
  EXPECT_EQ(table.FindNum(8, 800), -1);
}

TEST_F(DatabaseTest, NumericSearchReturnsMissingSentinel) {
  // No numeric cell contains this value; return the search sentinel.
  EXPECT_EQ(table.FindNum(3, 123), -1);
}

TEST_F(DatabaseTest, StringSearchFoldsAscii) {
  // Mixed ASCII case compares equal and the first matching row wins.
  EXPECT_EQ(table.FindStr(8, "aBcDE"), 42);
}

TEST_F(DatabaseTest, StringSearchPreservesNonAsciiCase) {
  // The stored uppercase umlaut does not match its lowercase counterpart.
  EXPECT_EQ(table.FindStr(8, "日本ä"), -1);
}

TEST_F(DatabaseTest, ExactStringSearchDistinguishesAsciiCase) {
  // Exact search compares the entire decoded string without case folding.
  EXPECT_EQ(table.FindStrReal(8, "abcde"), -1);
}

TEST_F(DatabaseTest, ExactStringSearchReturnsFirstMatchingIdentifier) {
  // Two stored strings match exactly; return the first item ID.
  EXPECT_EQ(table.FindStrReal(8, "AbCde"), 42);
}

TEST_F(DatabaseTest, StringSearchMatchesEmptyCells) {
  // A valid empty pool string belongs to item 99 and can be searched.
  EXPECT_EQ(table.FindStr(8, ""), 99);
}

TEST_F(DatabaseTest, ExactSearchMatchesEmptyCells) {
  // Empty strings also compare equal under exact search.
  EXPECT_EQ(table.FindStrReal(8, ""), 99);
}

TEST_F(DatabaseTest, StringSearchRequiresWholeString) {
  // A substring of the stored text is not an entire-cell match.
  EXPECT_EQ(table.FindStr(8, "AbC"), -1);
}

TEST_F(DatabaseTest, StringSearchDoesNotTrim) {
  // An extra trailing space prevents an otherwise equal match.
  EXPECT_EQ(table.FindStr(8, "AbCde "), -1);
}

TEST_F(DatabaseTest, StringSearchRejectsNumericColumn) {
  // Numeric cells are not converted to text for searching.
  EXPECT_EQ(table.FindStr(3, "5"), -1);
}

TEST_F(DatabaseTest, ExactSearchRejectsMissingColumn) {
  // An absent string column returns the search sentinel.
  EXPECT_EQ(table.FindStrReal(123, ""), -1);
}

TEST_F(DatabaseTest, EmptyDatabaseReturnsTypedDefaults) {
  // An unnamed slot has no headers; all operations return their defined
  // defaults.
  const Database empty;
  EXPECT_EQ(empty.GetNum(0, 0), 0);
  EXPECT_EQ(empty.GetStr(0, 0), "");
  EXPECT_EQ(empty.CheckItem(0), 0);
  EXPECT_EQ(empty.CheckColumn(0), 0);
  EXPECT_EQ(empty.FindNum(0, 0), -1);
  EXPECT_EQ(empty.FindStr(0, ""), -1);
  EXPECT_EQ(empty.FindStrReal(0, ""), -1);
}

TEST_F(DatabaseTest, TruncatedFileIsRejected) {
  // Truncating a valid compressed payload invalidates its declared extent.
  auto file = fixture.File();
  file.pop_back();
  EXPECT_THROW(Database::Decode(file), std::exception);
}

TEST_F(DatabaseTest, MissingSelectorIsRejected) {
  // A three-byte file cannot hold the leading selector.
  EXPECT_THROW(Database::Decode("abc"), std::exception);
}

TEST_F(DatabaseTest, InvalidPayloadSizeIsRejected) {
  // The logical extent cannot exceed the expanded buffer.
  auto bytes = fixture.Plain();
  PutWord(bytes, 0, bytes.size() + 1);
  EXPECT_THROW(Database::Decode(EncodeDatabase(bytes)), std::exception);
}

TEST_F(DatabaseTest, InvalidRowExtentIsRejected) {
  // A row table extending past the payload fails before reading its IDs.
  auto bytes = fixture.Plain();
  PutWord(bytes, 12, bytes.size() - 1);
  EXPECT_THROW(Database::Decode(EncodeDatabase(bytes)), std::exception);
}

TEST_F(DatabaseTest, InvalidColumnExtentIsRejected) {
  // An enormous column count cannot be used to address or allocate cells.
  auto bytes = fixture.Plain();
  PutWord(bytes, 8, 0xffffffff);
  EXPECT_THROW(Database::Decode(EncodeDatabase(bytes)), std::exception);
}

TEST_F(DatabaseTest, InvalidCellExtentIsRejected) {
  // The cell table cannot begin outside the logical payload.
  auto bytes = fixture.Plain();
  PutWord(bytes, 20, 0xffffffff);
  EXPECT_THROW(Database::Decode(EncodeDatabase(bytes)), std::exception);
}

TEST_F(DatabaseTest, InvalidStringPoolIsRejected) {
  // A string pool offset outside the expanded payload is corrupt data.
  auto bytes = fixture.Plain();
  PutWord(bytes, 24, 0xffffffff);
  EXPECT_THROW(Database::Decode(EncodeDatabase(bytes)), std::exception);
}

TEST_F(DatabaseTest, InvalidStringOffsetIsRejected) {
  // A string cell points beyond the pool; this is not a missing-ID default.
  fixture.cells[0] = 0xffffffff;
  EXPECT_THROW(Database::Decode(fixture.File()), std::exception);
}

TEST_F(DatabaseTest, UnterminatedUnicodeStringIsRejected) {
  // Referenced UTF-16 text lacks a terminator within data_size, despite
  // padding.
  fixture.strings.pop_back();
  fixture.strings.pop_back();
  EXPECT_THROW(Database::Decode(fixture.File()), std::exception);
}

TEST_F(DatabaseTest, UnterminatedMultibyteStringIsRejected) {
  // Multibyte text must end inside the logical string pool.
  fixture.rows = {1};
  fixture.columns = {{1, 'S'}};
  fixture.cells = {0};
  fixture.strings = "abc";
  EXPECT_THROW(Database::Decode(fixture.File(0)), std::exception);
}

TEST_F(DatabaseTest, InvalidUnicodeIsRejected) {
  // An unpaired surrogate is not valid UTF-16 text.
  fixture.strings = std::string("\0\xd8\0\0", 4);
  fixture.rows = {1};
  fixture.columns = {{1, 'S'}};
  fixture.cells = {0};
  EXPECT_THROW(Database::Decode(fixture.File()), std::exception);
}

TEST_F(DatabaseTest, InvalidBackReferenceIsRejected) {
  // The first LZSS token cannot refer to bytes before the expanded output.
  std::string compressed;
  AddWord(compressed, 11);
  AddWord(compressed, 2);
  compressed += std::string("\0\x10\0", 3);
  for (size_t pos = 0; pos + 4 <= compressed.size(); pos += 4)
    for (size_t j = 0; j < 4; ++j)
      compressed[pos + j] ^= static_cast<char>(0x89F4622Du >> (8 * j));
  std::string file;
  AddWord(file, 1);
  EXPECT_THROW(Database::Decode(file + compressed), std::exception);
}

TEST_F(DatabaseTest, TruncatedMultibyteCharacterIsRejected) {
  // A CP932 leading byte before the string terminator is corrupt text.
  fixture.rows = {1};
  fixture.columns = {{1, 'S'}};
  fixture.cells = {0};
  fixture.strings = std::string("\x93\0", 2);
  EXPECT_THROW(Database::Decode(fixture.File(0)), std::exception);
}

TEST_F(DatabaseTest, InvalidMultibyteTrailIsRejected) {
  // An invalid CP932 trailing byte is rejected before conversion.
  fixture.rows = {1};
  fixture.columns = {{1, 'S'}};
  fixture.cells = {0};
  fixture.strings = std::string("\x93\x20\0", 3);
  EXPECT_THROW(Database::Decode(fixture.File(0)), std::exception);
}

TEST_F(DatabaseTest, DecodedEmptyTableIsValid) {
  // A valid file can have no rows or columns and still provide lookup defaults.
  fixture.rows.clear();
  fixture.columns.clear();
  fixture.cells.clear();
  EXPECT_EQ(Database::Decode(fixture.File()).FindStr(0, ""), -1);
}

TEST_F(DatabaseTest, SupplementaryUnicodeCharactersArePreserved) {
  // A surrogate pair in UTF-16LE becomes one supplementary Unicode character.
  fixture.rows = {1};
  fixture.columns = {{1, 'S'}};
  fixture.cells = {0};
  fixture.strings = Utf16Bytes(u"🌟") + std::string(2, '\0');
  EXPECT_EQ(Database::Decode(fixture.File()).GetStr(1, 1), "🌟");
}

class SiglusDatabaseConfigTest : public ::testing::Test {
 protected:
  DatabaseFiles files;
  Gameexe config;
  DatabaseFixture fixture;
  auto Load() { return DatabaseList::Load(config, files.root); }
};

TEST_F(SiglusDatabaseConfigTest, AbsentCountDefaultsToZero) {
  // With no DATABASE settings, expose a valid empty list.
  EXPECT_EQ(Load()->size(), 0);
}

TEST_F(SiglusDatabaseConfigTest, MaximumCountIsAccepted) {
  // All 256 permitted slots can be unnamed without accessing asset files.
  config.SetIntAt("DATABASE.CNT", 256);
  EXPECT_EQ(Load()->size(), 256);
}

TEST_F(SiglusDatabaseConfigTest, NegativeCountIsRejected) {
  // Negative list sizes are configuration errors.
  config.SetIntAt("DATABASE.CNT", -1);
  EXPECT_THROW(Load(), std::exception);
}

TEST_F(SiglusDatabaseConfigTest, ExcessiveCountIsRejected) {
  // A count above the legacy maximum is rejected before allocation.
  config.SetIntAt("DATABASE.CNT", 257);
  EXPECT_THROW(Load(), std::exception);
}

TEST_F(SiglusDatabaseConfigTest, NonIntegerCountIsRejected) {
  // A malformed existing count must not silently use the missing-setting
  // default.
  config.SetStringAt("DATABASE.CNT", "three");
  EXPECT_THROW(Load(), std::exception);
}

TEST_F(SiglusDatabaseConfigTest, SparseSlotsRetainConfiguredPositions) {
  // Only slot 1 has a file; unnamed slots still count and remain empty.
  config.SetIntAt("DATABASE.CNT", 12);
  config.SetStringAt("DATABASE.001", "sample");
  files.Write("DaT/SAMPLE.DBS", fixture.File());
  auto list = Load();
  EXPECT_EQ(list->size(), 12);
  EXPECT_EQ(list->At(1)->GetNum(42, 3), -17);
  EXPECT_EQ(list->At(0)->CheckItem(42), 0);
}

TEST_F(SiglusDatabaseConfigTest, SlotOutsideCountIsRejected) {
  // A configured index equal to the count cannot create another slot.
  config.SetIntAt("DATABASE.CNT", 1);
  config.SetStringAt("DATABASE.001", "");
  EXPECT_THROW(Load(), std::exception);
}

TEST_F(SiglusDatabaseConfigTest, NegativeSlotIsRejected) {
  // A negative configured slot remains invalid even if unnamed.
  config.SetIntAt("DATABASE.CNT", 1);
  config.SetStringAt("DATABASE.-1", "");
  EXPECT_THROW(Load(), std::exception);
}

TEST_F(SiglusDatabaseConfigTest, MissingNamedAssetIdentifiesDatabase) {
  // A missing named asset reports the configured slot and name.
  config.SetIntAt("DATABASE.CNT", 2);
  config.SetStringAt("DATABASE.001", "missing");
  try {
    Load();
    FAIL() << "expected a missing asset error";
  } catch (const std::exception& e) {
    EXPECT_THAT(e.what(), HasSubstr("database[1] 'missing'"));
  }
}

TEST_F(SiglusDatabaseConfigTest, CorruptNamedAssetIdentifiesDatabase) {
  // A malformed file reports its configured identity and decoder failure.
  config.SetIntAt("DATABASE.CNT", 1);
  config.SetStringAt("DATABASE.000", "bad");
  files.Write("dat/bad.dbs", "bad");
  try {
    Load();
    FAIL() << "expected a decoder error";
  } catch (const std::exception& e) {
    EXPECT_THAT(e.what(), HasSubstr("database[0] 'bad': truncated"));
  }
}

TEST_F(SiglusDatabaseConfigTest, AppendLookupUsesFirstMatchInConfiguredOrder) {
  // Two append entries contain the asset; configuration order takes precedence.
  config.SetIntAt("DATABASE.CNT", 1);
  config.SetStringAt("DATABASE.000", "sample");
  files.Write("Select.ini", "z\tNewest\na\tOlder\n");
  files.Write("z/dat/sample.dbs", fixture.File());
  fixture.cells[1] = 23;
  files.Write("a/dat/sample.dbs", fixture.File());
  EXPECT_EQ(Load()->At(0)->GetNum(42, 3), -17);
}

TEST_F(SiglusDatabaseConfigTest, AppendLookupFallsForwardToLaterEntry) {
  // An asset absent from the first append is found in the next entry.
  config.SetIntAt("DATABASE.CNT", 1);
  config.SetStringAt("DATABASE.000", "sample");
  files.Write("Select.ini", "z\tNewest\na\tOlder\n");
  files.Write("a/dat/sample.dbs", fixture.File());
  EXPECT_EQ(Load()->At(0)->GetNum(42, 3), -17);
}

TEST_F(SiglusDatabaseConfigTest, AppendLookupBeginsAtCurrentEntry) {
  // A current selection skips an earlier asset even if it has the same name.
  config.SetIntAt("DATABASE.CNT", 1);
  config.SetStringAt("DATABASE.000", "sample");
  files.Write("Select.ini", "z\tNewest\na\tOlder\n");
  files.Write("z/dat/sample.dbs", fixture.File());
  fixture.cells[1] = 23;
  files.Write("a/dat/sample.dbs", fixture.File());
  EXPECT_EQ(DatabaseList::Load(config, files.root, "a")->At(0)->GetNum(42, 3),
            23);
}

TEST_F(SiglusDatabaseConfigTest, NegativeRuntimeIndexIsChecked) {
  // Runtime indexing rejects negative values instead of indexing a vector.
  config.SetIntAt("DATABASE.CNT", 1);
  EXPECT_THROW(Load()->At(-1), std::out_of_range);
}

TEST_F(SiglusDatabaseConfigTest, RuntimeIndexAtSizeIsChecked) {
  // Indexing past the configured list never grows it.
  config.SetIntAt("DATABASE.CNT", 1);
  EXPECT_THROW(Load()->At(1), std::out_of_range);
}
