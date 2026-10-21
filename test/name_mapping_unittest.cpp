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

#include "core/name_mapping.hpp"

#include "core/gameexe.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

void AddMapping(Gameexe& gexe,
                std::string_view key,
                const std::string& source,
                const std::string& replacement) {
  gexe.SetAt(key, {GexeStr{source}, GexeStr{replacement}});
}

TEST(NameMappingTest, DefaultMappingReturnsInputUnchanged) {
  const NameMapping mapping;
  EXPECT_EQ(mapping.Map("Name"), "Name");
  EXPECT_EQ(mapping.Map("あ"), "あ");
  EXPECT_EQ(mapping.Map(""), "");
}

TEST(NameMappingTest, ConfigurationWithoutNamaeHasNoMappings) {
  Gameexe gexe;
  AddMapping(gexe, "OTHER.000", "Name", "Replacement");
  const NameMapping mapping(gexe);
  EXPECT_EQ(mapping.Map("Name"), "Name");
  EXPECT_EQ(mapping.Map(""), "");
}

TEST(NameMappingTest, ReplacesOnlyExactCaseSensitiveMatches) {
  Gameexe gexe;
  AddMapping(gexe, "NAMAE.000", "Name", "Replacement");
  const NameMapping mapping(gexe);

  EXPECT_EQ(mapping.Map("Name"), "Replacement");
  for (const std::string name :
       {"Other", "name", "NAME", "Name ", " Name", "Name in dialogue", ""}) {
    SCOPED_TRACE(name);
    EXPECT_EQ(mapping.Map(name), name);
  }
}

TEST(NameMappingTest, DecodesBothStringsAsCp932RegardlessOfNameEncoding) {
  Gameexe gexe;
  gexe.SetIntAt("NAME_ENC", 2);
  AddMapping(gexe, "NAMAE.000", "\x82\xa0", "\x82\xa2");
  const NameMapping mapping(gexe);

  EXPECT_EQ(mapping.Map("あ"), "い");
  EXPECT_EQ(mapping.Map("\x82\xa0"), "\x82\xa0");
}

TEST(NameMappingTest, DoesNotMapReplacementAgain) {
  Gameexe gexe;
  AddMapping(gexe, "NAMAE.000", "First", "Second");
  AddMapping(gexe, "NAMAE.001", "Second", "Third");
  const NameMapping mapping(gexe);

  EXPECT_EQ(mapping.Map("First"), "Second");
  EXPECT_EQ(mapping.Map("Second"), "Third");
}

TEST(NameMappingTest, AcceptsEmptyNamesAndReplacements) {
  Gameexe gexe;
  AddMapping(gexe, "NAMAE.000", "", "Unnamed");
  AddMapping(gexe, "NAMAE.001", "Hidden", "");
  const NameMapping mapping(gexe);

  EXPECT_EQ(mapping.Map(""), "Unnamed");
  EXPECT_EQ(mapping.Map("Hidden"), "");
}

TEST(NameMappingTest, LastValidEntryInGameexeTraversalWins) {
  Gameexe gexe;
  // Gameexe traverses keys in order, independently of insertion order.
  AddMapping(gexe, "NAMAE.200", "Name", "Last");
  AddMapping(gexe, "NAMAE.100", "Name", "First");
  const NameMapping mapping(gexe);

  EXPECT_EQ(mapping.Map("Name"), "Last");
}

TEST(NameMappingTest, ReadsRepeatedConfigurationKeysInOrder) {
  Gameexe gexe;
  ASSERT_TRUE(gexe.ParseLine("#NAMAE.000 = \"Name\", \"First\"", 1));
  ASSERT_TRUE(gexe.ParseLine("#NAMAE.000 = \"Name\", \"Last\"", 2));
  const NameMapping mapping(gexe);

  EXPECT_EQ(mapping.Map("Name"), "Last");
}

TEST(NameMappingTest, SkipsMalformedEntriesAndPreservesValidMappings) {
  Gameexe gexe;
  AddMapping(gexe, "NAMAE.000", "Valid", "Replacement");
  gexe.SetAt("NAMAE.001", {});
  gexe.SetAt("NAMAE.002", {GexeStr{"Missing replacement"}});
  gexe.SetAt("NAMAE.003", {17, GexeStr{"Invalid key"}});
  gexe.SetAt("NAMAE.004", {GexeStr{"Invalid replacement"}, 42});
  gexe.SetAt("NAMAE.005", {GexeStr{"Valid"}, 42});
  AddMapping(gexe, "NAMAE.006", "Later", "Still loaded");

  const NameMapping mapping(gexe);
  EXPECT_EQ(mapping.Map("Valid"), "Replacement");
  EXPECT_EQ(mapping.Map("Later"), "Still loaded");
  EXPECT_EQ(mapping.Map("Missing replacement"), "Missing replacement");
  EXPECT_EQ(mapping.Map("Invalid replacement"), "Invalid replacement");
}

TEST(NameMappingTest, OwnsMappingDataAfterConfigurationChanges) {
  Gameexe gexe;
  AddMapping(gexe, "NAMAE.000", "Name", "Original");
  const NameMapping mapping(gexe);
  AddMapping(gexe, "NAMAE.000", "Name", "Changed");

  EXPECT_EQ(mapping.Map("Name"), "Original");
}
