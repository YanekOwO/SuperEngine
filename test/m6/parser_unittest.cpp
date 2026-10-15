// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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
//
// -----------------------------------------------------------------------

#include <gtest/gtest.h>

#include "m6/ast.hpp"
#include "m6/parser.hpp"
#include "m6/source_buffer.hpp"
#include "m6/tokenizer.hpp"
#include "machine/op.hpp"
#include "utilities/string_utilities.hpp"

#include <string>
#include <vector>

using namespace m6;

namespace m6test {

// helpers
namespace {
template <typename T>
inline static auto format_errors(T&& errors) {
  return Join("; ", std::views::all(std::forward<T>(errors)) |
                        std::views::transform([](const Error& e) {
                          std::string result = e.msg;
                          if (e.loc)
                            result += e.loc->GetDebugString();
                          return result;
                        }));
}

template <typename... Ts>
static std::vector<m6::Token> TokenArray(Ts&&... args) {
  std::vector<m6::Token> result;
  result.reserve(sizeof...(args));
  (result.emplace_back(std::forward<Ts>(args)), ...);
  return result;
}

template <typename T>
struct ParserResultBase {
  std::shared_ptr<SourceBuffer> src = nullptr;
  std::shared_ptr<T> ast = nullptr;
  std::string errors;

  std::string DumpAST() const {
    if (ast == nullptr)
      return "NULL";
    else
      return ast->DumpAST();
  }

  bool operator==(std::string_view expected) const {
    if (!errors.empty())
      return false;
    std::string str = trim_cp(DumpAST());
    expected = trim_sv(expected);
    return str == expected;
  }

  friend std::ostream& operator<<(std::ostream& os,
                                  ParserResultBase<T> const& result) {
    if (!result.errors.empty())
      os << "\nerrors:\n" << result.errors;
    os << "\nast:\n" << result.DumpAST();
    return os;
  }
};

}  // namespace

class ExprParserTest : public ::testing::Test {
 protected:
  using ParserResult = ParserResultBase<ExprAST>;

  static ParserResult parse(std::vector<Token> tokens) {
    Parser parser(tokens);
    std::shared_ptr<ExprAST> ast = parser.ParseExpression();

    ParserResult r;
    r.ast = ast;
    if (!parser.Ok())
      r.errors = format_errors(parser.GetErrors());
    return r;
  }

