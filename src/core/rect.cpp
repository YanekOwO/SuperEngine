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

#include "core/rect.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>

// -----------------------------------------------------------------------
// class Point
// -----------------------------------------------------------------------

Point::Point(int x, int y) : x_(x), y_(y) {}
Point::Point(Size size) : x_(size.width()), y_(size.height()) {}

int Point::x() const { return x_; }
void Point::set_x(int in) { x_ = in; }
int Point::y() const { return y_; }
void Point::set_y(int in) { y_ = in; }

bool Point::is_empty() const { return x_ == 0 && y_ == 0; }
std::string Point::DebugString() const {
  return std::format("Point({}, {})", x_, y_);
}

Point& Point::operator+=(const Point& rhs) {
  x_ += rhs.x_;
  y_ += rhs.y_;
  return *this;
}

Point& Point::operator-=(const Point& rhs) {
  x_ -= rhs.x_;
  y_ -= rhs.y_;
  return *this;
}

Point Point::operator+(const Point& rhs) const {
  return Point(x_ + rhs.x_, y_ + rhs.y_);
}

Point Point::operator+(const Size& rhs) const {
  return Point(x_ + rhs.width(), y_ + rhs.height());
}

Point Point::operator-(const Size& rhs) const {
  return Point(x_ - rhs.width(), y_ - rhs.height());
}

Size Point::operator-(const Point& rhs) const {
  return Size(x_ - rhs.x_, y_ - rhs.y_);
}

bool Point::operator==(const Point& rhs) const {
  return x_ == rhs.x_ && y_ == rhs.y_;
}

bool Point::operator!=(const Point& rhs) const { return !(*this == rhs); }

// -----------------------------------------------------------------------
// class Size
// -----------------------------------------------------------------------

Size::Size(int width, int height) : width_(width), height_(height) {}
Size::Size(Point p) : width_(p.x()), height_(p.y()) {}

int Size::width() const { return width_; }
void Size::set_width(int width) { width_ = width; }
int Size::height() const { return height_; }
void Size::set_height(int height) { height_ = height; }

bool Size::is_empty() const { return width_ == 0 && height_ == 0; }
std::string Size::DebugString() const {
  return std::format("Size({}, {})", width_, height_);
}

Rect Size::CenteredIn(const Rect& r) const {
  int new_x = r.x() + (r.width() - width_) / 2;
  int new_y = r.y() + (r.height() - height_) / 2;
  return Rect(Point(new_x, new_y), *this);
}

Size Size::SizeUnion(const Size& rhs) const {
  return Size(std::max(width_, rhs.width_), std::max(height_, rhs.height_));
}

Size& Size::operator+=(const Size& rhs) {
  width_ += rhs.width_;
  height_ += rhs.height_;
  return *this;
}

Size& Size::operator-=(const Size& rhs) {
  width_ -= rhs.width_;
  height_ -= rhs.height_;
  return *this;
}

Size Size::operator+(const Size& rhs) const {
  return Size(width_ + rhs.width_, height_ + rhs.height_);
}

Size Size::operator-(const Size& rhs) const {
  return Size(width_ - rhs.width_, height_ - rhs.height_);
}

Size Size::operator*(float factor) const {
  return Size(static_cast<int>(width_ * factor),
              static_cast<int>(height_ * factor));
}

Size Size::operator/(int denominator) const {
  return Size(width_ / denominator, height_ / denominator);
}

bool Size::operator==(const Size& rhs) const {
  return width_ == rhs.width_ && height_ == rhs.height_;
}

bool Size::operator!=(const Size& rhs) const { return !(*this == rhs); }

// -----------------------------------------------------------------------
// class Rect
// -----------------------------------------------------------------------

Rect::Rect(const Point& point1, const Point& point2)
    : origin_(point1),
      size_(point2.x() - point1.x(), point2.y() - point1.y()) {}

Rect::Rect(const Point& origin, const Size& size)
    : origin_(origin), size_(size) {}

