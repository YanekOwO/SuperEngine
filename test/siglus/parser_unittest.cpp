// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
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

#include "libsiglus/parser.hpp"
#include "utilities/string_utilities.hpp"

#include <cstdint>
#include <memory>
#include <sstream>

namespace siglus_test {
using namespace libsiglus;
using namespace libsiglus::lex;

class SiglusParserTest : public ::testing::Test {
 protected:
  std::string rawdata;
  std::vector<std::string> strs;
  std::vector<int> labels;
  std::vector<int> zlabels;
  std::vector<libsiglus::Property> scn_props;
  std::vector<libsiglus::Property> g_props;
  std::vector<libsiglus::Command> scn_cmd;
  std::vector<libsiglus::Command> g_cmd;
  std::unique_ptr<Parser> parser;

  SiglusParserTest() { ResetParser(); }

  void ResetParser(int scene_id = 0, std::string_view debug_title = "test") {
    parser = std::make_unique<Parser>(rawdata, strs, labels, zlabels, scn_props,
                                      g_props, scn_cmd, g_cmd, scene_id,
                                      debug_title);
  }

  inline void Parse(auto&&... params) {
    (parser->Add(Lexeme(std::forward<decltype(params)>(params))), ...);
  }

  void AppendRawByte(ByteCode code) {
    rawdata.push_back(static_cast<char>(code));
  }

  void AppendRawInt(std::int32_t value) {
    rawdata.append(reinterpret_cast<const char*>(&value), sizeof(value));
  }

  void AppendRawPush(Type type, std::int32_t value) {
    AppendRawByte(ByteCode::Push);
    AppendRawInt(static_cast<std::int32_t>(type));
    AppendRawInt(value);
  }

  void AppendRawDeclare(Type type, std::int32_t prop_id) {
    AppendRawByte(ByteCode::Declare);
    AppendRawInt(static_cast<std::int32_t>(type));
    AppendRawInt(prop_id);
  }

  void AppendRawLine(std::int32_t line) {
    AppendRawByte(ByteCode::Newline);
    AppendRawInt(line);
  }

  struct TokenArray {
    std::vector<token::Token_t> tokens;
    friend std::ostream& operator<<(std::ostream& os, const TokenArray& t) {
      for (const auto& it : t.tokens)
        os << ToString(it) << '\n';
      return os;
    }
    bool operator==(const TokenArray& other) const {
      return tokens == other.tokens;
    }
    bool operator==(char const* str) const {
      std::ostringstream oss;
      oss << *this;
      std::string_view sv(str);
      return trim_cp(oss.str()) == trim_sv(sv);
    }
  };
  inline TokenArray Tokens() const {
    TokenArray result;
    result.tokens.reserve(parser->ParsedTokens().size());
    for (const auto& parsed : parser->ParsedTokens())
      result.tokens.emplace_back(parsed.token);
    return result;
  }
};

TEST_F(SiglusParserTest, Gosub) {
  EXPECT_NO_THROW(
      Parse(Marker{}, Push{Type::Int, 83}, Push{Type::Int, 0},
            Push{Type::Int, -1}, Push{Type::Int, 0},
            Gosub{.return_type_ = Type::Int, .label_ = 5, .argt_ = {}},
            Assign{.ltype_ = Type::Int, .rtype_ = Type::Int, .v1_ = 1}));
}

TEST_F(SiglusParserTest, ParseAllLabelsAndZlabels) {
  rawdata = std::string(1, static_cast<char>(ByteCode::End));
  labels = {0};
  zlabels = {0};
  ResetParser();

  auto parsed = parser->ParseAll();
  ASSERT_TRUE(parsed.has_value()) << parsed.error();
  const auto& tokens = parsed.value().first;

  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].token, token::Token_t(token::Label{0}));
  EXPECT_EQ(tokens[1].token, token::Token_t(token::Eof{}));
}

TEST_F(SiglusParserTest, ParseAllMalformedInputReturnsUnexpected) {
  rawdata = std::string(1, static_cast<char>(ByteCode::Push));
  ResetParser();

  auto parsed = parser->ParseAll();
  ASSERT_FALSE(parsed.has_value());
}

