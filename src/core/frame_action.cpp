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

#include "core/frame_action.hpp"

#include <utility>

std::shared_ptr<FrameAction> TouchFrameAction(
    std::shared_ptr<FrameAction>& action) {
  if (!action)
    action = std::make_shared<FrameAction>();
  return action;
}

FrameAction::FrameAction(const FrameAction& source)
    : function(source.function),
      arguments(source.arguments),
      counter(source.counter ? std::make_shared<Counter>(*source.counter)
                             : nullptr),
      end_time(source.end_time) {}

FrameAction& FrameAction::operator=(const FrameAction& source) {
  if (this == &source)
    return *this;
  function = source.function;
  arguments = source.arguments;
  if (source.counter)
    *GetCounter() = *source.counter;
  else if (counter)
    counter->Reset();
  end_time = source.end_time;
  end_action = false;
  return *this;
}

FrameAction::FrameAction(FrameAction&& source) noexcept
    : function(std::exchange(source.function, nullptr)),
      arguments(std::move(source.arguments)),
      counter(std::move(source.counter)),
      end_time(std::exchange(source.end_time, 0)),
      end_action(std::exchange(source.end_action, false)) {
  source.arguments.clear();
}

FrameAction& FrameAction::operator=(FrameAction&& source) noexcept {
  if (this == &source)
    return *this;
  function = std::exchange(source.function, nullptr);
  arguments = std::move(source.arguments);
  source.arguments.clear();
  counter = std::move(source.counter);
  end_time = std::exchange(source.end_time, 0);
  end_action = std::exchange(source.end_action, false);
  return *this;
}

std::shared_ptr<Counter> FrameAction::GetCounter() {
  if (!counter)
    counter = std::make_shared<Counter>();
  return counter;
}

void FrameAction::Reset() {
  function = nullptr;
  arguments.clear();
  end_time = 0;
  end_action = false;
  if (counter)
    counter->Reset();
}
