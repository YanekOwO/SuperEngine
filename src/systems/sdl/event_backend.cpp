// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
// Copyright (C) 2006 Elliot Glaysher
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
//
// -----------------------------------------------------------------------

#include "systems/sdl/event_backend.hpp"

#include "utilities/shutdown_signal.hpp"

#include <SDL3/SDL.h>

KeyCode FromSDLKeycode(uint32_t key) {
  // Keycodes below 0x100 have the same values in SDL3 and in the KeyCode
  // table. This range includes ASCII and Latin-1 (WORLD_0..WORLD_95 at
  // 0xA0..0xFF).
  if (key < 0x100)
    return static_cast<KeyCode>(key);

  switch (key) {
    case SDLK_CLEAR:
      return KeyCode::CLEAR;
    case SDLK_PAUSE:
      return KeyCode::PAUSE;
    case SDLK_KP_0:
      return KeyCode::KP0;
    case SDLK_KP_1:
      return KeyCode::KP1;
    case SDLK_KP_2:
      return KeyCode::KP2;
    case SDLK_KP_3:
      return KeyCode::KP3;
    case SDLK_KP_4:
      return KeyCode::KP4;
    case SDLK_KP_5:
      return KeyCode::KP5;
    case SDLK_KP_6:
      return KeyCode::KP6;
    case SDLK_KP_7:
      return KeyCode::KP7;
    case SDLK_KP_8:
      return KeyCode::KP8;
    case SDLK_KP_9:
      return KeyCode::KP9;
    case SDLK_KP_PERIOD:
      return KeyCode::KP_PERIOD;
    case SDLK_KP_DIVIDE:
      return KeyCode::KP_DIVIDE;
    case SDLK_KP_MULTIPLY:
      return KeyCode::KP_MULTIPLY;
    case SDLK_KP_MINUS:
      return KeyCode::KP_MINUS;
    case SDLK_KP_PLUS:
      return KeyCode::KP_PLUS;
    case SDLK_KP_ENTER:
      return KeyCode::KP_ENTER;
    case SDLK_KP_EQUALS:
      return KeyCode::KP_EQUALS;
    case SDLK_UP:
      return KeyCode::UP;
    case SDLK_DOWN:
      return KeyCode::DOWN;
    case SDLK_RIGHT:
      return KeyCode::RIGHT;
    case SDLK_LEFT:
      return KeyCode::LEFT;
    case SDLK_INSERT:
      return KeyCode::INSERT;
    case SDLK_HOME:
      return KeyCode::HOME;
    case SDLK_END:
      return KeyCode::END;
    case SDLK_PAGEUP:
      return KeyCode::PAGEUP;
    case SDLK_PAGEDOWN:
      return KeyCode::PAGEDOWN;
    case SDLK_F1:
      return KeyCode::F1;
    case SDLK_F2:
      return KeyCode::F2;
    case SDLK_F3:
      return KeyCode::F3;
    case SDLK_F4:
      return KeyCode::F4;
    case SDLK_F5:
      return KeyCode::F5;
    case SDLK_F6:
      return KeyCode::F6;
    case SDLK_F7:
      return KeyCode::F7;
    case SDLK_F8:
      return KeyCode::F8;
    case SDLK_F9:
      return KeyCode::F9;
    case SDLK_F10:
      return KeyCode::F10;
    case SDLK_F11:
      return KeyCode::F11;
    case SDLK_F12:
      return KeyCode::F12;
    case SDLK_F13:
      return KeyCode::F13;
    case SDLK_F14:
      return KeyCode::F14;
    case SDLK_F15:
      return KeyCode::F15;
    case SDLK_NUMLOCKCLEAR:
      return KeyCode::NUMLOCK;
    case SDLK_CAPSLOCK:
      return KeyCode::CAPSLOCK;
    case SDLK_SCROLLLOCK:
      return KeyCode::SCROLLOCK;
    case SDLK_RSHIFT:
      return KeyCode::RSHIFT;
    case SDLK_LSHIFT:
      return KeyCode::LSHIFT;
    case SDLK_RCTRL:
      return KeyCode::RCTRL;
    case SDLK_LCTRL:
      return KeyCode::LCTRL;
    case SDLK_RALT:
      return KeyCode::RALT;
    case SDLK_LALT:
      return KeyCode::LALT;
    case SDLK_LGUI:
      return KeyCode::LSUPER;
    case SDLK_RGUI:
      return KeyCode::RSUPER;
    case SDLK_MODE:
      return KeyCode::MODE;
    case SDLK_HELP:
      return KeyCode::HELP;
    case SDLK_PRINTSCREEN:
      return KeyCode::PRINT;
    case SDLK_SYSREQ:
      return KeyCode::SYSREQ;
    case SDLK_APPLICATION:
    case SDLK_MENU:
      return KeyCode::MENU;
    case SDLK_POWER:
      return KeyCode::POWER;
    case SDLK_UNDO:
      return KeyCode::UNDO;
    default:
      return KeyCode::UNKNOWN;
  }
}

