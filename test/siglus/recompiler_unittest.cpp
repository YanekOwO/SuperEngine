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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libsiglus/recompiler.hpp"

#include <utility>
#include <variant>
#include <vector>

namespace siglus_test {
using namespace libsiglus;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

namespace {

Parser::ParsedToken Tok(token::Token_t token) {
  return Parser::ParsedToken{.line = 0, .token = std::move(token)};
}

Variable Tmp(int id, Type type = Type::Int) {
  return Variable{.type = type, .id = id};
}

Value TmpVal(int id, Type type = Type::Int) { return Tmp(id, type); }

elm::AccessChain NoReadChain() {
  return elm::AccessChain{
      .root = elm::Root(Type::None, std::monostate{}),
      .nodes = {},
  };
}

Parser::ParsedToken WriteTemp(int id) {
  return Tok(token::Command{
      .elmcode = elm::ElementCode{},
      .chain = NoReadChain(),
      .dst = Tmp(id),
  });
}

Parser::ParsedToken Dup(int src, int dst) {
  return Tok(token::Duplicate{
      .src = TmpVal(src),
      .dst = Tmp(dst),
  });
}

Parser::ParsedToken ReadCond(int id) {
  return Tok(token::GotoIf{
      .label = 0,
      .cond = true,
      .src = TmpVal(id),
  });
}

Parser::ParsedToken Sub(std::vector<Type> args) {
  return Tok(token::Subroutine{
      .name = "sub",
      .source_entry = 0,
      .args = std::move(args),
  });
}

Parser::ParsedToken Local(int id) {
  return Tok(token::LocalVar{
      .id = id,
      .type = Type::Int,
      .size = 0,
  });
}

elm::AccessChain IndexedProperty(int idx_tmp) {
  return elm::AccessChain{
      .root = elm::Root(Type::IntListRef,
                        elm::Usrprop{.scene = 0, .idx = 0, .name = "prop"}),
      .nodes =
          {
              elm::Node(Type::IntRef, elm::Subscript{.idx = TmpVal(idx_tmp)}),
          },
  };
}

}  // namespace

TEST(ResolveFastSlotsTest, ConstantOnlyTokensProduceEmptyPlan) {
  const std::vector<Parser::ParsedToken> tokens = {
      Tok(token::Label{.id = 0}),
      Tok(token::Textout{.kidoku = 0, .str = String("hello")}),
      Tok(token::Return{.ret_vals = {Integer(0)}}),
      Tok(token::Eof{}),
  };

  auto plan = resolve_fast_slots(tokens);

  ASSERT_TRUE(plan.has_value()) << plan.error();
  EXPECT_THAT(plan->slots, IsEmpty());
  EXPECT_EQ(plan->total_fast_locals, 1);
}

TEST(ResolveFastSlotsTest, NoTempWritesStillReservesSubroutineFixedSlots) {
  const std::vector<Parser::ParsedToken> tokens = {
      Sub({Type::Int, Type::String}),
      Local(0),
      Local(1),
      Tok(token::Return{.ret_vals = {Integer(0)}}),
  };

  auto plan = resolve_fast_slots(tokens);

  ASSERT_TRUE(plan.has_value()) << plan.error();
  EXPECT_THAT(plan->slots, IsEmpty());
  EXPECT_EQ(plan->total_fast_locals, 5);
}

TEST(ResolveFastSlotsTest, ReadWithoutAnyWriteReturnsUninitializedError) {
  const std::vector<Parser::ParsedToken> tokens = {
      ReadCond(0),
  };

  auto plan = resolve_fast_slots(tokens);

  ASSERT_FALSE(plan.has_value());
  EXPECT_THAT(plan.error(), HasSubstr("0: t0 is uninitialized"));
}

TEST(ResolveFastSlotsTest, WriteOnlyTempIsAllocatedBeforeRelease) {
  const std::vector<Parser::ParsedToken> tokens = {
      Tok(token::Label{.id = 0}),
      WriteTemp(0),
      WriteTemp(1),
  };

  auto plan = resolve_fast_slots(tokens);

  ASSERT_TRUE(plan.has_value()) << plan.error();
  EXPECT_THAT(plan->slots, ElementsAre(1, 1));
  EXPECT_EQ(plan->total_fast_locals, 2);
}

TEST(ResolveFastSlotsTest, ReusesSlotsOnlyAfterLastRead) {
  const std::vector<Parser::ParsedToken> tokens = {
      WriteTemp(0),
      Dup(0, 1),
      ReadCond(1),
      WriteTemp(2),
  };

  auto plan = resolve_fast_slots(tokens);

  ASSERT_TRUE(plan.has_value()) << plan.error();
  EXPECT_THAT(plan->slots, ElementsAre(1, 2, 1));
  EXPECT_EQ(plan->total_fast_locals, 3);
}

TEST(ResolveFastSlotsTest, SubroutineResetsTempStateAndProtectsArguments) {
  const std::vector<Parser::ParsedToken> tokens = {
      WriteTemp(0),
      Sub({Type::Int}),
      WriteTemp(1),
  };

  auto plan = resolve_fast_slots(tokens);

  ASSERT_TRUE(plan.has_value()) << plan.error();
  EXPECT_THAT(plan->slots, ElementsAre(1, 2));
  EXPECT_EQ(plan->total_fast_locals, 3);
}

TEST(ResolveFastSlotsTest, SubroutineArgsAndLocalsReserveLowSlots) {
  const std::vector<Parser::ParsedToken> tokens = {
      Sub({Type::Int, Type::String}),
      Local(0),
      Local(1),
      WriteTemp(0),
  };

  auto plan = resolve_fast_slots(tokens);

  ASSERT_TRUE(plan.has_value()) << plan.error();
  EXPECT_THAT(plan->slots, ElementsAre(5));
  EXPECT_EQ(plan->total_fast_locals, 6);
}

TEST(ResolveFastSlotsTest, ReadAcrossSubroutineBoundaryIsUninitialized) {
  const std::vector<Parser::ParsedToken> tokens = {
      WriteTemp(0),
      Sub({}),
      ReadCond(0),
  };

  auto plan = resolve_fast_slots(tokens);

  ASSERT_FALSE(plan.has_value());
  EXPECT_THAT(plan.error(), HasSubstr("2: t0 is uninitialized"));
}

TEST(ResolveFastSlotsTest, AssignmentTargetReadsSubscriptIndexAndSource) {
  const std::vector<Parser::ParsedToken> tokens = {
      WriteTemp(0),
      WriteTemp(1),
      Tok(token::Assign{
          .dst_elmcode = elm::ElementCode{},
          .dst = IndexedProperty(0),
          .src = TmpVal(1),
      }),
      WriteTemp(2),
  };

  auto plan = resolve_fast_slots(tokens);

  ASSERT_TRUE(plan.has_value()) << plan.error();
  EXPECT_THAT(plan->slots, ElementsAre(1, 2, 1));
  EXPECT_EQ(plan->total_fast_locals, 3);
}

TEST(RecompilerTest, ExposesUserCommandMetadataAfterCompilation) {
  auto gc = std::make_shared<serilang::GarbageCollector>();
  Recompiler compiler(gc);
  const std::vector<Parser::ParsedToken> tokens = {
      Tok(token::Subroutine{
          .name = "$frame_action",
          .source_entry = 42,
          .args = {Type::FrameAction, Type::Object, Type::Int, Type::String}}),
      Tok(token::Return{}),
      Tok(token::Eof{}),
  };

  compiler.Compile(tokens);

  ASSERT_TRUE(compiler.Ok());
  ASSERT_EQ(compiler.GetUserCommands().size(), 1);
  auto expected = UserCommandMetadata{
      .name = "$frame_action",
      .source_entry = 42,
      .bytecode_entry = 5,
      .args = {Type::FrameAction, Type::Object, Type::Int, Type::String}};
  EXPECT_EQ(compiler.GetUserCommands().front(), expected);
}

}  // namespace siglus_test