Rect::Rect(int x, int y, const Size& size) : origin_(x, y), size_(size) {}

Rect Rect::GRP(int x1, int y1, int x2, int y2) {
  return Rect(Point(x1, y1), Point(x2, y2));
}

Rect Rect::REC(int x, int y, int width, int height) {
  return Rect(Point(x, y), Size(width, height));
}

int Rect::x() const { return origin_.x(); }
void Rect::set_x(int in) { origin_.set_x(in); }
int Rect::y() const { return origin_.y(); }
void Rect::set_y(int in) { origin_.set_y(in); }
int Rect::x2() const { return origin_.x() + size_.width(); }
void Rect::set_x2(int in) { size_.set_width(in - origin_.x()); }
int Rect::y2() const { return origin_.y() + size_.height(); }
void Rect::set_y2(int in) { size_.set_height(in - origin_.y()); }

const Point Rect::lower_right() const { return origin_ + size_; }
const Size& Rect::size() const { return size_; }
const Point& Rect::origin() const { return origin_; }

std::string Rect::DebugString() const {
  return std::format("Rect({}, {}, {})", x(), y(), size().DebugString());
}

bool Rect::Contains(const Point& loc) const {
  return loc.x() >= x() && loc.x() < x2() && loc.y() >= y() && loc.y() < y2();
}

bool Rect::Intersects(const Rect& rhs) const {
  return x() < rhs.x2() && x2() > rhs.x() && y() < rhs.y2() && y2() > rhs.y();
}

Rect Rect::Intersection(const Rect& rhs) const {
  if (!Intersects(rhs))
    return Rect();

  int new_x = std::max(x(), rhs.x());
  int new_y = std::max(y(), rhs.y());
  int new_x2 = std::min(x2(), rhs.x2());
  int new_y2 = std::min(y2(), rhs.y2());

  return Rect::GRP(new_x, new_y, new_x2, new_y2);
}

Rect Rect::Union(const Rect& rhs) const {
  if (is_empty())
    return rhs;
  if (rhs.is_empty())
    return *this;

  int new_x = std::min(x(), rhs.x());
  int new_y = std::min(y(), rhs.y());
  int new_x2 = std::max(x2(), rhs.x2());
  int new_y2 = std::max(y2(), rhs.y2());

  return Rect::GRP(new_x, new_y, new_x2, new_y2);
}

Rect Rect::GetInsetRectangle(const Rect& rhs) const {
  Size offset = rhs.origin() - origin_;
  return Rect(Point(offset.width(), offset.height()), rhs.size());
}

Rect Rect::ApplyInset(const Rect& inset) const {
  Point new_origin = origin_ + inset.origin();
  return Rect(new_origin, inset.size());
}

bool Rect::operator==(const Rect& rhs) const {
  return origin_ == rhs.origin_ && size_ == rhs.size_;
}

bool Rect::operator!=(const Rect& rhs) const { return !(*this == rhs); }

Rect::operator std::string() const {
  std::ostringstream oss;
  oss << *this;
  return oss.str();
}

// -----------------------------------------------------------------------

Rect AspectFitRect(Size content, Size bounds) {
  const double scale =
      std::min(static_cast<double>(bounds.width()) / content.width(),
               static_cast<double>(bounds.height()) / content.height());
  const Size fitted(
      std::max(1, static_cast<int>(std::lround(content.width() * scale))),
      std::max(1, static_cast<int>(std::lround(content.height() * scale))));
  return fitted.CenteredIn(Rect(Point(0, 0), bounds));
}

// -----------------------------------------------------------------------
// Overloaded output operators

std::ostream& operator<<(std::ostream& os, const Point& p) {
  return os << p.DebugString();
}

std::ostream& operator<<(std::ostream& os, const Size& s) {
  return os << s.DebugString();
}

std::ostream& operator<<(std::ostream& os, const Rect& r) {
  return os << r.DebugString();
}
