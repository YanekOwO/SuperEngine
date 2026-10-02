// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
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

#include "core/rect.hpp"
#include "utilities/string_utilities.hpp"

#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <sstream>
#include <string>

namespace {
template <typename... Ts>
std::string ToString(Ts&&... params) {
  std::string result = ((std::to_string(std::forward<Ts>(params)) + ' ') + ...);
  if constexpr (sizeof...(Ts) > 0)
    result.pop_back();
  return result;
}
}  // namespace

// -----------------------------------------------------------------------
// Point
// -----------------------------------------------------------------------

TEST(PointTest, DefaultConstructor) {
  Point p;
  EXPECT_EQ(ToString(p.x(), p.y()), "0 0");
  EXPECT_TRUE(p.is_empty());
}

TEST(PointTest, ConstructFromSize) {
  Size ps(15, 20);
  Point p = Point(ps);
  EXPECT_EQ(ToString(p.x(), p.y()), "15 20");
}

TEST(PointTest, ParameterizedConstructor) {
  Point p(5, 10);
  EXPECT_EQ(ToString(p.x(), p.y()), "5 10");
  EXPECT_FALSE(p.is_empty());
}

TEST(PointTest, AccessorsMutators) {
  Point p;
  p.set_x(7);
  p.set_y(14);
  EXPECT_EQ(ToString(p.x(), p.y()), "7 14");
  EXPECT_FALSE(p.is_empty());
}

TEST(PointTest, EqualityOperators) {
  Point p1(3, 4);
  Point p2(3, 4);
  Point p3(5, 6);

  EXPECT_EQ(ToString((p1 == p2), (p1 != p2), (p1 == p3), (p1 != p3)),
            "1 0 0 1");
}

TEST(PointTest, AdditionAssignmentOperator) {
  Point p1(1, 2);
  Point p2(3, 4);
  p1 += p2;
  EXPECT_EQ(ToString(p1.x(), p1.y()), "4 6");
}

TEST(PointTest, SubtractionAssignmentOperator) {
  Point p1(5, 7);
  Point p2(2, 3);
  p1 -= p2;
  EXPECT_EQ(ToString(p1.x(), p1.y()), "3 4");
}

TEST(PointTest, AdditionOperatorPoint) {
  Point p1(1, 2);
  Point p2(3, 4);
  Point p3 = p1 + p2;
  EXPECT_EQ(ToString(p1.x(), p1.y()), "1 2");
  EXPECT_EQ(ToString(p3.x(), p3.y()), "4 6");
}

TEST(PointTest, AdditionOperatorSize) {
  Point p(1, 2);
  Size s(3, 4);
  Point result = p + s;
  EXPECT_EQ(ToString(result.x(), result.y()), "4 6");
}

TEST(PointTest, SubtractionOperatorSize) {
  Point p(5, 7);
  Size s(2, 3);
  Point result = p - s;
  EXPECT_EQ(ToString(result.x(), result.y()), "3 4");
}

TEST(PointTest, SubtractPointOperator) {
  Point p1(10, 15);
  Point p2(4, 7);
  Size s = p1 - p2;
  EXPECT_EQ(ToString(s.width(), s.height()), "6 8");
}

TEST(PointTest, ChainingOperators) {
  Point p(1, 1);
  Size s(2, 2);
  Point result = (p + s) - s;
  EXPECT_EQ(result, p);
}

TEST(PointTest, Serialization) {
  Point p1(5, 10);
  std::stringstream ss;
  {
    boost::archive::text_oarchive oa(ss);
    oa << p1;
  }
  Point p2;
  {
    boost::archive::text_iarchive ia(ss);
    ia >> p2;
  }
  EXPECT_EQ(p1, p2);
}

// -----------------------------------------------------------------------
// Size
// -----------------------------------------------------------------------

TEST(SizeTest, DefaultConstructor) {
  Size s;
  EXPECT_EQ(ToString(s.width(), s.height(), s.is_empty()), "0 0 1");
}