namespace {

inline MouseButton fromSDLButton(Uint8 sdlButton) {
  switch (sdlButton) {
    case SDL_BUTTON_LEFT:
      return MouseButton::LEFT;
    case SDL_BUTTON_RIGHT:
      return MouseButton::RIGHT;
    case SDL_BUTTON_MIDDLE:
      return MouseButton::MIDDLE;
    default:
      return MouseButton::NONE;
  }
}

Event translateSDLToEvent(const SDL_Event& sdlEvent) {
  switch (sdlEvent.type) {
    case SDL_EVENT_QUIT:
      return Quit{};

    // window re-exposed after being covered/minimized
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      return VideoExpose{};

    case SDL_EVENT_WINDOW_RESIZED:
      return VideoResize{
          Size{sdlEvent.window.data1, sdlEvent.window.data2}};

    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      // Assume the mouse is inside the window. Actually checking the mouse
      // state doesn't work in the case where we mouse click on another window
      // that's partially covered by rlvm's window and then alt-tab back.
      return Active{true};

    case SDL_EVENT_WINDOW_MOUSE_ENTER:
      return Active{true};
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
      return Active{false};

    case SDL_EVENT_KEY_DOWN: {
      if (sdlEvent.key.repeat)
        return std::monostate{};
      KeyDown kd;
      kd.code = FromSDLKeycode(sdlEvent.key.key);
      return kd;
    }
    case SDL_EVENT_KEY_UP: {
      KeyUp ku;
      ku.code = FromSDLKeycode(sdlEvent.key.key);
      return ku;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      MouseDown md;
      md.button = fromSDLButton(sdlEvent.button.button);
      return md;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
      MouseUp mu;
      mu.button = fromSDLButton(sdlEvent.button.button);
      return mu;
    }

    case SDL_EVENT_MOUSE_MOTION: {
      MouseMotion mm;
      mm.pos = {static_cast<int>(sdlEvent.motion.x),
                static_cast<int>(sdlEvent.motion.y)};
      return mm;
    }

    // Unhandled event type
    default:
      return std::monostate{};
  }
}

}  // namespace

SDLEventBackend::SDLEventBackend() = default;

std::shared_ptr<Event> SDLEventBackend::PollEvent() {
  if (ConsumeShutdownSignal())
    return std::make_shared<Event>(Quit{});

  if (!pending_.empty()) {
    auto event = std::make_shared<Event>(pending_.front());
    pending_.pop_front();
    return event;
  }

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
      // SDL gives wheel.y in the scroll direction that the user configured.
      // The value can be a fraction of one tick. The code collects the
      // deltas and makes one press pair for each full tick.
      wheel_accum_y_ += event.wheel.y;
      while (wheel_accum_y_ >= 1.0f) {
        pending_.push_back(MouseDown{MouseButton::WHEELUP});
        pending_.push_back(MouseUp{MouseButton::WHEELUP});
        wheel_accum_y_ -= 1.0f;
      }
      while (wheel_accum_y_ <= -1.0f) {
        pending_.push_back(MouseDown{MouseButton::WHEELDOWN});
        pending_.push_back(MouseUp{MouseButton::WHEELDOWN});
        wheel_accum_y_ += 1.0f;
      }
      if (!pending_.empty()) {
        auto out = std::make_shared<Event>(pending_.front());
        pending_.pop_front();
        return out;
      }
      continue;
    }

    Event translated = translateSDLToEvent(event);
    if (std::holds_alternative<std::monostate>(translated))
      continue;  // The event is filtered or unknown. The loop polls again.
    return std::make_shared<Event>(translated);
  }
  return std::make_shared<Event>(std::monostate());
}
