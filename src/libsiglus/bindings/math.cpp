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

#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/util.hpp"
#include "srbind/srbind.hpp"
#include "vm/exception.hpp"
#include "vm/list.hpp"
#include "vm/value.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

namespace libsiglus::binding {

namespace sb = srbind;
namespace sr = serilang;

namespace {

constexpr int kAngleUnit = 10;
constexpr double kPi = std::numbers::pi;

int RoundHalfAwayFromZero(double value) {
  if (value > 0.0)
    return static_cast<int>(value + 0.5);
  if (value < 0.0)
    return static_cast<int>(value - 0.5);
  return 0;
}

int ListInt(const sr::List& list, std::size_t index, std::string_view where) {
  if (index >= list.items.size()) {
    throw sr::RuntimeError(
        std::format("{} expected at least {} item(s)", where, index + 1));
  }
  return RequireInt(list.items[index], where);
}

std::string TostrImpl(int value, int length, char fill) {
  const bool negative = value < 0;
  const long long magnitude =
      negative ? -static_cast<long long>(value) : static_cast<long long>(value);
  const std::string digits = std::to_string(magnitude);

  std::string result;
  if (negative)
    result.push_back('-');

  const int pad_count =
      length - static_cast<int>(digits.size()) - (negative ? 1 : 0);
  if (pad_count > 0)
    result.append(static_cast<std::size_t>(pad_count), fill);
  result += digits;
  return result;
}

int VarargInt(const std::vector<sr::Value>& args,
              std::size_t index,
              std::string_view where) {
  if (index >= args.size()) {
    throw sr::RuntimeError(
        std::format("{} expected at least {} argument(s)", where, index + 1));
  }
  return RequireInt(args[index], where);
}

void RequireArgCount(const std::vector<sr::Value>& args,
                     std::size_t min,
                     std::size_t max,
                     std::string_view where) {
  if (args.size() < min || args.size() > max) {
    throw sr::RuntimeError(
        std::format("{} expected {}{} argument(s), got {}", where, min,
                    min == max ? "" : std::format("-{}", max), args.size()));
  }
}

void AppendUtf8(std::string& out, int codepoint) {
  if (codepoint <= 0x7f) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else if (codepoint <= 0xffff) {
    out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else {
    out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  }
}

bool IsValidUnicodeScalar(int codepoint) {
  return codepoint >= 0 && codepoint <= 0x10ffff &&
         (codepoint < 0xd800 || codepoint > 0xdfff);
}

std::string FullWidthAscii(std::string input) {
  std::string result;
  for (const unsigned char ch : input) {
    if (ch == ' ') {
      AppendUtf8(result, 0x3000);
    } else if (ch >= 0x21 && ch <= 0x7e) {
      AppendUtf8(result, 0xff01 + (ch - 0x21));
    } else {
      result.push_back(static_cast<char>(ch));
    }
  }
  return result;
}

}  // namespace

void BindMath(SiglusMachine& runtime) {
  sr::VM& vm = *runtime.vm;
  sb::module_ m(vm, "math");

  m.def("max", +[](int lhs, int rhs) { return std::max(lhs, rhs); });
  m.def("min", +[](int lhs, int rhs) { return std::min(lhs, rhs); });
  m.def("limit", [](int value, int lhs, int rhs) {
    const auto [lo, hi] = std::minmax(lhs, rhs);
    return std::clamp(value, lo, hi);
  });
  m.def("abs", +[](int value) { return std::abs(value); });
  m.def("rand", [](int lhs, int rhs) {
    const auto [lo, hi] = std::minmax(lhs, rhs);
    return std::rand() % (hi - lo + 1) + lo;
  });
  m.def("sqrt", [](int value, int scale) {
    value = std::max(value, 0);
    return static_cast<int>(std::sqrt(static_cast<double>(value)) * scale);
  });
  m.def("log", [](int value, int scale) {
    value = std::max(value, 1);
    return static_cast<int>(std::log(static_cast<double>(value)) * scale);
  });
  m.def("log2", [](int value, int scale) {
    value = std::max(value, 1);
    return static_cast<int>(std::log(static_cast<double>(value)) /
                            std::log(2.0f) * scale);
  });
  m.def("log10", [](int value, int scale) {
    value = std::max(value, 1);
    return static_cast<int>(std::log10(static_cast<double>(value)) * scale);
  });
  m.def("sin", [](int angle, int scale) {
    return static_cast<int>(
        std::sin((static_cast<double>(angle) * kPi / 180.0) / kAngleUnit) *
        scale);
  });
  m.def("cos", [](int angle, int scale) {
    return static_cast<int>(
        std::cos((static_cast<double>(angle) * kPi / 180.0) / kAngleUnit) *
        scale);
  });
  m.def("tan", [](int angle, int scale) {
    return static_cast<int>(
        std::tan((static_cast<double>(angle) * kPi / 180.0) / kAngleUnit) *
        scale);
  });
  m.def("asin", [](int value, int scale) {
    if (scale == 0)
      return 0;
    const double ratio =
        std::clamp(static_cast<double>(value) / scale, -1.0, 1.0);
    return RoundHalfAwayFromZero((std::asin(ratio) * 180.0 / kPi) * kAngleUnit);
  });
  m.def("acos", [](int value, int scale) {
    if (scale == 0)
      return 0;
    const double ratio =
        std::clamp(static_cast<double>(value) / scale, -1.0, 1.0);
    return RoundHalfAwayFromZero((std::acos(ratio) * 180.0 / kPi) * kAngleUnit);
  });
  m.def("atan", [](int value, int scale) {
    if (scale == 0)
      return 0;
    return RoundHalfAwayFromZero(
        (std::atan(static_cast<double>(value) / scale) * 180.0 / kPi) *
        kAngleUnit);
  });
  m.def("distance", [](int x1, int y1, int x2, int y2) {
    const double dx = static_cast<double>(x2) - x1;
    const double dy = static_cast<double>(y2) - y1;
    return static_cast<int>(std::sqrt(dx * dx + dy * dy));
  });
  m.def("angle", [](int x1, int y1, int x2, int y2) {
    int angle =
        RoundHalfAwayFromZero((std::atan2(static_cast<double>(y2) - y1,
                                          static_cast<double>(x2) - x1) *
                               180.0 / kPi) *
                              kAngleUnit);
    angle = (angle + 360 * kAngleUnit) % (360 * kAngleUnit);
    return angle;
  });
  m.def("linear", [](int x0, int x1, int y1, int x2, int y2) {
    if (x1 == x2)
      return y1;
    return static_cast<int>(
        (static_cast<double>(y2 - y1) * (x0 - x1)) / (x2 - x1) + y1);
  });
  m.def("timetable", [](int now_time, int rep_time, int start_value_in,
                        std::vector<sr::Value> segments) {
    double now = static_cast<double>(now_time - rep_time);
    double start_value = static_cast<double>(start_value_in);
    double ret_value = start_value;

    for (const sr::Value& segment : segments) {
      const sr::List* list = RequireList(segment, "math.timetable segment");
      const double start_time =
          static_cast<double>(ListInt(*list, 0, "math.timetable start_time"));
      const double end_time =
          static_cast<double>(ListInt(*list, 1, "math.timetable end_time"));
      const double end_value =
          static_cast<double>(ListInt(*list, 2, "math.timetable end_value"));
      const int speed_type =
          list->items.size() >= 4
              ? ListInt(*list, 3, "math.timetable speed_type")
              : 0;

      if (now < start_time) {
        ret_value = start_value;
        break;
      }
      if (now >= end_time) {
        ret_value = end_value;
        start_value = end_value;
        continue;
      }

      const double elapsed = now - start_time;
      const double duration = end_time - start_time;
      if (speed_type == 0) {
        ret_value =
            (end_value - start_value) * elapsed / duration + start_value;
      } else if (speed_type == 1) {
        ret_value = (end_value - start_value) * elapsed * elapsed / duration /
                        duration +
                    start_value;
      } else if (speed_type == 2) {
        const double remaining = now - end_time;
        ret_value = -(end_value - start_value) * remaining * remaining /
                        duration / duration +
                    end_value;
      }
      break;
    }

    return RoundHalfAwayFromZero(ret_value);
  });
  m.def("tostr", [](std::vector<sr::Value> args) {
    RequireArgCount(args, 1, 2, "math.tostr");
    const int value = VarargInt(args, 0, "math.tostr value");
    if (args.size() == 1)
      return std::to_string(value);
    return TostrImpl(value, VarargInt(args, 1, "math.tostr length"), ' ');
  });
  m.def("tostr_zero", [](std::vector<sr::Value> args) {
    RequireArgCount(args, 2, 2, "math.tostr_zero");
    return TostrImpl(VarargInt(args, 0, "math.tostr_zero value"),
                     VarargInt(args, 1, "math.tostr_zero length"), '0');
  });
  m.def("tostr_zen", [](std::vector<sr::Value> args) {
    RequireArgCount(args, 1, 2, "math.tostr");
    const int value = VarargInt(args, 0, "math.tostr value");
    if (args.size() == 1)
      return FullWidthAscii(std::to_string(value));
    return FullWidthAscii(
        TostrImpl(value, VarargInt(args, 1, "math.tostr length"), ' '));
  });
  m.def("tostr_zen_zero", [](std::vector<sr::Value> args) {
    RequireArgCount(args, 2, 2, "math.tostr_zero");
    return FullWidthAscii(
        TostrImpl(VarargInt(args, 0, "math.tostr_zero value"),
                  VarargInt(args, 1, "math.tostr_zero length"), '0'));
  });
  m.def("tostr_code", [](int codepoint) {
    if (!IsValidUnicodeScalar(codepoint))
      return std::string();
    std::string result;
    AppendUtf8(result, codepoint);
    return result;
  });
}

RLVM_REGISTER(SiglusBindingRegistry, "math", BindMath)

}  // namespace libsiglus::binding
