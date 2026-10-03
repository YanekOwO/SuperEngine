// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2013 Elliot Glaysher
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
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
// -----------------------------------------------------------------------

#pragma once

enum class InterpolationMode {
  Linear = 0,      // f(t) = t
  LogEaseOut = 1,  // f(t) = log(1+t)/2
  LogEaseIn = 2,   // same as LogEaseOut ?
  Accelerate = 3,  // f(t) = t^2
  Decelerate = 4,  // f(t) = 1 - (1-t)^2
};

enum class InterpolationType {
  OneShot,
  Loop,
  Turn,
};

struct Range {
  double start = 0.0;
  double end = 1.0;
};

struct InterpolationRange {
  double start = 0.0;
  double current = 0.0;
  double end = 1.0;
};

// Computes an interpolated fraction of an amount.
// t = (current - start) / (end - start)
// The value is clamped so that t \in [0,1]
double Interpolate(const InterpolationRange& range,
                   double amount,
                   InterpolationMode mode);

// Interpolates between two scalar values.
// result = value.start + f(t)*(value.end - value.start)
// The resulting interpolation factor is applied to the interval [value.start,
// value.end]
double InterpolateBetween(const InterpolationRange& time,
                          const Range& value,
                          InterpolationMode mode);

class Interpolation {
 public:
  explicit Interpolation(Range range,
                         InterpolationType type = InterpolationType::OneShot,
                         InterpolationMode mode = InterpolationMode::Linear)
      : range_(range), type_(type), mode_(mode) {}

  double ValueAt(double normalized_time) const;

  inline bool IsOneShot() const { return type_ == InterpolationType::OneShot; }
  inline bool IsConstant() const { return range_.start == range_.end; }
  inline double StartValue() const { return range_.start; }
  inline double EndValue() const { return range_.end; }

 private:
  Range range_;
  InterpolationType type_;
  InterpolationMode mode_;
};