TEST(SizeTest, ConstructFromPoint) {
  Point p(15, 20);
  Size ps = Size(p);
  EXPECT_EQ(ToString(ps.width(), ps.height()), "15 20");
}

TEST(SizeTest, ParameterizedConstructor) {
  Size s(5, 10);
  EXPECT_EQ(ToString(s.width(), s.height()), "5 10");
  EXPECT_FALSE(s.is_empty());
}

TEST(SizeTest, AccessorsMutators) {
  Size s;
  s.set_width(7);
  s.set_height(14);
  EXPECT_EQ(ToString(s.width(), s.height()), "7 14");
  EXPECT_FALSE(s.is_empty());
}

TEST(SizeTest, EqualityOperators) {
  Size s1(3, 4);
  Size s2(3, 4);
  Size s3(5, 6);
  EXPECT_EQ(ToString((s1 == s2), (s1 != s2), (s1 == s3), (s1 != s3)),
            "1 0 0 1");
}

TEST(SizeTest, AdditionAssignmentOperator) {
  Size s1(1, 2);
  Size s2(3, 4);
  s1 += s2;
  EXPECT_EQ(ToString(s1.width(), s1.height()), "4 6");
}

TEST(SizeTest, NegativeValues) {
  Size s1(-5, -10);
  EXPECT_EQ(s1.width(), -5);
  EXPECT_EQ(s1.height(), -10);
  Size s2(10, 20);
  Size s3 = s1 + s2;
  EXPECT_EQ(s3.width(), 5);
  EXPECT_EQ(s3.height(), 10);
}

TEST(SizeTest, SubtractionAssignmentOperator) {
  Size s1(5, 7);
  Size s2(2, 3);
  s1 -= s2;
  EXPECT_EQ(ToString(s1.width(), s1.height()), "3 4");
}

TEST(SizeTest, AdditionOperator) {
  Size s1(1, 2);
  Size s2(3, 4);
  Size s3 = s1 + s2;
  EXPECT_EQ(ToString(s3.width(), s3.height()), "4 6");
}

TEST(SizeTest, SubtractionOperator) {
  Size s1(5, 7);
  Size s2(2, 3);
  Size s3 = s1 - s2;
  EXPECT_EQ(ToString(s3.width(), s3.height()), "3 4");
}

TEST(SizeTest, MultiplicationOperator) {
  Size s(2, 3);
  float factor = 2.5f;
  Size result = s * factor;
  EXPECT_EQ(result.width(), static_cast<int>(2 * 2.5f));
  EXPECT_EQ(result.height(), static_cast<int>(3 * 2.5f));
}

TEST(SizeTest, DivisionOperator) {
  Size s(5, 10);
  int denominator = 2;
  Size result = s / denominator;
  EXPECT_EQ(ToString(result.width(), result.height()), "2 5");
}

TEST(SizeTest, MultiplicationByZero) {
  Size s(10, 20);
  float factor = 0.0f;
  Size result = s * factor;
  EXPECT_EQ(result.width(), 0);
  EXPECT_EQ(result.height(), 0);
}

TEST(SizeTest, ChainingOperators) {
  Size s1(5, 5);
  Size s2(2, 2);
  Size result = (s1 + s2) - s2 * 2;
  EXPECT_EQ(result.width(), 3);
  EXPECT_EQ(result.height(), 3);
}

TEST(SizeTest, SizeUnion) {
  Size s1(5, 7);
  Size s2(3, 10);
  Size result = s1.SizeUnion(s2);
  EXPECT_EQ(ToString(result.width(), result.height()), "5 10");
}

TEST(SizeTest, CenteredIn) {
  Size s(10, 10);
  Rect outer_rect(0, 0, Size(30, 30));
  Rect centered_rect = s.CenteredIn(outer_rect);
  EXPECT_EQ(centered_rect.x(), 10);
  EXPECT_EQ(centered_rect.y(), 10);
  EXPECT_EQ(centered_rect.width(), 10);
  EXPECT_EQ(centered_rect.height(), 10);
}

