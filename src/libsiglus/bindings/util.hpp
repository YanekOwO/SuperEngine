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

#pragma once

#include "core/frame_counter.hpp"
#include "core/interpolation.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace serilang {
class VM;
class Value;
class List;
class Dict;
};  // namespace serilang

namespace libsiglus::binding {

InterpolationMode GetInterpolationMode(int speed_type);
FrameCounter MakeFrameCounter(int duration,
                              int delay,
                              int start_val,
                              int end_val,
                              int type,
                              std::shared_ptr<Clock> clock);
FrameCounter MakeRepeatingFrameCounter(InterpolationType type,
                                       int duration,
                                       int delay,
                                       int start_value,
                                       int end_value,
                                       int speed_type,
                                       std::shared_ptr<Clock> clock);

// for code injection
serilang::Value Execute(serilang::VM& vm, std::string src);

std::optional<int> AsInt(const serilang::Value& value);
std::string AsString(const serilang::Value& value);
int RequireInt(const serilang::Value& value, std::string_view where);
const serilang::List* RequireList(const serilang::Value& value,
                                  std::string_view where);

struct CallPacket {
  std::optional<int> overload_id;
  std::vector<serilang::Value> args;
  const serilang::Dict* kwargs = nullptr;

  static CallPacket DecodeFrom(std::vector<serilang::Value> raw);
};
std::optional<int> ParseKeywordId(serilang::Value key);
void ForEachKeywordId(
    const serilang::Dict* kwargs,
    const std::function<void(int, const serilang::Value&)>& callback);

}  // namespace libsiglus::binding