  static ParserResult parse(char const* s) {
    auto sb = SourceBuffer::Create(std::string(s), "<test>");
    std::vector<m6::Token> tokens;
    m6::Tokenizer tokenizer(tokens);
    tokenizer.Parse(sb);
    if (tokenizer.Ok()) {
      auto r = parse(tokens);
      r.src = sb;
      return r;
    } else {
      return ParserResult{.src = sb,
                          .ast = nullptr,
                          .errors = format_errors(tokenizer.GetErrors())};
    }
  }
};

TEST_F(ExprParserTest, BasicArithmetic) {
  {
    auto result =
        parse(TokenArray(tok::Int(1), tok::Operator(Op::Add), tok::Int(2)));
    EXPECT_EQ(result, R"(
Binaryop +
   ├─IntLiteral 1
   └─IntLiteral 2
)");
  }
  {
    auto result =
        parse(TokenArray(tok::Int(3), tok::Operator(Op::Sub), tok::Int(4)));
    EXPECT_EQ(result, R"(
Binaryop -
   ├─IntLiteral 3
   └─IntLiteral 4
)");
  }
  {
    auto result =
        parse(TokenArray(tok::Int(5), tok::Operator(Op::Mul), tok::Int(6)));
    EXPECT_EQ(result, R"(
Binaryop *
   ├─IntLiteral 5
   └─IntLiteral 6
)");
  }
  {
    auto result =
        parse(TokenArray(tok::Int(7), tok::Operator(Op::Div), tok::Int(8)));
    EXPECT_EQ(result, R"(
Binaryop /
   ├─IntLiteral 7
   └─IntLiteral 8
)");
  }
  {
    auto result =
        parse(TokenArray(tok::Int(9), tok::Operator(Op::Mod), tok::Int(10)));
    EXPECT_EQ(result, R"(
Binaryop %
   ├─IntLiteral 9
   └─IntLiteral 10
)");
  }
  {
    auto result = parse("true && false");
    EXPECT_EQ(result, R"(
Binaryop &&
   ├─TrueLiteral
   └─FalseLiteral
)");
  }
  {
    auto result = parse("true || false");
    EXPECT_EQ(result, R"(
Binaryop ||
   ├─TrueLiteral
   └─FalseLiteral
)");
  }
}

TEST_F(ExprParserTest, Precedence) {
  {
    auto result =
        parse(TokenArray(tok::Int(5), tok::Operator(Op::Mul), tok::Int(6),
                         tok::Operator(Op::Add), tok::Int(7)));
    EXPECT_EQ(result, R"(
Binaryop +
   ├─Binaryop *
   │  ├─IntLiteral 5
   │  └─IntLiteral 6
   └─IntLiteral 7
)");
  }

  {
    auto result =
        parse(TokenArray(tok::Int(5), tok::Operator(Op::Add), tok::Int(6),
                         tok::Operator(Op::Div), tok::Int(7)));
    EXPECT_EQ(result, R"(
Binaryop +
   ├─IntLiteral 5
   └─Binaryop /
      ├─IntLiteral 6
      └─IntLiteral 7
)");
  }

  {
    auto result = parse("2**3**4**5");
    EXPECT_EQ(result, R"(
Binaryop **
   ├─IntLiteral 2
   └─Binaryop **
      ├─IntLiteral 3
      └─Binaryop **
         ├─IntLiteral 4
         └─IntLiteral 5
)");
  }
}

TEST_F(ExprParserTest, Parenthesis) {
  {
    auto result = parse(TokenArray(
        tok::ParenthesisL(), tok::Int(5), tok::Operator(Op::Add), tok::Int(6),
        tok::ParenthesisR(), tok::Operator(Op::Div), tok::Int(7)));
    EXPECT_EQ(result, R"(
Binaryop /
   ├─Parenthesis
   │  └─Binaryop +
   │     ├─IntLiteral 5
   │     └─IntLiteral 6
   └─IntLiteral 7
)");
  }
}

TEST_F(ExprParserTest, Identifier) {
  {
    auto result = parse(TokenArray(
        tok::ID("v1"), tok::Operator(Op::Add), tok::ID("v2"),
        tok::Operator(Op::Div), tok::ID("v3"), tok::SquareL(), tok::ID("v4"),
        tok::Operator(Op::Add), tok::ID("v5"), tok::SquareR()));
    EXPECT_EQ(result, R"(
Binaryop +
   ├─ID v1
   └─Binaryop /
      ├─ID v2
      └─Subscript
         ├─ID v3
         └─Binaryop +
            ├─ID v4
            └─ID v5
)");
  }
}

TEST_F(ExprParserTest, Comparisons) {
  {
    auto result = parse(TokenArray(
        tok::ID("v1"), tok::Operator(Op::Equal), tok::ID("v2"),
        tok::Operator(Op::NotEqual), tok::ID("v3"), tok::Operator(Op::Greater),
        tok::ID("v4"), tok::Operator(Op::Less), tok::ID("v5"),
        tok::Operator(Op::LessEqual), tok::Int(12),
        tok::Operator(Op::GreaterEqual), tok::Int(13)));
    EXPECT_EQ(result, R"(
Binaryop !=
   ├─Binaryop ==
   │  ├─ID v1
   │  └─ID v2
   └─Binaryop >=
      ├─Binaryop <=
      │  ├─Binaryop <
      │  │  ├─Binaryop >
      │  │  │  ├─ID v3
      │  │  │  └─ID v4
      │  │  └─ID v5
      │  └─IntLiteral 12
      └─IntLiteral 13
)");
  }
}

TEST_F(ExprParserTest, Shifts) {
  {
    auto result = parse(TokenArray(
        tok::ID("v1"), tok::Operator(Op::ShiftLeft), tok::ID("v2"),
        tok::Operator(Op::Less), tok::ID("v3"), tok::Operator(Op::ShiftRight),
        tok::ID("v4"), tok::Operator(Op::Add), tok::ID("v5"),
        tok::Operator(Op::ShiftLeft), tok::Int(12), tok::Operator(Op::Less),
        tok::Int(13)));
    EXPECT_EQ(result, R"(
Binaryop <
   ├─Binaryop <
   │  ├─Binaryop <<
   │  │  ├─ID v1
   │  │  └─ID v2
   │  └─Binaryop <<
   │     ├─Binaryop >>
   │     │  ├─ID v3
   │     │  └─Binaryop +
   │     │     ├─ID v4
   │     │     └─ID v5
   │     └─IntLiteral 12
   └─IntLiteral 13
)");
  }
}

TEST_F(ExprParserTest, Logical) {
  {
    auto result = parse(TokenArray(tok::ID("v1"), tok::Operator(Op::LogicalOr),
                                   tok::ID("v2"), tok::Operator(Op::LogicalAnd),
                                   tok::ID("v3"), tok::Operator(Op::ShiftRight),
                                   tok::ID("v4"), tok::Operator(Op::LogicalOr),
                                   tok::ID("v5"), tok::Operator(Op::LogicalAnd),
                                   tok::Int(12)));
    EXPECT_EQ(result, R"(
Binaryop ||
   ├─Binaryop ||
   │  ├─ID v1
   │  └─Binaryop &&
   │     ├─ID v2
   │     └─Binaryop >>
   │        ├─ID v3
   │        └─ID v4
   └─Binaryop &&
      ├─ID v5
      └─IntLiteral 12
)");
  }
}

TEST_F(ExprParserTest, BitwiseOperators) {
  {
    auto result = parse(
        TokenArray(tok::ID("a"), tok::Operator(Op::BitAnd), tok::ID("b")));
    EXPECT_EQ(result, R"(
Binaryop &
   ├─ID a
   └─ID b
)");
  }
  {
    auto result =
        parse(TokenArray(tok::ID("a"), tok::Operator(Op::BitOr), tok::ID("b")));
    EXPECT_EQ(result, R"(
Binaryop |
   ├─ID a
   └─ID b
)");
  }
  {
    auto result = parse(
        TokenArray(tok::ID("a"), tok::Operator(Op::BitXor), tok::ID("b")));
    EXPECT_EQ(result, R"(
Binaryop ^
   ├─ID a
   └─ID b
)");
  }
  {
    auto result =
        parse(TokenArray(tok::ID("a"), tok::Operator(Op::BitAnd), tok::ID("b"),
                         tok::Operator(Op::BitOr), tok::ID("c"),
                         tok::Operator(Op::BitXor), tok::ID("d")));
    EXPECT_EQ(result, R"(
Binaryop |
   ├─Binaryop &
   │  ├─ID a
   │  └─ID b
   └─Binaryop ^
      ├─ID c
      └─ID d
)");
  }
}

TEST_F(ExprParserTest, UnaryOperators) {
  {
    auto result = parse(TokenArray(tok::Operator(Op::Sub), tok::ID("a")));
    EXPECT_EQ(result, R"(
Unaryop -
   └─ID a
)");
  }
  {
    auto result = parse(TokenArray(tok::Operator(Op::Add), tok::ID("a")));
    EXPECT_EQ(result, R"(
Unaryop +
   └─ID a
)");
  }
  {
    auto result = parse(TokenArray(tok::Operator(Op::Tilde), tok::ID("a")));
    EXPECT_EQ(result, R"(
Unaryop ~
   └─ID a
)");
  }
  {
    auto result = parse(TokenArray(tok::Operator(Op::Sub),
                                   tok::Operator(Op::Tilde), tok::ID("a")));
    EXPECT_EQ(result, R"(
Unaryop -
   └─Unaryop ~
      └─ID a
)");
  }
  {
    auto result = parse(TokenArray(tok::Operator(Op::Sub), tok::ParenthesisL(),
                                   tok::ID("a"), tok::Operator(Op::Add),
                                   tok::ID("b"), tok::ParenthesisR()));
    EXPECT_EQ(result, R"(
Unaryop -
   └─Parenthesis
      └─Binaryop +
         ├─ID a
         └─ID b
)");
  }
}

TEST_F(ExprParserTest, MixedPrecedence) {
  // a + b * c
  {
    auto result =
        parse(TokenArray(tok::ID("a"), tok::Operator(Op::Add), tok::ID("b"),
                         tok::Operator(Op::Mul), tok::ID("c")));
    EXPECT_EQ(result, R"(
Binaryop +
   ├─ID a
   └─Binaryop *
      ├─ID b
      └─ID c
)");

    // a & b | c ^ d
  }
  {
    auto result =
        parse(TokenArray(tok::ID("a"), tok::Operator(Op::BitAnd), tok::ID("b"),
                         tok::Operator(Op::BitOr), tok::ID("c"),
                         tok::Operator(Op::BitXor), tok::ID("d")));
    EXPECT_EQ(result, R"(
Binaryop |
   ├─Binaryop &
   │  ├─ID a
   │  └─ID b
   └─Binaryop ^
      ├─ID c
      └─ID d
)");

    // -a + b * ~c
  }
  {
    auto result = parse(TokenArray(tok::Operator(Op::Sub), tok::ID("a"),
                                   tok::Operator(Op::Add), tok::ID("b"),
                                   tok::Operator(Op::Mul),
                                   tok::Operator(Op::Tilde), tok::ID("c")));
    EXPECT_EQ(result, R"(
Binaryop +
   ├─Unaryop -
   │  └─ID a
   └─Binaryop *
      ├─ID b
      └─Unaryop ~
         └─ID c
)");

    // (a + b) * (c - d) / ~e
  }
  {
    auto result = parse(TokenArray(
        tok::ParenthesisL(), tok::ID("a"), tok::Operator(Op::Add), tok::ID("b"),
        tok::ParenthesisR(), tok::Operator(Op::Mul), tok::ParenthesisL(),
        tok::ID("c"), tok::Operator(Op::Sub), tok::ID("d"), tok::ParenthesisR(),
        tok::Operator(Op::Div), tok::Operator(Op::Tilde), tok::ID("e")));
    EXPECT_EQ(result, R"(
Binaryop /
   ├─Binaryop *
   │  ├─Parenthesis
   │  │  └─Binaryop +
   │  │     ├─ID a
   │  │     └─ID b
   │  └─Parenthesis
   │     └─Binaryop -
   │        ├─ID c
   │        └─ID d
   └─Unaryop ~
      └─ID e
)");

    // a << b + c & d
  }
  {
    auto result =
        parse(TokenArray(tok::ID("a"), tok::Operator(Op::ShiftLeft),
                         tok::ID("b"), tok::Operator(Op::Add), tok::ID("c"),
                         tok::Operator(Op::BitAnd), tok::ID("d")));
    EXPECT_EQ(result, R"(
Binaryop &
   ├─Binaryop <<
   │  ├─ID a
   │  └─Binaryop +
   │     ├─ID b
   │     └─ID c
   └─ID d
)");

    // ~a | b && c ^ d
  }
  {
    auto result = parse(TokenArray(tok::Operator(Op::Tilde), tok::ID("a"),
                                   tok::Operator(Op::BitOr), tok::ID("b"),
                                   tok::Operator(Op::LogicalAnd), tok::ID("c"),
                                   tok::Operator(Op::BitXor), tok::ID("d")));
    EXPECT_EQ(result, R"(
Binaryop &&
   ├─Binaryop |
   │  ├─Unaryop ~
   │  │  └─ID a
   │  └─ID b
   └─Binaryop ^
      ├─ID c
      └─ID d
)");

    // a + b << c - ~d
  }
  {
    auto result = parse(TokenArray(tok::ID("a"), tok::Operator(Op::Add),
                                   tok::ID("b"), tok::Operator(Op::ShiftLeft),
                                   tok::ID("c"), tok::Operator(Op::Sub),
                                   tok::Operator(Op::Tilde), tok::ID("d")));
    EXPECT_EQ(result, R"(
Binaryop <<
   ├─Binaryop +
   │  ├─ID a
   │  └─ID b
   └─Binaryop -
      ├─ID c
      └─Unaryop ~
         └─ID d
)");

    // a && b | c ^ d & e
  }
  {
    auto result = parse(TokenArray(
        tok::ID("a"), tok::Operator(Op::LogicalAnd), tok::ID("b"),
        tok::Operator(Op::BitOr), tok::ID("c"), tok::Operator(Op::BitXor),
        tok::ID("d"), tok::Operator(Op::BitAnd), tok::ID("e")));
    EXPECT_EQ(result, R"(
Binaryop &&
   ├─ID a
   └─Binaryop |
      ├─ID b
      └─Binaryop ^
         ├─ID c
         └─Binaryop &
            ├─ID d
            └─ID e
)");
  }
}

TEST_F(ExprParserTest, StringLiterals) {
  {
    auto result = parse(TokenArray(tok::ID("foo"), tok::Operator(Op::Add),
                                   tok::Literal("bar")));
    EXPECT_EQ(result, R"(
Binaryop +
   ├─ID foo
   └─StrLiteral bar
)");
  }
}

TEST_F(ExprParserTest, ListLiterals) {
  {
    auto result = parse("[]");
    EXPECT_EQ(result, "ListLiteral");
  }
  {
    auto result = parse(R"( [1,2,"3"] )");
    EXPECT_EQ(result, R"(
ListLiteral
   ├─IntLiteral 1
   ├─IntLiteral 2
   └─StrLiteral 3
)");
  }
  {
    auto result = parse(R"( [1+1,2,foo()+boo()] )");
    EXPECT_EQ(result, R"(
ListLiteral
   ├─Binaryop +
   │  ├─IntLiteral 1
   │  └─IntLiteral 1
   ├─IntLiteral 2
   └─Binaryop +
      ├─Invoke
      │  └─ID foo
      └─Invoke
         └─ID boo
)");
  }
}

TEST_F(ExprParserTest, DictLiterals) {
  {
    auto result = parse("{}");
    EXPECT_EQ(result, "DictLiteral");
  }
  {
    auto result = parse(R"( {1:1,2:2,"3":3} )");
    EXPECT_EQ(result, R"(
 DictLiteral
   ├─IntLiteral 1
   ├─IntLiteral 1
   ├─IntLiteral 2
   ├─IntLiteral 2
   ├─StrLiteral 3
   └─IntLiteral 3
 )");
  }
  {
    auto result = parse(R"( {foo:boo(), [1]:[1]} )");
    EXPECT_EQ(result, R"(
DictLiteral
   ├─ID foo
   ├─Invoke
   │  └─ID boo
   ├─ListLiteral
   │  └─IntLiteral 1
   └─ListLiteral
      └─IntLiteral 1
  )");
  }
}

TEST_F(ExprParserTest, Postfix) {
  // boo()
  {
    auto result = parse(
        TokenArray(tok::ID("boo"), tok::ParenthesisL(), tok::ParenthesisR()));
    EXPECT_EQ(result, R"(
Invoke
   └─ID boo
)");

    // foo(42)
  }
  {
    auto result = parse(TokenArray(tok::ID("foo"), tok::ParenthesisL(),
                                   tok::Int(42), tok::ParenthesisR()));
    EXPECT_EQ(result, R"(
Invoke
   ├─ID foo
   └─IntLiteral 42
)");

    // sum(1,2,3,4)
  }
  {
    auto result = parse(TokenArray(tok::ID("sum"), tok::ParenthesisL(),
                                   tok::Int(1), tok::Operator(Op::Comma),
                                   tok::Int(2), tok::Operator(Op::Comma),
                                   tok::Int(3), tok::Operator(Op::Comma),
                                   tok::Int(4), tok::ParenthesisR()));
    EXPECT_EQ(result, R"(
Invoke
   ├─ID sum
   ├─IntLiteral 1
   ├─IntLiteral 2
   ├─IntLiteral 3
   └─IntLiteral 4
)");
  }
  {
    auto result = parse(R"( count(a,b,c=1+1) )");
    EXPECT_EQ(result, R"(
Invoke
   ├─ID count
   ├─ID a
   ├─ID b
   └─kwarg c
      └─Binaryop +
         ├─IntLiteral 1
         └─IntLiteral 1
)");

    // array(3).field
  }
  {
    auto result = parse(TokenArray(tok::ID("array"), tok::SquareL(),
                                   tok::Int(3), tok::SquareR(),
                                   tok::Operator(Op::Dot), tok::ID("field")));
    EXPECT_EQ(result, R"(
Member
   ├─Subscript
   │  ├─ID array
   │  └─IntLiteral 3
   └─ID field
)");
  }
  {
    auto result = parse(R"( obj.getArray()[idx+1].method(10) )");
    EXPECT_EQ(result, R"(
Invoke
   ├─Member
   │  ├─Subscript
   │  │  ├─Invoke
   │  │  │  └─Member
   │  │  │     ├─ID obj
   │  │  │     └─ID getArray
   │  │  └─Binaryop +
   │  │     ├─ID idx
   │  │     └─IntLiteral 1
   │  └─ID method
   └─IntLiteral 10
)");
  }
  {
    auto result = parse(R"( (foo(bar(1)).baz)["3"] )");
    EXPECT_EQ(result, R"(
Subscript
   ├─Parenthesis
   │  └─Member
   │     ├─Invoke
   │     │  ├─ID foo
   │     │  └─Invoke
   │     │     ├─ID bar
   │     │     └─IntLiteral 1
   │     └─ID baz
   └─StrLiteral 3
)");
  }
}

TEST_F(ExprParserTest, Spawn) {
  {
    auto result = parse("spawn function();");
    EXPECT_EQ(result, R"(
spawn
   └─Invoke
      └─ID function
)");
  }
  {
    auto result = parse("spawn function(a, b);");
    EXPECT_EQ(result, R"(
spawn
   └─Invoke
      ├─ID function
      ├─ID a
      └─ID b
)");
  }
}

TEST_F(ExprParserTest, Await) {
  {
    auto result = parse("await function();");
    EXPECT_EQ(result, R"(
await
   └─Invoke
      └─ID function
)");
  }
  {
    auto result = parse("await f;");
    EXPECT_EQ(result, R"(
await
   └─ID f
)");
  }
}

// -----------------------------------------------------------------------

class StmtParserTest : public ::testing::Test {
 protected:
  using ParserResult = ParserResultBase<AST>;