TEST(SizeTest, Serialization) {
  Size s1(5, 10);
  std::stringstream ss;
  {
    boost::archive::text_oarchive oa(ss);
    oa << s1;
  }
  Size s2;
  {
    boost::archive::text_iarchive ia(ss);
    ia >> s2;
  }
  EXPECT_EQ(s1, s2);
}

// -----------------------------------------------------------------------
// Rect
// -----------------------------------------------------------------------

TEST(RectTest, DefaultConstructor) {
  Rect r;
  EXPECT_EQ(ToString(r.x(), r.y(), r.width(), r.height(), r.is_empty()),
            "0 0 0 0 1");
}

TEST(RectTest, ConstructorWithTwoPoints) {
  Point p1(1, 2);
  Point p2(4, 6);
  Rect r(p1, p2);
  EXPECT_EQ(ToString(r.x(), r.y(), r.x2(), r.y2(), r.width(), r.height(),
                     r.is_empty()),
            "1 2 4 6 3 4 0");
}

TEST(RectTest, ConstructorWithPositionAndSize) {
  Size size(5, 10);
  Rect r(2, 3, size);
  EXPECT_EQ(ToString(r.x(), r.y(), r.width(), r.height(), r.is_empty()),
            "2 3 5 10 0");
}

TEST(RectTest, AccessorsMutators) {
  Rect r;
  r.set_x(5);
  r.set_y(10);
  r.set_x2(15);
  r.set_y2(20);
  EXPECT_EQ(ToString(r.x(), r.y(), r.x2(), r.y2(), r.width(), r.height(),
                     r.is_empty()),
            "5 10 15 20 10 10 0");
}

TEST(RectTest, EqualityOperators) {
  Rect r1(0, 0, Size(10, 10));
  Rect r2(0, 0, Size(10, 10));
  Rect r3(5, 5, Size(10, 10));
  EXPECT_EQ(ToString((r1 == r2), (r1 != r2), (r1 == r3), (r1 != r3)),
            "1 0 0 1");
}

TEST(RectTest, ContainsPoint) {
  Rect r(0, 0, Size(10, 10));
  Point p_inside(5, 5);
  Point p_outside(15, 15);
  EXPECT_TRUE(r.Contains(p_inside));
  EXPECT_FALSE(r.Contains(p_outside));
}

TEST(RectTest, IntersectsRect) {
  Rect r1(0, 0, Size(10, 10));
  Rect r2(5, 5, Size(10, 10));
  Rect r3(15, 15, Size(5, 5));
  EXPECT_TRUE(r1.Intersects(r2));
  EXPECT_FALSE(r1.Intersects(r3));
}

TEST(RectTest, Intersection) {
  Rect r1(0, 0, Size(10, 10));
  Rect r2(5, 5, Size(10, 10));
  Rect expected_intersection(5, 5, Size(5, 5));
  Rect result = r1.Intersection(r2);
  EXPECT_EQ(result, expected_intersection);
}

TEST(RectTest, Union) {
  Rect r1(0, 0, Size(10, 10));
  Rect r2(5, 5, Size(10, 10));
  Rect expected_union(0, 0, Size(15, 15));
  Rect result = r1.Union(r2);
  EXPECT_EQ(result, expected_union);
}

TEST(RectTest, IsEmpty) {
  Rect r;
  EXPECT_TRUE(r.is_empty());
  Rect r2(0, 0, Size(0, 0));
  EXPECT_TRUE(r2.is_empty());
  Rect r3(0, 0, Size(10, 10));
  EXPECT_FALSE(r3.is_empty());
}

TEST(RectTest, AccessorMethods) {
  Rect r(5, 10, Size(15, 20));
  Point origin = r.origin();
  EXPECT_EQ(origin.x(), 5);
  EXPECT_EQ(origin.y(), 10);
  Size size = r.size();
  EXPECT_EQ(size.width(), 15);
  EXPECT_EQ(size.height(), 20);
  Point lower_right = r.lower_right();
  EXPECT_EQ(lower_right.x(), r.x2());
  EXPECT_EQ(lower_right.y(), r.y2());
}

