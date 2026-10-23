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

#pragma once

#include "systems/itext_system.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// Independently timed streams of already laid-out drawing records.
class MessageStream {
 public:
  struct Glyph {
    std::string text;
    FontFace font;
    RGBColour colour;
    std::optional<RGBColour> shadow;
    Point position;
  };
  struct Ruby {
    std::shared_ptr<SDLSurface> surface;
    Rect destination;
  };
  using Entry = std::variant<Glyph, Ruby>;

  MessageStream();

  void Enqueue(Entry entry);
  void NextStream();
  bool Empty() const;

  template <class Callback>
  bool Advance(std::chrono::milliseconds delta,
               std::chrono::milliseconds delay,
               Callback cb) {
    delta = std::max(delta, std::chrono::milliseconds::zero());
    delay = std::max(delay, std::chrono::milliseconds::zero());
    bool changed = false;
    for (auto& stream : streams_) {
      if (stream.pending.empty())
        continue;
      stream.elapsed += delta;
      while (!stream.pending.empty()) {
        const auto& item = stream.pending.front();
        if (std::holds_alternative<Glyph>(item)) {
          if (stream.elapsed < delay)
            break;
          stream.elapsed -= delay;
        }

        cb(item);
        stream.pending.pop_front();
        changed = true;
      }
    }
    return changed;
  }

 private:
  struct Stream {
    std::deque<Entry> pending;
    std::chrono::milliseconds elapsed{};
  };
  std::vector<Stream> streams_;
};

struct MessageStreamState {
  MessageStream stream;
  bool deferring = false;
  std::optional<int> first_stream_chars;

  static void Draw(ITextSystem& renderer,
                   const std::shared_ptr<SDLSurface>& destination,
                   const MessageStream::Entry& entry);
};
