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

#include "core/message_stream.hpp"

#include "systems/sdl/sdl_surface.hpp"

#include <utility>

MessageStream::MessageStream() : streams_(1) {}

void MessageStream::Enqueue(Entry entry) {
  auto& stream = streams_.back();
  if (stream.pending.empty())
    stream.elapsed = std::chrono::milliseconds::zero();
  stream.pending.emplace_back(std::move(entry));
}

void MessageStream::NextStream() { streams_.emplace_back(); }

bool MessageStream::Empty() const {
  return std::all_of(streams_.begin(), streams_.end(),
                     [](const Stream& s) { return s.pending.empty(); });
}

void MessageStreamState::Draw(ITextSystem& renderer,
                              const std::shared_ptr<SDLSurface>& destination,
                              const MessageStream::Entry& entry) {
  if (const auto* glyph = std::get_if<MessageStream::Glyph>(&entry)) {
    renderer.RenderGlyphOnto(glyph->text, glyph->font, glyph->colour,
                             glyph->shadow, glyph->position, destination);
  } else {
    const auto& ruby = std::get<MessageStream::Ruby>(entry);
    destination->blitFROMSurface(*ruby.surface, ruby.surface->GetRect(),
                                 ruby.destination);
  }
}
