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

#include <set>
#include <unordered_set>

#include "vm/dict.hpp"
#include "vm/gc.hpp"
#include "vm/list.hpp"
#include "vm/object.hpp"
#include "vm/primops.hpp"
#include "vm/string.hpp"
#include "vm/value.hpp"

namespace value_test {
using namespace serilang;

class ValueArithmeticTest : public ::testing::Test {
 protected:
  GarbageCollector gc;

  template <typename T, typename... Ts>
  inline auto Alloc(Ts&&... params) {
    return gc.Allocate<T>(std::forward<Ts>(params)...);
  }

  std::optional<Value> eval(Value lhs, Op op, Value rhs) {
    std::optional<TempValue> result = primops::EvaluateBinary(op, lhs, rhs);
    if (!result)
      return std::nullopt;
    return gc.TrackValue(std::move(*result));
  }
  std::optional<Value> eval(Op op, Value rhs) {
    std::optional<TempValue> result = primops::EvaluateUnary(op, rhs);
    if (!result)
      return std::nullopt;
    return gc.TrackValue(std::move(*result));
  }

  inline Value v(const char* s) {
    String* str = gc.Allocate<String>(s);
    return Value(str);
  }
};

TEST_F(ValueArithmeticTest, TruthinessAndType) {
  Value nil;
  Value bTrue(true);
  Value bFalse(false);
  Value iZero(0);
  Value iPos(42);
  Value dZero(0.0);
  Value dPos(3.14);
  Value strEmpty = v("");
  Value strNonEmpty = v("hi");

  EXPECT_FALSE(nil.IsTruthy());
  EXPECT_TRUE(bTrue.IsTruthy());
  EXPECT_FALSE(bFalse.IsTruthy());
  EXPECT_FALSE(iZero.IsTruthy());
  EXPECT_TRUE(iPos.IsTruthy());
  EXPECT_FALSE(dZero.IsTruthy());
  EXPECT_TRUE(dPos.IsTruthy());
  EXPECT_FALSE(strEmpty.IsTruthy());
  EXPECT_TRUE(strNonEmpty.IsTruthy());

  EXPECT_EQ(nil.Type(), ObjType::Nil);
  EXPECT_EQ(bTrue.Type(), ObjType::Bool);
  EXPECT_EQ(iPos.Type(), ObjType::Int);
  EXPECT_EQ(dPos.Type(), ObjType::Double);
  EXPECT_EQ(strNonEmpty.Type(), ObjType::String);
}

TEST_F(ValueArithmeticTest, IntAndDoubleArithmetic) {
  Value a(6), b(3), c(2.5), d(1.5);

  EXPECT_EQ(eval(a, Op::Add, b), 9);
  EXPECT_EQ(eval(a, Op::Sub, b), 3);
  EXPECT_EQ(eval(a, Op::Mul, b), 18);
  EXPECT_EQ(eval(a, Op::Div, b), 2);

  EXPECT_EQ(eval(c, Op::Add, d), 4.0);
  EXPECT_EQ(eval(c, Op::Sub, d), 1.0);
  EXPECT_EQ(eval(c, Op::Mul, d), 3.75);
  EXPECT_EQ(eval(c, Op::Div, d), (2.5 / 1.5));

  // mixed int / double
  EXPECT_EQ(eval(a, Op::Add, d), 7.5);
  EXPECT_EQ(eval(c, Op::Mul, b), 7.5);
}

TEST_F(ValueArithmeticTest, NumericComparisons) {
  Value one(1), two(2), oneD(1.0), twoD(2.0);

  EXPECT_EQ(eval(one, Op::Less, two), true);
  EXPECT_EQ(eval(one, Op::GreaterEqual, one), true);
  EXPECT_EQ(eval(twoD, Op::Equal, two), true);
  EXPECT_EQ(eval(oneD, Op::NotEqual, twoD), true);
}

TEST_F(ValueArithmeticTest, NumericUnaryOperators) {
  Value five(5), minusFive(-5), pi(3.14);

  EXPECT_EQ(eval(Op::Sub, five), -5);
  EXPECT_EQ(eval(Op::Sub, minusFive), 5);
  EXPECT_EQ(eval(Op::Sub, pi), -3.14);
}

TEST_F(ValueArithmeticTest, IntBitwiseShift) {
  Value v1(1), shift3(3);

  EXPECT_EQ(eval(v1, Op::ShiftLeft, shift3), 8);
  EXPECT_EQ(eval(Value(15), Op::BitAnd, Value(9)), 9);
  EXPECT_EQ(eval(Value(12), Op::BitOr, Value(3)), 15);
  EXPECT_EQ(eval(Value(5), Op::BitXor, Value(1)), 4);
}

TEST_F(ValueArithmeticTest, BoolLogicalOps) {
  Value t(true), f(false);

  EXPECT_EQ(eval(t, Op::LogicalAnd, f), false);
  EXPECT_EQ(eval(t, Op::LogicalOr, f), true);
  EXPECT_EQ(eval(f, Op::LogicalOr, f), false);

  // unary logical NOT (mapped to Op::Tilde)
  EXPECT_EQ(eval(Op::Tilde, t), false);
  EXPECT_EQ(eval(Op::Tilde, f), true);
}

TEST_F(ValueArithmeticTest, StringConcatenateAndRepeat) {
  Value hello = v("hello");
  Value world = v("world");
  Value three(3);

  EXPECT_EQ(eval(hello, Op::Add, world), "helloworld");
  EXPECT_EQ(eval(v("ab"), Op::Mul, three), "ababab");
}

TEST_F(ValueArithmeticTest, DivisionByZero) {
  Value six(6), zero(0);

  // int / 0 returns 0 per implementation
  EXPECT_EQ(eval(six, Op::Div, zero), 0);

  // double / 0.0 returns 0.0 per implementation
  EXPECT_EQ(eval(Value(4.2), Op::Div, Value(0.0)), 0.0);
}

TEST_F(ValueArithmeticTest, NilValue) {
  Value nil = serilang::nil;
  Dict* dict = Alloc<Dict>();

  auto testNilEquality = [&](Value v) {
    Op op[2] = {Op::Equal, Op::NotEqual};
    if (v.Type() == ObjType::Nil)
      std::swap(op[0], op[1]);
    EXPECT_EQ(eval(nil, op[0], v), false) << ToString(op[0]) << ' ' << v.Desc();
    EXPECT_EQ(eval(v, op[0], nil), false) << ToString(op[0]) << ' ' << v.Desc();
    EXPECT_EQ(eval(nil, op[1], v), true) << ToString(op[1]) << ' ' << v.Desc();
    EXPECT_EQ(eval(v, op[1], nil), true) << ToString(op[1]) << ' ' << v.Desc();
  };
  testNilEquality(nil);
  testNilEquality(Value(true));
  testNilEquality(Value(false));
  testNilEquality(Value(123));
  testNilEquality(Value(123.4));
  testNilEquality(v("hello"));
  testNilEquality(Value(dict));

  EXPECT_EQ(eval(Op::Tilde, nil), true);
}

class ValueTest : public ::testing::Test {
 protected:
  GarbageCollector gc;