TEST_F(SiglusParserTest, Operate1) {
  Parse(Push{Type::Int, 5}, Operate1{Type::Int, OperatorCode::Minus});

  const auto& tokens = parser->ParsedTokens();
  ASSERT_EQ(tokens.size(), 1u);
  const auto& tok = std::get<token::Operate1>(tokens[0].token);
  EXPECT_EQ(tok.op, OperatorCode::Minus);
  EXPECT_EQ(AsInt(tok.rhs), 5);
  ASSERT_TRUE(tok.val.has_value());
  EXPECT_EQ(AsInt(*tok.val), -5);
  EXPECT_EQ(tok.dst.id, 0);
}

TEST_F(SiglusParserTest, Operate2) {
  Parse(Push{Type::Int, 10}, Push{Type::Int, 20},
        Operate2{Type::Int, Type::Int, OperatorCode::Plus});

  const auto& tokens = parser->ParsedTokens();
  ASSERT_EQ(tokens.size(), 1u);
  const auto& tok = std::get<token::Operate2>(tokens[0].token);
  EXPECT_EQ(tok.op, OperatorCode::Plus);
  EXPECT_EQ(AsInt(tok.lhs), 10);
  EXPECT_EQ(AsInt(tok.rhs), 20);
  ASSERT_TRUE(tok.val.has_value());
  EXPECT_EQ(AsInt(*tok.val), 30);
  EXPECT_EQ(tok.dst.id, 0);
}

TEST_F(SiglusParserTest, ConditionalGoto) {
  Parse(Push{Type::Int, 1}, Goto{lex::Goto::Condition::True, 42});

  const auto& tokens = parser->ParsedTokens();
  ASSERT_EQ(tokens.size(), 1u);
  const auto& tok = std::get<token::GotoIf>(tokens[0].token);
  EXPECT_TRUE(tok.cond);
  EXPECT_EQ(tok.label, 42);
  EXPECT_EQ(AsInt(tok.src), 1);
}

TEST_F(SiglusParserTest, AssignElement) {
  Parse(Marker{}, Push{Type::Int, 25}, Push{Type::Int, 7},
        Assign{Type::IntRef, Type::Int, 1});

  const auto& tokens = parser->ParsedTokens();
  ASSERT_EQ(tokens.size(), 1u);
  ASSERT_TRUE(std::holds_alternative<token::Assign>(tokens[0].token));
  const auto& tok = std::get<token::Assign>(tokens[0].token);
  EXPECT_EQ(tok.dst_elmcode, elm::ElementCode{25});
  EXPECT_EQ(AsInt(tok.src), 7);
}

TEST_F(SiglusParserTest, ElementAlias) {
  strs = {"bg47"};
  g_cmd = {
      libsiglus::Command{.scene_id = 78, .offset = 913, .name = "$$usr_cmd"}};
  ResetParser();

  Parse(Marker{}, Push{Type::Int, 0x7e000000}, Marker{}, Push{Type::Int, 37},
        Push{Type::Int, 2}, Push{Type::Int, -1}, Push{Type::Int, 0},
        Push{Type::String, 0},
        lex::Command(elm::Signature{
            .overload_id = 0,
            .arglist = elm::ArgumentList({Type::Object, Type::String}),
            .argtags = {},
            .rettype = Type::Int}));

  EXPECT_EQ(Tokens(), R"(
alias.object t0 = stage.back.object[int:0]              ;cmd<int:37,int:2,int:-1,int:0>
int t1 = @78.913:$$usr_cmd(t0,str:bg47)                 ;cmd<int:2113929216>
)");
}