  static ParserResult parse(char const* s) {
    auto sb = SourceBuffer::Create(std::string(s), "<test>");
    ParserResult r;
    r.src = sb;

    std::vector<Token> tokens;
    Tokenizer tokenizer(tokens);
    tokenizer.Parse(sb);
    if (!tokenizer.Ok()) {
      r.errors = format_errors(tokenizer.GetErrors());
      return r;
    }

    Parser parser(tokens);
    r.ast = parser.ParseStatement();
    if (!parser.Ok())
      r.errors = format_errors(parser.GetErrors());
    return r;
  }
};

TEST_F(StmtParserTest, Assignment) {
  {  // Basic variable assignment
    auto result = parse("v1 = 1 + 2 - 3;");
    EXPECT_EQ(result, R"(
Assign
   ├─ID v1
   └─Binaryop -
      ├─Binaryop +
      │  ├─IntLiteral 1
      │  └─IntLiteral 2
      └─IntLiteral 3
)");
  }

  {  // Basic compound assignment
    auto result = parse("v1+=x+y-y%x;");
    EXPECT_EQ(result, R"(
AugAssign +=
   ├─ID v1
   └─Binaryop -
      ├─Binaryop +
      │  ├─ID x
      │  └─ID y
      └─Binaryop %
         ├─ID y
         └─ID x
)");
  }

  {  // a >>>= b >>> c;
    auto result = parse(" a>>>=b>>>c; ");
    EXPECT_EQ(result, R"(
AugAssign >>>=
   ├─ID a
   └─Binaryop >>>
      ├─ID b
      └─ID c
)");
  }

  {
    auto result = parse("foo[boo] = x;");
    EXPECT_EQ(result, R"(
Assign
   ├─Subscript
   │  ├─ID foo
   │  └─ID boo
   └─ID x
)");
  }

  {
    auto result = parse("foo.boo = x;");
    EXPECT_EQ(result, R"(
Assign
   ├─Member
   │  ├─ID foo
   │  └─ID boo
   └─ID x
)");
  }
}

TEST_F(StmtParserTest, If) {
  auto result = parse("if(a) b; else c;");
  EXPECT_EQ(result, R"(
If
   ├─cond
   │  └─ID a
   ├─then
   │  └─ID b
   └─else
      └─ID c
)");
}

TEST_F(StmtParserTest, While) {
  auto result = parse("while(i<10) i+=1;");
  EXPECT_EQ(result, R"(
While
   ├─cond
   │  └─Binaryop <
   │     ├─ID i
   │     └─IntLiteral 10
   └─body
      └─AugAssign +=
         ├─ID i
         └─IntLiteral 1
)");
}

TEST_F(StmtParserTest, For) {
  auto result = parse("for(i=0;i<10;i+=1) sum += i;");
  EXPECT_EQ(result, R"(
For
   ├─init
   │  └─Assign
   │     ├─ID i
   │     └─IntLiteral 0
   ├─cond
   │  └─Binaryop <
   │     ├─ID i
   │     └─IntLiteral 10
   ├─inc
   │  └─AugAssign +=
   │     ├─ID i
   │     └─IntLiteral 1
   └─body
      └─AugAssign +=
         ├─ID sum
         └─ID i
)");
}

TEST_F(StmtParserTest, Block) {
  auto result = parse("{i=1;j=2;k=3;l=4; {}}");
  EXPECT_EQ(result, R"(
Compound
   ├─Assign
   │  ├─ID i
   │  └─IntLiteral 1
   ├─Assign
   │  ├─ID j
   │  └─IntLiteral 2
   ├─Assign
   │  ├─ID k
   │  └─IntLiteral 3
   ├─Assign
   │  ├─ID l
   │  └─IntLiteral 4
   └─Compound
)");
}

TEST_F(StmtParserTest, FunctionDecl) {
  {
    auto result = parse("fn main(){ a=1; b=2; a+=b; }");
    EXPECT_EQ(result, R"(
fn main()
   └─body
      └─Compound
         ├─Assign
         │  ├─ID a
         │  └─IntLiteral 1
         ├─Assign
         │  ├─ID b
         │  └─IntLiteral 2
         └─AugAssign +=
            ├─ID a
            └─ID b
)");
  }

  {
    auto result = parse("fn bar(a, b=2, *args, **kwargs){}");
    EXPECT_EQ(result, R"(
fn bar(a,b,*args,**kwargs)
   ├─default b
   │  └─IntLiteral 2
   └─body
      └─Compound
)");
  }

  {
    auto result = parse(R"( fn foo(a, b="def", c=1, d=[1,2]) {} )");

    EXPECT_EQ(result, R"(
fn foo(a,b,c,d)
   ├─default b
   │  └─StrLiteral def
   ├─default c
   │  └─IntLiteral 1
   ├─default d
   │  └─ListLiteral
   │     ├─IntLiteral 1
   │     └─IntLiteral 2
   └─body
      └─Compound
)");
  }

  {
    auto result = parse("fn kw_only(a, *args, b=1, c=3) {}");
    EXPECT_EQ(result, R"(
fn kw_only(a,b,c,*args)
   ├─default b
   │  └─IntLiteral 1
   ├─default c
   │  └─IntLiteral 3
   └─body
      └─Compound
)");
  }
}

TEST_F(StmtParserTest, ClassDecl) {
  auto result =
      parse(R"( class Klass{ fn foo(){} fn boo(a,b,c){} fn moo(self,a){} } )");

  EXPECT_EQ(result, R"(
class Klass
   ├─fn moo(self,a)
   │  └─body
   │     └─Compound
   ├─fn foo()
   │  └─body
   │     └─Compound
   └─fn boo(a,b,c)
      └─body
         └─Compound
)");
}

TEST_F(StmtParserTest, PropertyDecl) {
  auto result = parse(R"(
class Box {
  fn method(self) {}
  property size { set {} get {} }
  property read_only { get {} }
  property write_only { set {} }
}
)");
  EXPECT_EQ(result, R"(
class Box
   ├─fn method(self)
   │  └─body
   │     └─Compound
   ├─property size
   │  ├─get
   │  │  └─Compound
   │  └─set
   │     └─Compound
   ├─property read_only
   │  └─get
   │     └─Compound
   └─property write_only
      └─set
         └─Compound
)");
  ASSERT_NE(result.ast, nullptr);
  const auto* klass = result.ast->Get_if<ClassDecl>();
  ASSERT_NE(klass, nullptr);
  ASSERT_EQ(klass->properties.size(), 3);
  const auto& property = klass->properties.front();
  ASSERT_TRUE(property.getter.has_value());
  ASSERT_TRUE(property.setter.has_value());
  EXPECT_EQ(property.getter->params, (std::vector<std::string>{"self"}));
  EXPECT_EQ(property.setter->params,
            (std::vector<std::string>{"self", "value"}));
  EXPECT_EQ(property.getter->name, "<get size>");
  EXPECT_EQ(property.setter->name, "<set size>");
}

TEST_F(StmtParserTest, InvalidPropertyDecl) {
  const std::vector<std::pair<const char*, const char*>> cases = {
      {"class Box { property {} }", "expected property name"},
      {"class Box { property size; }", "expected '{' after property name"},
      {"class Box { property size {} }",
       "property must declare at least one accessor"},
      {"class Box { property size { fn get() {} } }",
       "expected 'get' or 'set' in property body"},
      {"class Box { property size { get(self) {} } }",
       "property accessor body must be a block"},
      {"class Box { property size { set; } }",
       "property accessor body must be a block"},
      {"class Box { property size { get {} get {} } }",
       "duplicate 'get' accessor for property 'size'"},
      {"class Box { property size { set {} set {} } }",
       "duplicate 'set' accessor for property 'size'"},
      {"class Box { property size { get {} } property size { set {} } }",
       "property 'size' conflicts with another class member"},
      {"class Box { fn size(self) {} property size { get {} } }",
       "property 'size' conflicts with another class member"},
      {"class Box { property size { get {} } fn size() {} }",
       "property 'size' conflicts with another class member"},
      {"class Box { property size { get {}",
       "expected '}' after property body"},
      {"class Box { property size { get {} }", "expected '}' after class body"},
      {"property size { get {} }", "Expected ';'."},
  };
  for (const auto& [source, diagnostic] : cases) {
    SCOPED_TRACE(source);
    auto result = parse(source);
    EXPECT_NE(result.errors.find(diagnostic), std::string::npos) << result;
  }
}

TEST_F(StmtParserTest, Return) {
  {
    auto result = parse("return;");
    EXPECT_EQ(result, R"(
return
)");
  }
  {
    auto result = parse("return a+b;");
    EXPECT_EQ(result, R"(
return
   └─Binaryop +
      ├─ID a
      └─ID b
)");
  }
}

TEST_F(StmtParserTest, Yield) {
  {
    auto result = parse("yield;");
    EXPECT_EQ(result, R"(
yield
)");
  }
  {
    auto result = parse("yield a+b;");
    EXPECT_EQ(result, R"(
yield
   └─Binaryop +
      ├─ID a
      └─ID b
)");
  }
}

TEST_F(StmtParserTest, Throw) {
  {
    auto result = parse("throw;");
    EXPECT_EQ(result, R"(
throw
)");
  }
  {
    auto result = parse("throw err;");
    EXPECT_EQ(result, R"(
throw
   └─ID err
)");
  }
}

TEST_F(StmtParserTest, TryCatch) {
  auto result = parse("try{} catch(e){}");
  EXPECT_EQ(result, R"(
try
   ├─try
   │  └─Compound
   └─catch e
      └─Compound
)");
}

TEST_F(StmtParserTest, Scope) {
  auto result = parse("global a, b;");
  EXPECT_EQ(result, R"(
scope a,b
)");
}

TEST_F(StmtParserTest, Import) {
  {
    auto result = parse("import a;");
    EXPECT_EQ(result, R"(
import a
)");
  }
  {
    auto result = parse("import a as b;");
    EXPECT_EQ(result, R"(
import a as b
)");
  }
  {
    auto result = parse("from a import b as c, d as e, f as g;");
    EXPECT_EQ(result, R"(
from a import b as c,d as e,f as g
)");
  }
}

}  // namespace m6test
