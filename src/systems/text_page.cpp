// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2007 Elliot Glaysher
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

#include "systems/text_page.hpp"

#include "core/gameexe.hpp"
#include "systems/text_window.hpp"

#include <utf8.h>

#include <string>
#include <utility>

// -----------------------------------------------------------------------
// TextPage
// -----------------------------------------------------------------------

TextPage::TextPage(Gameexe& g, std::shared_ptr<TextWindow> window)
    : TextPage(g, *window) {
  window_owner_ = std::move(window);
}

TextPage::TextPage(Gameexe& g, TextWindow& window)
    : gexe(g),
      text_window_(&window),
      number_of_chars_on_page_(0),
      in_ruby_gloss_(false) {}

TextPage::~TextPage() = default;

TextPage TextPage::CloneFor(TextWindow& window,
                            std::shared_ptr<TextWindow> owner) const {
  TextPage result(*this);
  result.text_window_ = &window;
  result.window_owner_ = std::move(owner);
  return result;
}

void TextPage::Replay(bool is_active_page) {
  // Reset the font color.
  if (!is_active_page) {
    auto colour = gexe("COLOR_TABLE", 254);
    if (auto vec = colour.IntVec()) {
      text_window_->SetFontColor(*vec);
    }
  }

  for (const Command& command : replay_commands_) {
    command(*this, is_active_page);
  }
}

// ------------------------------------------------- [ Public operations ]

bool TextPage::Character(const std::string& current, const std::string& rest) {
  bool rendered = CharacterImpl(current, rest);

  if (rendered) {
    replay_commands_.push_back([current, rest](TextPage& page, bool) {
      page.CharacterImpl(current, rest);
    });
    number_of_chars_on_page_++;
  }

  return rendered;
}

std::size_t TextPage::Text(std::string_view text) {
  if (text.empty())
    return 0;
  auto current = text.begin();
  while (current != text.end()) {
    auto next = current;
    utf8::next(next, text.end());
    if (!Character(std::string(current, next), std::string(next, text.end())))
      break;
    current = next;
  }
  return static_cast<std::size_t>(current - text.begin());
}

void TextPage::Name(const std::string& name, const std::string& next_char) {
  AddAction([name, next_char](TextPage& page, bool) {
    page.text_window_->SetName(name, next_char);
  });
  number_of_chars_on_page_++;
}

void TextPage::KoeMarker(int id) {
  AddAction([id](TextPage& page, bool is_active_page) {
    if (!is_active_page)
      page.text_window_->KoeMarker(id);
  });
}

void TextPage::HardBrake() {
  AddAction(
      [](TextPage& page, bool) { page.text_window_->Layout().HardBreak(); });
}

void TextPage::NextMessage() {
  AddAction([](TextPage& page, bool) { page.text_window_->NextMessage(); });
}

void TextPage::SetIndentation() {
  AddAction([](TextPage& page, bool) { page.text_window_->SetIndentation(); });
}

void TextPage::ResetIndentation() {
  AddAction(
      [](TextPage& page, bool) { page.text_window_->ResetIndentation(); });
}

void TextPage::FontColour(int colour) {
  AddAction([colour](TextPage& page, bool is_active_page) {
    if (is_active_page) {
      page.text_window_->SetFontColor(
          page.gexe("COLOR_TABLE", colour).ToIntVec());
    }
  });
}

void TextPage::DefaultFontSize() {
  AddAction([](TextPage& page, bool) {
    page.text_window_->set_font_size_to_default();
  });
}

void TextPage::FontSize(const int size) {
  AddAction([size](TextPage& page, bool) {
    page.text_window_->set_font_size_in_pixels(size);
  });
}

void TextPage::MarkRubyBegin() {
  AddAction([](TextPage& page, bool) {
    page.text_window_->MarkRubyBegin();
    page.in_ruby_gloss_ = true;
  });
}

void TextPage::DisplayRubyText(const std::string& utf8str) {
  AddAction([utf8str](TextPage& page, bool) {
    page.text_window_->DisplayRubyText(utf8str);
    page.in_ruby_gloss_ = false;
  });
}

void TextPage::SetInsertionPointX(int x) {
  AddAction([x](TextPage& page, bool) {
    page.text_window_->set_insertion_point_x(x);
  });
}

void TextPage::SetInsertionPointY(int y) {
  AddAction([y](TextPage& page, bool) {
    page.text_window_->set_insertion_point_y(y);
  });
}

void TextPage::Offset_insertion_point_x(int offset) {
  AddAction([offset](TextPage& page, bool) {
    page.text_window_->offset_insertion_point_x(offset);
  });
}

void TextPage::Offset_insertion_point_y(int offset) {
  AddAction([offset](TextPage& page, bool) {
    page.text_window_->offset_insertion_point_y(offset);
  });
}

void TextPage::SetGlyphRenderOffset(Point offset) {
  AddAction([offset](TextPage& page, bool) {
    page.text_window_->SetGlyphRenderOffset(offset);
  });
}

void TextPage::ResetGlyphRenderOffset() {
  AddAction([](TextPage& page, bool) {
    page.text_window_->ResetGlyphRenderOffset();
  });
}

void TextPage::FaceOpen(const std::string& filename, int index) {
  AddAction([filename, index](TextPage& page, bool) {
    page.text_window_->FaceOpen(filename, index);
  });
}

void TextPage::FaceClose(int index) {
  AddAction(
      [index](TextPage& page, bool) { page.text_window_->FaceClose(index); });
}

void TextPage::NextCharIsItalic() {
  AddAction(
      [](TextPage& page, bool) { page.text_window_->NextCharIsItalic(); });
}

bool TextPage::IsFull() const { return text_window_->IsFull(); }

void TextPage::AddAction(Command command) {
  command(*this, true);
  replay_commands_.push_back(std::move(command));
}

bool TextPage::CharacterImpl(const std::string& c, const std::string& rest) {
  return text_window_->DisplayCharacter(c, rest);
}
