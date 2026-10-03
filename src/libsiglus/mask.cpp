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

#include "libsiglus/mask.hpp"

#include "systems/sdl/sdl_surface.hpp"
#include "utilities/clock.hpp"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace libsiglus {
namespace {

FrameCounter MakeFrameCounter(MaskValue::EventType event_type,
                              int duration,
                              int start,
                              int end,
                              int speed_type,
                              std::shared_ptr<Clock> clock) {
  InterpolationMode mode = InterpolationMode::Linear;
  if (speed_type == 1)
    mode = InterpolationMode::Accelerate;
  else if (speed_type == 2)
    mode = InterpolationMode::Decelerate;

  InterpolationType type = InterpolationType::OneShot;
  if (event_type == MaskValue::EventType::Loop)
    type = InterpolationType::Loop;
  else if (event_type == MaskValue::EventType::Turn)
    type = InterpolationType::Turn;

  return FrameCounter(std::move(clock),
                      Interpolation(Range(start, end), type, mode), duration);
}

}  // namespace

MaskValue::MaskValue(std::shared_ptr<Clock> clock) : clock_(std::move(clock)) {
  if (!clock_)
    clock_ = std::make_shared<Clock>();
}

void MaskValue::Reset() {
  value_ = 0;
  render_value_ = 0;
  frame_counter_.reset();
}

void MaskValue::SetValue(int value) {
  value_ = value;
  if (!frame_counter_)
    render_value_ = value;
}

void MaskValue::SetEvent(int value, int duration, int delay, int speed_type) {
  const int start = value_;
  value_ = value;
  StartEvent(EventType::OneShot, start, value, duration, delay, speed_type);
}

void MaskValue::LoopEvent(int start,
                          int end,
                          int duration,
                          int delay,
                          int speed_type) {
  StartEvent(EventType::Loop, start, end, duration, delay, speed_type);
}

void MaskValue::TurnEvent(int start,
                          int end,
                          int duration,
                          int delay,
                          int speed_type) {
  StartEvent(EventType::Turn, start, end, duration, delay, speed_type);
}

void MaskValue::StartEvent(EventType event_type,
                           int start,
                           int end,
                           int duration,
                           int delay,
                           int speed_type) {
  render_value_ = start;
  frame_counter_ =
      MakeFrameCounter(event_type, duration, start, end, speed_type, clock_);
  frame_counter_->BeginTimer(std::chrono::milliseconds(delay));
}

void MaskValue::EndEvent() {
  frame_counter_.reset();
  render_value_ = value_;
}

void MaskValue::Execute() {
  if (!frame_counter_) {
    render_value_ = value_;
    return;
  }

  render_value_ = static_cast<int>(frame_counter_->ReadFrame());
  if (frame_counter_->IsFinished())
    frame_counter_.reset();
}

MaskElement::MaskElement(std::shared_ptr<Clock> clock)
    : x_(clock), y_(std::move(clock)) {}

void MaskElement::Reset() {
  x_.Reset();
  y_.Reset();
  filename_.clear();
  surface_.reset();
}

void MaskElement::Create(std::string filename,
                         std::shared_ptr<SDLSurface> surface) {
  Reset();
  filename_ = std::move(filename);
  surface_ = std::move(surface);
}

void MaskElement::Execute() {
  x_.Execute();
  y_.Execute();
}

MaskList::MaskList(std::size_t size, std::shared_ptr<Clock> clock) {
  elements_.reserve(size);
  for (std::size_t i = 0; i < size; ++i)
    elements_.emplace_back(clock);
}

MaskElement& MaskList::At(int index) {
  if (index < 0 || static_cast<std::size_t>(index) >= elements_.size())
    throw std::out_of_range("mask index out of range: " +
                            std::to_string(index));
  return elements_[static_cast<std::size_t>(index)];
}

const MaskElement& MaskList::At(int index) const {
  if (index < 0 || static_cast<std::size_t>(index) >= elements_.size())
    throw std::out_of_range("mask index out of range: " +
                            std::to_string(index));
  return elements_[static_cast<std::size_t>(index)];
}

void MaskList::Reset() {
  for (MaskElement& element : elements_)
    element.Reset();
}

void MaskList::Execute() {
  for (MaskElement& element : elements_)
    element.Execute();
}

}  // namespace libsiglus
