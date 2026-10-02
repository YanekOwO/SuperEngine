// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
// Copyright (C) 2008 Elliot Glaysher
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

#pragma once

#include <boost/serialization/access.hpp>
#include <iosfwd>

class Rect;
class Size;

class Point {
 public:
  // Constructors
  Point() = default;
  Point(int x, int y);
  explicit Point(Size size);

  // Accessors
  int x() const;
  void set_x(int in);
  int y() const;
  void set_y(int in);

  // Utility
  bool is_empty() const;
  [[nodiscard]] std::string DebugString() const;

  // Operators
  Point& operator+=(const Point& rhs);
  Point& operator-=(const Point& rhs);
  Point operator+(const Point& rhs) const;
  Point operator+(const Size& rhs) const;
  Point operator-(const Size& rhs) const;
  Size operator-(const Point& rhs) const;

  bool operator==(const Point& rhs) const;
  bool operator!=(const Point& rhs) const;

 private:
  int x_ = 0;
  int y_ = 0;

  // boost::serialization support
  friend class boost::serialization::access;
  template <class Archive>
  void serialize(Archive& ar, unsigned int version) {
    ar & x_ & y_;
  }
};

class Size {
 public:
  // Constructors
  Size() = default;
  Size(int width, int height);
  explicit Size(Point p);

  // Accessors
  int width() const;
  void set_width(int width);
  int height() const;
  void set_height(int height);

  // Utility
  bool is_empty() const;
  [[nodiscard]] std::string DebugString() const;

  // Methods
  Rect CenteredIn(const Rect& r) const;
  Size SizeUnion(const Size& rhs) const;

  // Operators
  Size& operator+=(const Size& rhs);
  Size& operator-=(const Size& rhs);
  Size operator+(const Size& rhs) const;
  Size operator-(const Size& rhs) const;
  Size operator*(float factor) const;
  Size operator/(int denominator) const;

  bool operator==(const Size& rhs) const;
  bool operator!=(const Size& rhs) const;

 private:
  int width_ = 0;
  int height_ = 0;

  // boost::serialization support
  friend class boost::serialization::access;
  template <class Archive>
  void serialize(Archive& ar, unsigned int version) {
    ar & width_ & height_;
  }
};

class Rect {
 public:
  // Constructors
  Rect() = default;
  Rect(const Point& point1, const Point& point2);
  Rect(const Point& origin, const Size& size);
  Rect(int x, int y, const Size& size);

  // Static factory methods
  static Rect GRP(int x1, int y1, int x2, int y2);
  static Rect REC(int x, int y, int width, int height);

  // Accessors
  int x() const;
  void set_x(int in);
  int y() const;
  void set_y(int in);
  int x2() const;
  void set_x2(int in);
  int y2() const;
  void set_y2(int in);
  inline int width() const { return size_.width(); }
  inline int height() const { return size_.height(); }

  const Point lower_right() const;
  const Size& size() const;
  const Point& origin() const;

  // Utility
  inline bool is_empty() const { return width() == 0 && height() == 0; }
  inline bool is_degenerate() const { return width() <= 0 || height() <= 0; }
  [[nodiscard]] std::string DebugString() const;

  // Methods
  bool Contains(const Point& loc) const;
  bool Intersects(const Rect& rhs) const;
  Rect Intersection(const Rect& rhs) const;
  Rect Union(const Rect& rhs) const;
  Rect GetInsetRectangle(const Rect& rhs) const;
  Rect ApplyInset(const Rect& inset) const;

  // Operators
  bool operator==(const Rect& rhs) const;
  bool operator!=(const Rect& rhs) const;
  explicit operator std::string() const;

 private:
  Point origin_;
  Size size_;

  // boost::serialization support
  friend class boost::serialization::access;
  template <class Archive>
  void serialize(Archive& ar, unsigned int version) {
    ar & origin_ & size_;
  }
};  // end of class Rect

// -----------------------------------------------------------------------

// Returns the largest centered sub-rectangle of Rect(Point(0, 0), bounds)
// that keeps the aspect ratio of |content|. |content| must not be empty.
Rect AspectFitRect(Size content, Size bounds);

// -----------------------------------------------------------------------

std::ostream& operator<<(std::ostream& os, const Size& s);
std::ostream& operator<<(std::ostream& os, const Point& p);
std::ostream& operator<<(std::ostream& os, const Rect& r);
