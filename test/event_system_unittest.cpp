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

#include <gtest/gtest.h>

#include "core/event_listener.hpp"
#include "systems/event_backend.hpp"
#include "systems/event_system.hpp"

#include <deque>
#include <memory>
#include <vector>

class QueuedEventBackend final : public IEventBackend {
 public:
  std::deque<Event> queue;
  std::shared_ptr<Event> PollEvent() override {
    if (queue.empty())
      return std::make_shared<Event>(std::monostate{});

    auto event = std::make_shared<Event>(std::move(queue.front()));
    queue.pop_front();
    return event;
  }
};

class RecordingEventListener final : public EventListener {
 public:
  std::vector<Event> received;
  void OnEvent(std::shared_ptr<Event> event) override {
    received.push_back(*event);
  }
};

class EventSystemTest : public ::testing::Test {
 protected:
  QueuedEventBackend* backend = nullptr;
  std::unique_ptr<EventSystem> events;
  std::shared_ptr<RecordingEventListener> listener =
      std::make_shared<RecordingEventListener>();

  void SetUp() override {
    auto owner = std::make_unique<QueuedEventBackend>();
    backend = owner.get();
    events = std::make_unique<EventSystem>(std::move(owner));
    events->AddListener(listener);
  }
};

TEST_F(EventSystemTest, HostPumpDefersGameplayInput) {
  // Keyboard and pointer input arrives while script execution is active;
  // listeners must see it only when normal logical-frame dispatch resumes.
  backend->queue = {KeyDown{KeyCode::RETURN}, MouseDown{MouseButton::LEFT}};

  events->PumpHostEvents();
  EXPECT_TRUE(listener->received.empty());
  events->ExecuteEventSystem();

  ASSERT_EQ(listener->received.size(), 2u);
  EXPECT_TRUE(std::holds_alternative<KeyDown>(listener->received[0]));
  EXPECT_TRUE(std::holds_alternative<MouseDown>(listener->received[1]));
}

TEST_F(EventSystemTest, HostPumpPreservesWindowAndInputOrdering) {
  // Pointer events surround a resize across two pumps; normal dispatch must
  // preserve their order relative to both window changes and newer input.
  backend->queue = {MouseMotion{Point(1, 2)}, VideoResize{Size(30, 40)}};
  events->PumpHostEvents();
  backend->queue = {MouseMotion{Point(5, 6)}, Active{true}};
  events->PumpHostEvents();
  EXPECT_FALSE(events->mouse_inside_window());
  backend->queue = {KeyUp{KeyCode::RETURN}};

  events->ExecuteEventSystem();

  ASSERT_EQ(listener->received.size(), 5u);
  EXPECT_EQ(std::get<MouseMotion>(listener->received[0]).pos, Point(1, 2));
  EXPECT_EQ(std::get<VideoResize>(listener->received[1]).size, Size(30, 40));
  EXPECT_EQ(std::get<MouseMotion>(listener->received[2]).pos, Point(5, 6));
  EXPECT_TRUE(std::holds_alternative<Active>(listener->received[3]));
  EXPECT_TRUE(std::holds_alternative<KeyUp>(listener->received[4]));
  EXPECT_TRUE(events->mouse_inside_window());
}

TEST_F(EventSystemTest, DeferredEventsAreDeliveredOnce) {
  // Input survives idle host pumps and multiple normal dispatches; it must
  // produce exactly one listener notification.
  backend->queue = {MouseUp{MouseButton::LEFT}};
  events->PumpHostEvents();
  events->PumpHostEvents();

  events->ExecuteEventSystem();
  events->ExecuteEventSystem();

  EXPECT_EQ(listener->received.size(), 1u);
}

TEST_F(EventSystemTest, DiscardingDeferredInputKeepsWindowEvents) {
  // A local reset discards queued gameplay input while retaining the resize
  // and focus state needed by the next logical frame.
  backend->queue = {KeyDown{KeyCode::RETURN},   VideoResize{Size(30, 40)},
                    MouseMotion{Point(1, 2)},   MouseDown{MouseButton::LEFT},
                    MouseUp{MouseButton::LEFT}, Active{true},
                    KeyUp{KeyCode::RETURN}};
  events->PumpHostEvents();

  events->DiscardDeferredInput();
  events->ExecuteEventSystem();

  ASSERT_EQ(listener->received.size(), 2u);
  EXPECT_TRUE(std::holds_alternative<VideoResize>(listener->received[0]));
  EXPECT_TRUE(std::holds_alternative<Active>(listener->received[1]));
}

TEST_F(EventSystemTest, HostPumpDeliversQuitImmediately) {
  // Quit follows queued input during script execution; it must be delivered
  // immediately and the deferred gameplay input must be discarded.
  backend->queue = {KeyDown{KeyCode::RETURN}, Quit{}};

  events->PumpHostEvents();

  ASSERT_EQ(listener->received.size(), 1u);
  EXPECT_TRUE(std::holds_alternative<Quit>(listener->received.front()));
  events->ExecuteEventSystem();
  EXPECT_EQ(listener->received.size(), 1u);
}