TEST_F(SiglusParserTest, StageObjectCreate) {
  strs = {"bg47"};
  ResetParser();

  Parse(Marker{}, Push{Type::Int, 37}, Push{Type::Int, 2}, Push{Type::Int, -1},
        Push{Type::Int, 0}, Push{Type::Int, 38}, Push{Type::String, 0},
        Push{Type::Int, 1},
        lex::Command(elm::Signature{
            .overload_id = 0,
            .arglist = elm::ArgumentList({Type::String, Type::Int}),
            .argtags = {},
            .rettype = Type::None}));

  EXPECT_EQ(Tokens(), R"(
null_t t0 = stage.back.object[int:0].create(str:bg47,int:1) ;cmd<int:37,int:2,int:-1,int:0,int:38>
)");
}

TEST_F(SiglusParserTest, ExpressionListPreservesMixedTypeSourceOrder) {
  strs = {"middle", "tail"};
  g_cmd = {libsiglus::Command{.name = "$$list_test"}};
  ResetParser();

  Parse(Marker{}, Push{Type::Int, 0x7e000000}, Push{Type::Int, 10},
        Push{Type::Int, 11}, Push{Type::String, 0}, Push{Type::Int, 22},
        Push{Type::String, 1},
        lex::Command(elm::Signature{
            .overload_id = 0,
            .arglist = elm::ArgumentList(
                {Type::Int,
                 elm::ArgumentList({Type::Int, Type::String, Type::Int}),
                 Type::String}),
            .argtags = {},
            .rettype = Type::None}));

  const auto& tokens = parser->ParsedTokens();
  ASSERT_EQ(tokens.size(), 1u);
  const auto& command = std::get<token::Command>(tokens[0].token);
  const auto& args =
      std::get<elm::Usrcmd>(command.chain.root.var).arguments.arg;
  const std::vector<Value> expected = {
      Integer{10}, List{{Integer{11}, String{"middle"}, Integer{22}}},
      String{"tail"}};
  EXPECT_EQ(args, expected);
}

TEST_F(SiglusParserTest, NestedExpressionListsPreserveSourceOrder) {
  strs = {"nested"};
  g_cmd = {libsiglus::Command{.name = "$$list_test"}};
  ResetParser();

  Parse(Marker{}, Push{Type::Int, 0x7e000000}, Push{Type::Int, 1},
        Push{Type::Int, 2}, Push{Type::String, 0}, Push{Type::Int, 3},
        Push{Type::Int, 4}, Push{Type::Int, 5}, Push{Type::Int, 6},
        lex::Command(elm::Signature{
            .overload_id = 0,
            .arglist = {.args = {elm::ArgumentList(
                            {Type::Int,
                             elm::ArgumentList(
                                 {Type::Int, Type::String,
                                  elm::ArgumentList({Type::Int, Type::Int}),
                                  Type::Int}),
                             Type::Int})}},
            .argtags = {},
            .rettype = Type::None}));

  const auto& tokens = parser->ParsedTokens();
  ASSERT_EQ(tokens.size(), 1u);
  const auto& command = std::get<token::Command>(tokens[0].token);
  const auto& args =
      std::get<elm::Usrcmd>(command.chain.root.var).arguments.arg;
  const std::vector<Value> expected = {
      List{{Integer{1},
            List{{Integer{2}, String{"nested"}, List{{Integer{3}, Integer{4}}},
                  Integer{5}}},
            Integer{6}}}};
  EXPECT_EQ(args, expected);
}

TEST_F(SiglusParserTest, EmptyExpressionListDoesNotConsumeAdjacentArguments) {
  g_cmd = {libsiglus::Command{.name = "$$list_test"}};
  ResetParser();

  Parse(Marker{}, Push{Type::Int, 0x7e000000}, Push{Type::Int, 10},
        Push{Type::Int, 20},
        lex::Command(elm::Signature{
            .overload_id = 0,
            .arglist =
                elm::ArgumentList({Type::Int, elm::ArgumentList{}, Type::Int}),
            .argtags = {},
            .rettype = Type::None}));

  const auto& tokens = parser->ParsedTokens();
  ASSERT_EQ(tokens.size(), 1u);
  const auto& command = std::get<token::Command>(tokens[0].token);
  const auto& args =
      std::get<elm::Usrcmd>(command.chain.root.var).arguments.arg;
  const std::vector<Value> expected = {Integer{10}, List{}, Integer{20}};
  EXPECT_EQ(args, expected);
}

