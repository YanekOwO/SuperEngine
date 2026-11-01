// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006 Elliot Glaysher
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
//
// -----------------------------------------------------------------------

#pragma once

#include "core/colour.hpp"
#include "core/rect.hpp"
#include "core/text_layout.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace msgcmd {

struct Text {
  // Keep the complete submission for wrapping lookahead during replay, even
  // when only its prefix fitted on this page.
  std::string text, final_lookahead;
  std::size_t accepted_bytes = std::string_view::npos;
};
struct Name {
  std::string name, next_char;
};
struct NameWithoutDisplay {
  std::string name;
};
struct SetNameMode {
  int mode;
};
struct KoeMarker {
  int id;
};
struct HardBreak {};
struct NextMessage {
  TextLayout layout;
  RGBColour color;
};
struct SetIndentation {};
struct ClearIndentation {};
struct SetFontColor {
  RGBColour color;
};
struct SetDefaultTextColor {
  RGBColour color;
};
struct SetFontSize {
  int size;
};
struct RubyBegin {};
struct RubyText {
  std::string ruby;
};
struct SetInsertion {
  Point point;
};
struct SetGlyphRenderOffset {
  Point offset;
};
struct FaceOpen {
  std::string filename;
  int index;
};
struct FaceClose {
  int index;
};
struct MarkItalic {};

}  // namespace msgcmd

using MessageCommand = std::variant<msgcmd::Text,
                                    msgcmd::Name,
                                    msgcmd::NameWithoutDisplay,
                                    msgcmd::SetNameMode,
                                    msgcmd::KoeMarker,
                                    msgcmd::HardBreak,
                                    msgcmd::NextMessage,
                                    msgcmd::SetIndentation,
                                    msgcmd::ClearIndentation,
                                    msgcmd::SetFontColor,
                                    msgcmd::SetDefaultTextColor,
                                    msgcmd::SetFontSize,
                                    msgcmd::RubyBegin,
                                    msgcmd::RubyText,
                                    msgcmd::SetInsertion,
                                    msgcmd::SetGlyphRenderOffset,
                                    msgcmd::FaceOpen,
                                    msgcmd::FaceClose,
                                    msgcmd::MarkItalic>;

// Message settings that can carry across page breaks. Decorations, controls,
// loaded resources, and reveal timing belong to the window, not the page.
struct MessagePageState {
  TextLayout layout;
  int default_font_size = 25;
  RGBColour default_color = RGBColour::White();
  RGBColour color = RGBColour::White();
  std::string name;
  bool next_char_italic = false;
  std::vector<msgcmd::FaceOpen> faces;
};

// An owned, copyable description of a message, independent of its renderer.
struct MessagePage {
  MessagePageState initial_state;

  // Number of characters on this page (used in automode)
  int number_of_chars_on_page = 0;

  // Text commands to reconstruct this page.
  std::vector<MessageCommand> history;

  inline bool empty() const { return history.empty(); }
};
