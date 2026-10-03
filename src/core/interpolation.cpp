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

#include "core/interpolation.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

double Interpolate(const InterpolationRange& range,
                   double amount,
                   InterpolationMode mode) {
  double cur = std::clamp(range.current, range.start, range.end);
  double percentage = (cur - range.start) / (range.end - range.start);
  static const double logBase = std::log(2.0);  // Cached log(2)

  switch (mode) {
    case InterpolationMode::Linear:
      return percentage * amount;

    case InterpolationMode::LogEaseOut: {
      // Eases out using logarithmic scaling
      double logPercentage = std::log(percentage + 1.0) / logBase;
      return logPercentage * amount;
    }

    case InterpolationMode::LogEaseIn: {
      // Eases in using inverse logarithmic scaling
      double logPercentage = std::log(percentage + 1.0) / logBase;
      return amount - (1.0 - logPercentage) * amount;
    }

    case InterpolationMode::Accelerate:
      return percentage * percentage * amount;

    case InterpolationMode::Decelerate: {
      double inversePercentage = 1.0 - percentage;
      return (1.0 - inversePercentage * inversePercentage) * amount;
    }

    default:
      throw std::invalid_argument("Invalid interpolation mode: " +
                                  std::to_string(static_cast<int>(mode)));
  }
}

double InterpolateBetween(const InterpolationRange& range,
                          const Range& value,
                          InterpolationMode mode) {
  double to_add = value.end - value.start;
  return value.start + Interpolate(range, to_add, mode);
}

double Interpolation::ValueAt(double normalized_time) const {
  double percentage = normalized_time;

  switch (type_) {
    case InterpolationType::OneShot:
      break;

    case InterpolationType::Loop: {
      if (percentage <= 0.0) {
        percentage = 0.0;
      } else {
        percentage -= std::floor(percentage);
      }
    } break;

    case InterpolationType::Turn: {
      if (percentage <= 0.0) {
        percentage = 0.0;
      } else {
        double cycle = std::fmod(percentage, 2.0);
        percentage = 1.0 - std::fabs(1.0 - cycle);
      }
    } break;

    default:
      throw std::invalid_argument("Invalid interpolation type: " +
                                  std::to_string(static_cast<int>(type_)));
  }

  return InterpolateBetween(InterpolationRange(0.0, percentage, 1.0), range_,
                            mode_);
}
