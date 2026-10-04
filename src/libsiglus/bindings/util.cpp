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

#include "libsiglus/bindings/util.hpp"

#include "core/interpolation.hpp"
#include "m6/compiler_pipeline.hpp"
#include "m6/source_buffer.hpp"
#include "vm/exception.hpp"
#include "vm/list.hpp"
#include "vm/string.hpp"
#include "vm/value.hpp"
#include "vm/vm.hpp"

#include <format>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace libsiglus::binding {

InterpolationMode GetInterpolationMode(int speed_type) {
  switch (speed_type) {
    case 1:
      return InterpolationMode::Accelerate;
    case 2:
      return InterpolationMode::Decelerate;
    case 0:
    default:
      return InterpolationMode::Linear;
  }
}

FrameCounter MakeFrameCounter(int duration,
                              int delay,
                              int start_val,
                              int end_val,
                              int speed_type,
                              std::shared_ptr<Clock> clock) {
  FrameCounter fc(
      std::move(clock),
      Interpolation(Range(start_val, end_val), InterpolationType::OneShot,
                    GetInterpolationMode(speed_type)),
      duration);
  fc.BeginTimer(std::chrono::milliseconds(delay));
  return fc;
}

FrameCounter MakeRepeatingFrameCounter(InterpolationType type,
                                       int duration,
                                       int delay,
                                       int start_value,
                                       int end_value,
                                       int speed_type,
                                       std::shared_ptr<Clock> clock) {
  FrameCounter counter(std::move(clock),
                       Interpolation(Range(start_value, end_value), type,
                                     GetInterpolationMode(speed_type)),
                       duration);
  counter.BeginTimer(std::chrono::milliseconds(delay));
  return counter;
}

// ------------------------------------------------------------------------------

namespace sr = serilang;

// for code injection
sr::Value Execute(sr::VM& vm, std::string src) {
  m6::CompilerPipeline pipe(vm.gc_, false);
  auto sb = m6::SourceBuffer::Create(std::move(src), "<siglus bootstrap>");
  pipe.compile(sb);

  if (!pipe.Ok())
    throw std::runtime_error(pipe.FormatErrors());

  auto* chunk = pipe.Get();
  if (!chunk)
    throw std::runtime_error("pipeline returned null chunk\n" + sb->GetStr());

  sr::Value result = vm.Evaluate(chunk);
  return result;
}

std::optional<int> AsInt(const sr::Value& value) {
  if (const int* int_value = value.Get_if<int>())
    return *int_value;
  if (const bool* bool_value = value.Get_if<bool>())
    return *bool_value ? 1 : 0;
  if (const double* double_value = value.Get_if<double>())
    return static_cast<int>(*double_value);
  return std::nullopt;
}

std::string AsString(const sr::Value& value) {
  if (const sr::String* str = value.Get_if<sr::String>())
    return str->str_;
  return value.Str();
}

int RequireInt(sr::Value const& value, std::string_view where) {
  if (auto* i = value.Get_if<int>())
    return *i;
  throw sr::RuntimeError(
      std::format("expected int for {}, got {}", where, value.Desc()));
}

const sr::List* RequireList(sr::Value const& value, std::string_view where) {
  if (auto* list = value.Get_if<sr::List>())
    return list;
  throw sr::RuntimeError(
      std::format("expected list for {}, got {}", where, value.Desc()));
}

CallPacket CallPacket::DecodeFrom(std::vector<sr::Value> raw) {
  if (raw.size() == 3 && raw[1].Get_if<sr::List>() &&
      raw[2].Get_if<sr::Dict>()) {
    const sr::List* args = raw[1].Get_if<sr::List>();
    return CallPacket{.overload_id = AsInt(raw[0]),
                      .args = args->items,
                      .kwargs = raw[2].Get_if<sr::Dict>()};
  }
  return CallPacket{.args = std::move(raw)};
}

std::optional<int> ParseKeywordId(sr::Value key) {
  const sr::String* str = key.Get_if<sr::String>();
  if (!str)
    return std::nullopt;

  std::string_view text = str->str_;
  if (!text.empty() && text.front() == '_')
    text.remove_prefix(1);
  if (text.empty())
    return std::nullopt;

  int result = 0;
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, result);
  if (ec != std::errc() || ptr != end)
    return std::nullopt;
  return result;
}

void ForEachKeywordId(
    const sr::Dict* kwargs,
    const std::function<void(int, const sr::Value&)>& callback) {
  if (!kwargs || !callback)
    return;

  for (const auto& [key, value] : kwargs->map) {
    const std::optional<int> id = ParseKeywordId(key);
    if (id)
      callback(*id, value);
  }
}

}  // namespace libsiglus::binding