  template <typename T, typename... Ts>
  inline auto Alloc(Ts&&... params) {
    return gc.Allocate<T>(std::forward<Ts>(params)...);
  }
};

TEST_F(ValueTest, TriviallyCopyable) {
  EXPECT_TRUE(std::is_trivially_copyable<Value>::value);
}

TEST_F(ValueTest, ContainerListAndDict) {
  // empty & filled lists
  Value lstEmpty(Alloc<List>());
  Value lstFilled(
      Alloc<List>(std::vector<Value>{Value(1), Value(2), Value(3)}));

  // empty & filled dicts
  Value dictEmpty(Alloc<Dict>());
  Value ka(Alloc<String>("a"));
  Value dictFilled(
      Alloc<Dict>(std::unordered_map<Value, Value>{{ka, Value(1)}}));

  // All container objects are truthy
  EXPECT_TRUE(lstEmpty.IsTruthy());
  EXPECT_TRUE(lstFilled.IsTruthy());
  EXPECT_TRUE(dictEmpty.IsTruthy());
  EXPECT_TRUE(dictFilled.IsTruthy());

  // Correct dynamic type
  EXPECT_EQ(lstEmpty.Type(), ObjType::List);
  EXPECT_EQ(lstFilled.Type(), ObjType::List);
  EXPECT_EQ(dictEmpty.Type(), ObjType::Dict);
  EXPECT_EQ(dictFilled.Type(), ObjType::Dict);

  // Simple Desc() sanity checks
  EXPECT_EQ(lstEmpty.Desc(), "<list[0]>");
  EXPECT_EQ(lstFilled.Desc(), "<list[3]>");
  EXPECT_EQ(dictEmpty.Desc(), "<dict{0}>");
  EXPECT_EQ(dictFilled.Desc(), "<dict{1}>");

  // Str() formatting smoke test
  EXPECT_EQ(lstFilled.Str(), "[1,2,3]");
  EXPECT_EQ(lstEmpty.Str(), "[]");
  EXPECT_EQ(dictFilled.Str(), "{a:1}");
  EXPECT_EQ(dictEmpty.Str(), "{}");
}

TEST_F(ValueTest, ClassAndInstance) {
  Value base(Alloc<Class>());
  Value inst(Alloc<Instance>(base.Get_if<Class>()));

  EXPECT_NO_THROW(inst.SetMember("mem", Value(123)));
  EXPECT_EQ(gc.TrackValue(inst.GetMember("mem")), 123);
}

TEST_F(ValueTest, Code) {
  Code* code = Alloc<Code>();
  Value v(code);

  EXPECT_EQ(v.Type(), ObjType::Code);
  EXPECT_EQ(v.Get<Code*>(), code);
}

TEST_F(ValueTest, ValueComparisonRejectsDifferentUnderlyingTypes) {
  const Value one(1);
  const Value truth(true);
  const Value one_double(1.0);

  EXPECT_THROW(static_cast<void>(one == truth), ValueError);
  EXPECT_THROW(static_cast<void>(one < truth), ValueError);
  EXPECT_THROW(static_cast<void>(truth == one_double), ValueError);
}

TEST_F(ValueTest, OrderedAndHashedContainersSupportValueKeys) {
  std::set<Value> ints;
  EXPECT_TRUE(ints.insert(Value(2)).second);
  EXPECT_TRUE(ints.insert(Value(1)).second);
  EXPECT_FALSE(ints.insert(Value(2)).second);
  EXPECT_EQ(*ints.begin(), 1);

  Value hello1(Alloc<String>("hello"));
  Value hello2(Alloc<String>("hello"));

  EXPECT_TRUE(hello1 == hello2);
  EXPECT_EQ(std::hash<Value>{}(hello1), std::hash<Value>{}(hello2));

  std::set<Value> strings;
  EXPECT_TRUE(strings.insert(hello1).second);
  EXPECT_FALSE(strings.insert(hello2).second);

  std::unordered_set<Value> string_set;
  EXPECT_TRUE(string_set.insert(hello1).second);
  EXPECT_FALSE(string_set.insert(hello2).second);

  std::set<Value> mixed;
  EXPECT_TRUE(mixed.insert(Value(1)).second);
  EXPECT_THROW(mixed.insert(Value(true)), ValueError);
}

}  // namespace value_test
