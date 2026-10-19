// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
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
//
// -----------------------------------------------------------------------

#include "systems/event_system.hpp"

#include "core/event_listener.hpp"
#include "systems/event_backend.hpp"

// -----------------------------------------------------------------------
// EventSystem
// -----------------------------------------------------------------------
EventSystem::EventSystem(std::unique_ptr<IEventBackend> backend)
    : clock_(std::make_shared<Clock>()), event_backend_(std::move(backend)) {}

EventSystem::~EventSystem() = default;

void EventSystem::AddListener(int priority,
                              std::weak_ptr<EventListener> listener) {
  listeners_[listener] = priority;
}
void EventSystem::AddListener(std::weak_ptr<EventListener> listener) {
  constexpr auto default_priority = 0;
  AddListener(default_priority, listener);
}
void EventSystem::RemoveListener(std::weak_ptr<EventListener> listener) {
  listeners_.erase(listener);
}

unsigned int EventSystem::GetTicks() const {
  return static_cast<unsigned int>(clock_->GetTicks().count());
}

std::chrono::time_point<std::chrono::steady_clock> EventSystem::GetTime()
    const {
  return clock_->GetTime();
}

std::shared_ptr<Clock> EventSystem::GetClock() const { return clock_; }

void EventSystem::ExecuteEventSystem() {
  std::shared_ptr<Event> event = nullptr;
  do {
    event = event_backend_->PollEvent();
    DispatchEvent(event);
  } while (event != nullptr && !std::holds_alternative<std::monostate>(*event));
}

void EventSystem::DispatchEvent(std::shared_ptr<Event> event) {
  if (!event)
    return;

  if (const auto* active = std::get_if<Active>(event.get()))
    mouse_inside_window_ = active->mouse_inside_window;

  std::vector<std::pair<int, std::shared_ptr<EventListener>>> event_listeners;
  event_listeners.reserve(listeners_.size());

  for (auto it = listeners_.begin(); it != listeners_.end();) {
    if (auto sp = it->first.lock()) {
      event_listeners.emplace_back(std::make_pair(it->second, sp));
      ++it;
    } else
      it = listeners_.erase(it);
  }

  std::sort(event_listeners.begin(), event_listeners.end(), std::greater<>());

  for (auto& [priority, ptr] : event_listeners) {
    if (std::holds_alternative<std::monostate>(*event))
      return;
    ptr->OnEvent(event);
  }
}
