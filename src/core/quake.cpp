// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
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
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
// -----------------------------------------------------------------------

#include "core/quake.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

std::int64_t SaturatingAdd(std::int64_t lhs, std::int64_t rhs) {
  rhs = std::max<std::int64_t>(0, rhs);
  if (lhs > std::numeric_limits<std::int64_t>::max() - rhs)
    return std::numeric_limits<std::int64_t>::max();
  return lhs + rhs;
}

std::int64_t SaturatingMultiply(int lhs, std::int64_t rhs) {
  const std::int64_t a = std::max(0, lhs);
  const std::int64_t b = std::max<std::int64_t>(0, rhs);
  if (a != 0 && b > std::numeric_limits<std::int64_t>::max() / a)
    return std::numeric_limits<std::int64_t>::max();
  return a * b;
}

int EaseIn(std::int64_t time,
           std::int64_t start_time,
           int start_value,
           std::int64_t end_time,
           int end_value) {
  if (start_time == end_time)
    return end_value;
  const double t = std::clamp<double>(time, std::min(start_time, end_time),
                                      std::max(start_time, end_time));
  const double duration = static_cast<double>(end_time - start_time);
  const double progress = (t - start_time) / duration;
  return static_cast<int>(progress * progress * (end_value - start_value) +
                          start_value);
}

int EaseOut(std::int64_t time,
            std::int64_t start_time,
            int start_value,
            std::int64_t end_time,
            int end_value) {
  if (start_time == end_time)
    return end_value;
  const double t = std::clamp<double>(time, std::min(start_time, end_time),
                                      std::max(start_time, end_time));
  const double duration = static_cast<double>(end_time - start_time);
  const double remaining = (t - end_time) / duration;
  return static_cast<int>(-remaining * remaining * (end_value - start_value) +
                          end_value);
}

double LinearEnvelope(std::int64_t now, std::int64_t begin, std::int64_t end) {
  if (begin == end)
    return now < end ? 1.0 : 0.0;
  if (now <= begin)
    return 1.0;
  if (now >= end)
    return 0.0;
  return static_cast<double>(end - now) / static_cast<double>(end - begin);
}

std::pair<int, int> DirectionSigns(int direction) {
  switch (direction) {
    case 0:
      return {0, -1};
    case 1:
      return {1, -1};
    case 2:
      return {1, 0};
    case 3:
      return {1, 1};
    case 4:
      return {0, 1};
    case 5:
      return {-1, 1};
    case 6:
      return {-1, 0};
    case 7:
      return {-1, -1};
    default:
      return {0, 0};
  }
}

}  // namespace

bool QuakeTransform::IsIdentity() const {
  constexpr double epsilon = 1e-9;
  return std::abs(scale - 1.0) < epsilon && std::abs(translate_x) < epsilon &&
         std::abs(translate_y) < epsilon;
}

void QuakeTransform::Then(const QuakeTransform& next) {
  translate_x = translate_x * next.scale + next.translate_x;
  translate_y = translate_y * next.scale + next.translate_y;
  scale *= next.scale;
}

QuakeState::QuakeState() { Reset(); }

void QuakeState::Reset() {
  type_ = Type::None;
  direction_ = 0;
  power_ = 0;
  period_ms_ = 0;
  count_ = 0;
  end_count_ = 0;
  center_x_ = 0;
  center_y_ = 0;
  begin_order_ = 0;
  end_order_ = 0;
  elapsed_ms_ = 0;
  ending_ = false;
  end_duration_ms_ = 0;
  end_elapsed_ms_ = 0;
  transform_ = {};
}

bool QuakeState::Start(StartParams params) {
  if (params.type < static_cast<int>(Type::Vector) ||
      params.type > static_cast<int>(Type::Zoom)) {
    return false;
  }

  Reset();
  type_ = static_cast<Type>(params.type);
  direction_ = params.direction;
  power_ = params.power;
  period_ms_ = params.period_ms;
  count_ = std::max(0, params.count);
  end_count_ = std::max(0, params.end_count);
  center_x_ = params.center_x;
  center_y_ = params.center_y;
  begin_order_ = params.begin_order;
  end_order_ = params.end_order;
  UpdateTransform();
  return true;
}

void QuakeState::End(int duration_ms) {
  if (!IsActive())
    return;
  if (duration_ms <= 0) {
    Reset();
    return;
  }
  ending_ = true;
  end_duration_ms_ = duration_ms;
  end_elapsed_ms_ = 0;
  UpdateTransform();
}

void QuakeState::Advance(std::int64_t elapsed_ms) {
  if (!IsActive())
    return;

  elapsed_ms_ = SaturatingAdd(elapsed_ms_, elapsed_ms);
  if (ending_)
    end_elapsed_ms_ = SaturatingAdd(end_elapsed_ms_, elapsed_ms);

  if (period_ms_ <= 0) {
    Reset();
    return;
  }

  const bool loop = count_ == 0 && end_count_ == 0;
  const std::int64_t total_duration = SaturatingMultiply(
      period_ms_, static_cast<std::int64_t>(count_) + end_count_);
  if ((!loop && elapsed_ms_ >= total_duration) ||
      (ending_ && end_elapsed_ms_ >= end_duration_ms_)) {
    Reset();
    return;
  }

  UpdateTransform();
}

int QuakeState::Check() const {
  if (!IsActive())
    return 0;
  return ending_ ? 2 : 1;
}

bool QuakeState::Contains(int order) const {
  return IsActive() && begin_order_ <= order && order <= end_order_;
}