TEST(RectTest, StaticFactoryMethods) {
  Rect r1 = Rect::GRP(0, 0, 10, 10);
  EXPECT_EQ(ToString(r1.x(), r1.y(), r1.x2(), r1.y2(), r1.width(), r1.height()),
            "0 0 10 10 10 10");
  Rect r2 = Rect::REC(5, 5, 15, 20);
  EXPECT_EQ(ToString(r2.x(), r2.y(), r2.x2(), r2.y2(), r2.width(), r2.height()),
            "5 5 20 25 15 20");
}

TEST(RectTest, GetInsetRectangle) {
  Rect outer_rect(0, 0, Size(20, 20));
  Rect inset(2, 2, Size(16, 16));
  Rect result = outer_rect.GetInsetRectangle(inset);
  EXPECT_EQ(ToString(result.x(), result.y(), result.width(), result.height()),
            "2 2 16 16");
}

TEST(RectTest, ApplyInset) {
  Rect rect(10, 10, Size(30, 30));
  Rect inset(5, 5, Size(20, 20));
  Rect result = rect.ApplyInset(inset);
  EXPECT_EQ(ToString(result.x(), result.y(), result.width(), result.height()),
            "15 15 20 20");
}

TEST(RectTest, ContainsPointEdgeCases) {
  Rect r(0, 0, Size(10, 10));
  Point corner(0, 0);
  Point outside(10, 10);
  EXPECT_TRUE(r.Contains(corner));
  EXPECT_FALSE(r.Contains(outside));
}

TEST(RectTest, IntersectsEdgeCases) {
  Rect r1(0, 0, Size(10, 10));
  Rect touch_corner(10, 10, Size(5, 5));
  Rect overlap(5, 5, Size(5, 5));
  EXPECT_FALSE(r1.Intersects(touch_corner));
  EXPECT_TRUE(r1.Intersects(overlap));
}

TEST(RectTest, IntersectionNonOverlapping) {
  Rect r1(0, 0, Size(10, 10));
  Rect r2(20, 20, Size(5, 5));
  Rect result = r1.Intersection(r2);
  EXPECT_TRUE(result.is_empty());
}

TEST(RectTest, UnionDisjointRects) {
  Rect r1(0, 0, Size(10, 10));
  Rect r2(20, 20, Size(5, 5));
  Rect result = r1.Union(r2);
  EXPECT_EQ(result.x(), 0);
  EXPECT_EQ(result.y(), 0);
  EXPECT_EQ(result.width(), 25);
  EXPECT_EQ(result.height(), 25);
}

TEST(RectTest, Serialization) {
  Rect r1(5, 10, Size(15, 20));
  std::stringstream ss;
  {
    boost::archive::text_oarchive oa(ss);
    oa << r1;
  }
  Rect r2;
  {
    boost::archive::text_iarchive ia(ss);
    ia >> r2;
  }
  EXPECT_EQ(r1, r2);
}

TEST(RectTest, AspectFitRect) {
  EXPECT_EQ(AspectFitRect(Size(640, 480), Size(640, 480)),
            Rect(Point(0, 0), Size(640, 480)));
  EXPECT_EQ(AspectFitRect(Size(640, 480), Size(1920, 1080)),
            Rect(Point(240, 0), Size(1440, 1080)));
  EXPECT_EQ(AspectFitRect(Size(640, 480), Size(800, 900)),
            Rect(Point(0, 150), Size(800, 600)));
  EXPECT_EQ(AspectFitRect(Size(800, 600), Size(1366, 768)),
            Rect(Point(171, 0), Size(1024, 768)));
  EXPECT_EQ(AspectFitRect(Size(640, 480), Size(320, 240)),
            Rect(Point(0, 0), Size(320, 240)));
}