TEST_F(SiglusParserTest, SingletonExpressionListPreservesItsValue) {
  strs = {"single"};
  g_cmd = {libsiglus::Command{.name = "$$list_test"}};
  ResetParser();

  Parse(Marker{}, Push{Type::Int, 0x7e000000}, Push{Type::String, 0},
        lex::Command(elm::Signature{
            .overload_id = 0,
            .arglist = {.args = {elm::ArgumentList({Type::String})}},
            .argtags = {},
            .rettype = Type::None}));

  const auto& tokens = parser->ParsedTokens();
  ASSERT_EQ(tokens.size(), 1u);
  const auto& command = std::get<token::Command>(tokens[0].token);
  const auto& args =
      std::get<elm::Usrcmd>(command.chain.root.var).arguments.arg;
  const std::vector<Value> expected = {List{{String{"single"}}}};
  EXPECT_EQ(args, expected);
}

TEST_F(SiglusParserTest, SubroutineTemporariesDoNotOverwriteArguments) {
  strs = {"bg47"};
  ResetParser();

  Parse(Declare{Type::String, 1}, Arg{}, Marker{}, Push{Type::Int, 37},
        Push{Type::Int, 2}, Push{Type::Int, -1}, Push{Type::Int, 0},
        Push{Type::Int, 38}, Push{Type::String, 0}, Push{Type::Int, 1},
        lex::Command(elm::Signature{
            .overload_id = 0,
            .arglist = elm::ArgumentList({Type::String, Type::Int}),
            .argtags = {},
            .rettype = Type::None}));

  EXPECT_EQ(Tokens(), R"(
====== SUBROUTINE  @-1 ======
  arg_0: str
null_t t0 = stage.back.object[int:0].create(str:bg47,int:1) ;cmd<int:37,int:2,int:-1,int:0,int:38>
)");
}

TEST_F(SiglusParserTest, ListDeclareConsumesStackSizeBeforeLineCheck) {
  for (const Type list_type : {Type::IntList, Type::StrList}) {
    rawdata.clear();
    AppendRawByte(ByteCode::Arg);
    AppendRawLine(1);
    AppendRawPush(Type::Int, 10);
    AppendRawDeclare(list_type, 6);
    AppendRawLine(2);
    AppendRawByte(ByteCode::End);
    ResetParser();

    auto parsed = parser->ParseAll();
    ASSERT_TRUE(parsed.has_value()) << parsed.error();
    const auto& warnings = parsed.value().second;
    EXPECT_THAT(warnings, ::testing::IsEmpty());
  }
}

TEST_F(SiglusParserTest, NonListDeclareDoesNotConsumeStackValue) {
  AppendRawPush(Type::Int, 10);
  AppendRawDeclare(Type::String, 6);
  AppendRawLine(1);
  AppendRawByte(ByteCode::End);
  ResetParser();

  auto parsed = parser->ParseAll();
  ASSERT_TRUE(parsed.has_value()) << parsed.error();
  const auto& warnings = parsed.value().second;
  EXPECT_THAT(
      warnings,
      ::testing::Contains(::testing::HasSubstr("expected stack to be empty")));
}

TEST_F(SiglusParserTest, ReassignLinenoAndIgnoreLineLexeme) {
  Parse(Line{123});  // ignored
  Parse(Push{Type::Int, 5}, Operate1{Type::Int, OperatorCode::Minus});  // first
  Parse(Line{234});  // ignored
  Parse(Push{Type::Int, 10}, Push{Type::Int, 20},
        Operate2{Type::Int, Type::Int, OperatorCode::Plus});  // second
  Parse(Line{456});                                           // ignored

  auto tokens = parser->ParsedTokens();
  EXPECT_EQ(tokens.size(), 2);
  EXPECT_EQ(tokens[0].line, 1);
  EXPECT_EQ(tokens[1].line, 2);
}

}  // namespace siglus_test
