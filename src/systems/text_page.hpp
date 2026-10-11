// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
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

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

class TextWindow;
class Gameexe;
class Point;
class TextPageElement;
class SetWindowTextPageElement;
class System;
class TextTextPageElement;

// A sequence of replayable commands that write to or modify a window, such as
// displaying characters and changing font information.
//
// The majority of public methods in TextPage simply call the private versions
// of these methods, and add the appropriate TextPageElement to this page's
// back log for replay.
class TextPage {
 public:
  TextPage(Gameexe& gexe, std::shared_ptr<TextWindow> window);
  // Live pages belong to their window and must not retain it.
  TextPage(Gameexe& gexe, TextWindow& window);
  ~TextPage();

  TextWindow* window() const { return text_window_; }
  TextPage CloneFor(TextWindow& window,
                    std::shared_ptr<TextWindow> owner = {}) const;

  // Returns the number of characters printed with Character() and Name().
  int number_of_chars_on_page() const { return number_of_chars_on_page_; }

  // Queries to see if there has been an invocation of
  // MarkRubyBegin(), but not the closing DisplayRubyText().
  bool in_ruby_gloss() const { return in_ruby_gloss_; }

  bool empty() const { return replay_commands_.empty(); }

  // Replays every recordable action called on this TextPage.
  void Replay(bool is_active_page);

  // Add this character to the most recent text render operation on
  // this page's backlog, and then render it, minding the kinsoku
  // spacing rules.
  bool Character(const std::string& current, const std::string& rest);

  // Displays a name. This function will be called by the
  // TextoutLongOperation.
  void Name(const std::string& name, const std::string& next_char);

  // Puts a koe marker in the TextPage. This is only displayed during
  // scrollback.
  void KoeMarker(int id);

  // Forces a hard line brake.
  void HardBrake();

  // Sets the indentation to the x part of the current insertion point.
  void SetIndentation();

  // Resets the indentation.
  void ResetIndentation();

  // Sets the text foreground to the colour passed in, up until the
  // next pause().
  void FontColour(const int colour);

  // Changes the size of text.
  void DefaultFontSize();
  void FontSize(const int size);

  // Marks the current character as the beginning of a phrase that has
  // rubytext over it.
  void MarkRubyBegin();

  // Display the incoming phrase as the rubytext for the text since
  // MarkRubyBegin() was called.
  void DisplayRubyText(const std::string& utf8str);

  void SetInsertionPointX(int x);
  void SetInsertionPointY(int y);
  void Offset_insertion_point_x(int offset);
  void Offset_insertion_point_y(int offset);
  void SetGlyphRenderOffset(Point offset);
  void ResetGlyphRenderOffset();

  // Sets the face in slot |index| to filename.
  void FaceOpen(const std::string& filename, int index);

  // Removes the face in slot |index|.
  void FaceClose(int index);

  // Mark that the next character will be printed in italics (rlBabel
  // extension).
  void NextCharIsItalic();

  // Queries the corresponding TextWindow to see if it is full. Used
  // to implement implicit pauses when a page is full.
  bool IsFull() const;

 private:
  using Command = std::function<void(TextPage&, bool)>;

  // Executes |command| and then adds it to |elements_to_replay_|.
  void AddAction(Command command);

  // Performs textout.
  bool CharacterImpl(const std::string& c, const std::string& rest);

  Gameexe& gexe;

  // Current window that this page is rendering into
  TextWindow* text_window_;
  // Standalone pages and backlog snapshots keep their windows alive.
  std::shared_ptr<TextWindow> window_owner_;

  // Number of characters on this page (used in automode)
  int number_of_chars_on_page_;

  // Whether MarkRubyBegin() was called but DisplayRubyText() hasn't yet been
  // called.
  bool in_ruby_gloss_;

  // A list of the text commands to replay on this page.
  std::vector<Command> replay_commands_;
};