QuakeTransform QuakeState::CurrentTransform() const {
  return IsActive() ? transform_ : QuakeTransform{};
}

void QuakeState::UpdateTransform() {
  if (!IsActive() || period_ms_ <= 0) {
    transform_ = {};
    return;
  }

  const std::int64_t period = period_ms_;
  const std::int64_t quarter = period / 4;
  const std::int64_t cycle_time = elapsed_ms_ % period;
  int offset = 0;
  constexpr int scale_unit = 1'000'000;
  int current_scale = scale_unit;

  if (type_ == Type::Vector || type_ == Type::Directional) {
    if (type_ == Type::Vector) {
      if (cycle_time < quarter) {
        offset = EaseIn(cycle_time, 0, 0, quarter, power_ / 2);
      } else if (cycle_time < period / 2) {
        offset = EaseOut(cycle_time - quarter, 0, power_ / 2, quarter, power_);
      } else if (cycle_time < period * 3 / 4) {
        offset =
            EaseIn(cycle_time - period / 2, 0, power_, quarter, power_ / 2);
      } else {
        offset =
            EaseOut(cycle_time - period * 3 / 4, 0, power_ / 2, quarter, 0);
      }
    } else {
      if (cycle_time < quarter) {
        offset = EaseOut(cycle_time, 0, 0, quarter, power_ / 2);
      } else if (cycle_time < period / 2) {
        offset = EaseIn(cycle_time - quarter, 0, power_ / 2, quarter, 0);
      } else if (cycle_time < period * 3 / 4) {
        offset = EaseOut(cycle_time - period / 2, 0, 0, quarter, -power_ / 2);
      } else {
        offset =
            EaseIn(cycle_time - period * 3 / 4, 0, -power_ / 2, quarter, 0);
      }
    }
  } else if (type_ == Type::Zoom) {
    const int power = std::clamp(power_, 0, 255);
    const int maximum = 256 * scale_unit / (256 - power);
    const int halfway = (maximum - scale_unit) / 2 + scale_unit;
    int current = scale_unit;
    if (cycle_time < quarter) {
      current = EaseIn(cycle_time, 0, scale_unit, quarter, halfway);
    } else if (cycle_time < period / 2) {
      current = EaseOut(cycle_time - quarter, 0, halfway, quarter, maximum);
    } else if (cycle_time < period * 3 / 4) {
      current = EaseIn(cycle_time - period / 2, 0, maximum, quarter, halfway);
    } else {
      current =
          EaseOut(cycle_time - period * 3 / 4, 0, halfway, quarter, scale_unit);
    }
    current_scale = current;
  }

  const auto [x_sign, y_sign] = DirectionSigns(direction_);
  int offset_x = offset * x_sign;
  int offset_y = offset * y_sign;
  auto attenuate = [&](double envelope) {
    offset_x = static_cast<int>(offset_x * envelope);
    offset_y = static_cast<int>(offset_y * envelope);
    current_scale =
        static_cast<int>((current_scale - scale_unit) * envelope + scale_unit);
  };

  const bool loop = count_ == 0 && end_count_ == 0;
  if (!loop && elapsed_ms_ >= SaturatingMultiply(period_ms_, count_)) {
    attenuate(LinearEnvelope(
        elapsed_ms_, SaturatingMultiply(period_ms_, count_),
        SaturatingMultiply(period_ms_,
                           static_cast<std::int64_t>(count_) + end_count_)));
  }
  if (ending_)
    attenuate(LinearEnvelope(end_elapsed_ms_, 0, end_duration_ms_));

  const double scale = static_cast<double>(current_scale) / scale_unit;
  transform_ =
      QuakeTransform{.scale = scale,
                     .translate_x = offset_x + center_x_ * (1.0 - scale),
                     .translate_y = offset_y + center_y_ * (1.0 - scale)};
}

QuakeList::QuakeList(std::size_t size) {
  elements_.reserve(size);
  for (std::size_t i = 0; i < size; ++i)
    elements_.emplace_back(std::make_shared<QuakeState>());
}

std::shared_ptr<QuakeState> QuakeList::GetHandle(int index) const {
  if (index < 0 || static_cast<std::size_t>(index) >= elements_.size()) {
    throw std::out_of_range("quake index out of range: " +
                            std::to_string(index));
  }
  return elements_[static_cast<std::size_t>(index)];
}

void QuakeList::Advance(std::int64_t elapsed_ms) {
  for (const auto& quake : elements_)
    quake->Advance(elapsed_ms);
}

void QuakeList::Reset() {
  for (const auto& quake : elements_)
    quake->Reset();
}

bool QuakeList::IsActive() const {
  return std::ranges::any_of(
      elements_, [](const auto& quake) { return quake->IsActive(); });
}

QuakeTransform QuakeList::TransformForOrder(int order) const {
  QuakeTransform result;
  for (const auto& quake : elements_) {
    if (quake->Contains(order))
      result.Then(quake->CurrentTransform());
  }
  return result;
}

QuakeTransform ComposeQuakeTransforms(
    int order,
    const QuakeList* screen_quakes,
    const std::vector<std::shared_ptr<QuakeState>>& stage_quakes) {
  QuakeTransform result;
  if (screen_quakes)
    result.Then(screen_quakes->TransformForOrder(order));
  for (const auto& quake : stage_quakes) {
    if (quake && quake->Contains(order))
      result.Then(quake->CurrentTransform());
  }
  return result;
}
