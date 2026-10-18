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
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
//
// -----------------------------------------------------------------------

#pragma once

#include "core/colour.hpp"
#include "core/mwnd_config.hpp"
#include "core/rect.hpp"
#include "core/text_layout.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class GraphicsSystem;
class Point;
class RLMachine;
class SDLSurface;
class System;
class TextSystem;
class ITextSystem;
class TextWaku;
class TextWindowButton;
class TextPage;

struct MwndMessageState {
  bool block_started = false;
  bool clear_ready = false;
  int auto_mode_base_chars = 0;
  int current_koe = -1;
  int current_character = -1;
  bool current_koe_played = false;
  bool current_koe_no_auto_mode = false;
  std::string waku_file;
  std::string filter_file;
  std::optional<std::string> pending_ruby;

  void ClearKoe();
  void MarkNovelClear(TextPage& page);
  void MarkMessageClearReady(TextPage& page);
};

constexpr int kNumFaceSlots = MwndConfig::kNumFaceSlots;

// Sets of TextWindows should be reconstructable by the state in TextPage,
// though there are some notable exceptions, specifically Select_LongOperation.
//
// This class has all sorts of complex, rarely used text rendering options,
// including multiple co-ordinate systems, which I'm sure was done to give
// reverse engineers a headache.
class TextWindow {
 public:
  using FaceSlotConfig = MwndConfig::FaceSlot;
  using NameboxConfig = MwndConfig::Namebox;
  using InitParams = MwndConfig::Window;

  TextWindow(System& system,
             int window_num,
             ITextSystem* text_impl,
             const InitParams& params);
  ~TextWindow();

  std::shared_ptr<TextWindow> Clone() const;
  TextPage& GetPage();
  bool HasPage() const { return static_cast<bool>(page_); }
  void NewPage();
  const std::shared_ptr<MwndMessageState>& message_state() const {
    return message_state_;
  }
  std::pair<int, int> sorter() const { return sorter_; }
  void set_layer(int layer) { sorter_.second = layer; }
  void ClearMouseCapture();
  std::size_t capture_generation() const { return capture_generation_; }

  inline TextLayout& Layout() { return layout_; }
  inline TextLayout const& Layout() const { return layout_; }

  void Execute(bool update_controls = true);
  void ShowWaitIcon(bool page);
  void HideWaitIcon();
  void SetWakuMainFile(const std::string& file);
  void SetWakuFilterFile(const std::string& file);

  int window_number() const { return window_num_; }

  // TODO(erg): This is nowhere near good enough and handling waku better needs
  // to be put on the long term goal list. Should waku_set() live on the
  // TextWindow? O RLY?
  int waku_set() const { return waku_set_; }
  void SetTextboxWaku(int waku_set, std::unique_ptr<TextWaku> waku);
  void SetNameboxWaku(int waku_set, std::unique_ptr<TextWaku> waku);
  TextWaku* GetTextboxWaku() const { return textbox_waku_.get(); }
  TextWaku* GetNameboxWaku() const { return namebox_waku_.get(); }
  bool IsTextboxWakuVisible() const;
  bool IsNameboxWakuVisible() const;

  // Sets the size of the font. Represented by #WINDOW.xxx.MOJI.SIZE.
  void set_font_size_to_default() {
    set_font_size_in_pixels(default_font_size_);
  }
  void set_font_size_in_pixels(int i) { layout_.font_size = i; }
  int font_size_in_pixels() const { return layout_.font_size; }

  void SetTextboxPadding(const std::vector<int>& pos_data);

  void set_use_indentation(const int i) { use_indentation_ = i; }
  int use_indentation() const { return use_indentation_; }

  void SetIndentation();
  int current_indentation() const { return layout_.indent_pixels; }

  void SetDefaultTextColor(const std::vector<int>& colour_data);
  void SetFontColor(const std::vector<int>& colour_data);
  inline void SetDefaultTextColor(RGBColour rgb) { default_colour_ = rgb; }
  inline void SetFontColor(RGBColour rgb) { font_colour_ = rgb; }
  RGBColour GetFontColor() const { return font_colour_; }

  void SetWindowPosition(const std::vector<int>& pos_data);

  // We have to differentiate between the official size where we're supposed to
  // put text (GetTextWindowSize()) and the actual size of the surface
  // (GetTextSurfaceSize()) because GetTextSurfaceSize() contains extra space
  // for one squashed kinsoku character.
  Size GetTextWindowSize() const;
  Size GetTextSurfaceSize() const;
  void SetSelectionSize(std::optional<Size> size) { selection_size_ = size; }
  Size GetWakuContentSize() const;

  Rect GetWindowRect() const;
  Rect GetWindowRect(Size waku_size) const;
  Rect GetTextSurfaceRect() const;

  // Locations of the namebox waku (will be a relative calculation to
  // GetWindowRect()) and the name text surface (will be a relative calculation
  // to the waku).
  Rect GetNameboxWakuRect() const;
  Rect GetNameboxTextRect() const;

  // The size of the writable text area.
  Size GetNameboxTextArea() const;

  // TODO(erg): What's SetMousePosition and how does it differ from mouse
  // listeners?
  void SetMousePosition(const Point& pos, bool update_waku = true);
  bool HasMwndButtonAt(const Point& pos) const;
  bool HandleMouseClick(const Point& pos, bool pressed);

  // Sets how the name is displayed
  void SetNameMod(const int in);
  [[nodiscard]] int GetNameMod() const { return static_cast<int>(name_mod_); }
  [[nodiscard]] bool HasName() const { return !current_name_.empty(); }

  // Sets the size of the spacing between characters. Represented by
  // #WINDOW.xxx.NAME_MOJI_REP.
  void SetNameSpacingBetweenCharacters(int pos_data);

  void SetNameboxPadding(const std::vector<int>& pos_data);
  void SetNameboxPosition(const std::vector<int>& pos_data);

  // Set and access the position of the animated icon that appears when waiting
  // for user input. The TextWindow only owns the position which is queried by
  // the TextKeyCursor object which owns the actual image and other
  // properties. This is an artifact of how RealLive handles this.
  void SetKeycursorMod(const std::vector<int>& keycur);
  Point KeycursorPosition(const Size& cursor_size) const;

  // Accessors regarding the background colour of the window.
  //
  // Represents the data parsed from #WINDOW_ATTR,
  // #WINDOW.index.ATTR_MOD, and #WINDOW.index.ATTR
  int GetAttrMod() const { return window_attr_mod_; }

  void SetRGBAF(const std::vector<int>& rgba_values);

  [[nodiscard]] RGBAColour GetColour() const { return colour_; }
  void SetColour(RGBAColour col) { colour_ = col; }
  [[nodiscard]] bool GetIsFilter() const { return is_filter_; }
  void SetFilter(bool f) { is_filter_ = f; }

  void SetVisible(bool in) {
    if (!in)
      ClearMouseCapture();
    is_visible_ = in;
  }
  bool IsVisible() const { return is_visible_; }

  void set_action_on_pause(const int i) { action_on_pause_ = i; }
  bool action_on_pause() const { return action_on_pause_; }

  int insertion_point_x() const { return layout_.insertion_x; }
  int insertion_point_y() const { return layout_.insertion_y; }
  void offset_insertion_point_x(int offset) { layout_.insertion_x += offset; }
  void offset_insertion_point_y(int offset) { layout_.insertion_y += offset; }
  void set_insertion_point_x(int x) { layout_.insertion_x = x; }
  void set_insertion_point_y(int y) { layout_.insertion_y = y; }
  int line_height() const { return layout_.GetLineHeight(); }

  inline void SetGlyphRenderOffset(Point offset) {
    glyph_render_offset_ = offset;
  }
  inline void ResetGlyphRenderOffset() { glyph_render_offset_ = Point(0, 0); }
  inline Point glyph_render_offset() const { return glyph_render_offset_; }

  // Loads |filename| into face slot |index|.
  void FaceOpen(const std::string& filename, int index);

  // Clears face slot |index|.
  void FaceClose(int index);
  void SetFaceSlotPosition(int index, Point position);

  // Marks that the next character rendered in the window should be italic.
  void NextCharIsItalic();

  void Render();

  // Clears the text window of all text and resets the insertion
  // point.
  void ClearWin();

  // Displays one character, and performs line breaking logic based on the next
  // character. Returns true if the character fits on the screen. False if it
  // does not and was not displayed.
  bool DisplayCharacter(const std::string& current, const std::string& rest);

  // Checks to make sure that not only will |cur_codepoint| fit on the line,
  // but also that we'll perform kinsoku rules correctly.
  bool MustLineBreak(int cur_codepoint, const std::string& rest);

  // Returns whether another character can be placed on the screen.
  bool IsFull() const;

  // Sets (and displays, if appropriate) the name of the current speaker.
  void SetName(const std::string& utf8name, const std::string& next_char);
  void SetNameWithoutDisplay(const std::string& utf8name);

  void RenderNameInBox(const std::string& utf8str);

  void KoeMarker(int id);
  void ResetIndentation();
  void MarkRubyBegin();
  void DisplayRubyText(const std::string& utf8str);


 private:
  std::pair<int, int> sorter_;
  std::unique_ptr<TextPage> page_;
  std::shared_ptr<MwndMessageState> message_state_ =
      std::make_shared<MwndMessageState>();
  std::optional<Size> selection_size_;

  // Render faces implementation.
  void RenderFaces(int behind);

  void RenderKoeReplayButtons();

 private:
  // Non-owning pointer to the text implementor
  ITextSystem* text_impl_;

  std::shared_ptr<SDLSurface> text_surface_;
  std::shared_ptr<SDLSurface> name_surface_;

  // We cache the size of the screen so we don't need the machine in
  // some accessors.
  int screen_width_, screen_height_;

  // Our numeric window identifier.
  int window_num_;

  // The waku set number.
  int waku_set_;

  // The window decorations for the text window
  std::unique_ptr<TextWaku> textbox_waku_;

  TextLayout layout_;
  Point glyph_render_offset_;

  // Whether the last token was a SetName. This is used to control indentation
  // for quotes.
  bool last_token_was_name_;
  std::string current_name_;

  // The default font size.
  int default_font_size_;

  // Whether to indent (INDENT_USE)
  int use_indentation_;

  // The default colour. Initialized to #COLOR_TABLE.000, but can be
  // changed with the SetFontColour() command.
  RGBColour default_colour_;

  // The current colour. Initialized to the default colour on every
  // clearWin() call.
  RGBColour font_colour_;

  // Determines how the window will react to pause()
  // calls. Initialized to #WINDOW.x.R_COMMAND_MOD.
  int action_on_pause_;

  int origin_, x_distance_from_origin_, y_distance_from_origin_;

  int upper_box_padding_;
  int lower_box_padding_;
  int left_box_padding_;
  int right_box_padding_;

  // Whether r_, etc is a per-window colour.
  int window_attr_mod_;

  // The default window background colour.
  RGBAColour colour_;
  bool is_filter_;

  bool is_visible_;
  std::size_t capture_generation_ = 0;

  // Determines the position of the keycursor (the animated cursor
  // that appears when the game is waiting for a click to move to the
  // next page of text).
  //
  // If type is 0, the cursor appears at the bottom right corner of
  // the text area; if it is 1, it appears directly after the final
  // character printed; if it is 2, it appears at (x, y) relative to
  // the top left of the text area. (x and y are ignored when type is
  // 0 or 1.)
  int keycursor_type_;
  Point keycursor_pos_;

  enum class NameMode : int { Inline = 0, SeparateWindow = 1, Disable = 2 };
  NameMode name_mod_;

  // Waku set to use for the text box in the case where name_mod_ == 1.
  int name_waku_set_;

  std::unique_ptr<TextWaku> namebox_waku_;

  int name_font_size_in_pixels_;

  int name_waku_dir_set_;

  // Spacing between characters
  int name_x_spacing_;

  int horizontal_namebox_padding_;
  int vertical_namebox_padding_;

  int namebox_x_offset_;
  int namebox_y_offset_;

  int namebox_centering_;

  int minimum_namebox_size_;

  int name_size_;

  std::optional<NameboxConfig::SiglusLayout> siglus_namebox_layout_;

  // Name glyph bounds, excluding the two-pixel shadow added by RenderText.
  Size GetNameboxTextSize() const;

  // The number of characters in the window (or |minimum_namebox_size_|).
  int namebox_characters_;

  bool next_char_italic_;

  struct FaceSlot;
  std::unique_ptr<FaceSlot> face_slot_[kNumFaceSlots];

  // A list of visible koe replay buttons. The Point is the top left corner of
  // the button and int is the corresponding id number.
  std::vector<std::pair<Point, int>> koe_replay_button_;

 public:
  struct KoeReplayInfo {
    std::shared_ptr<const SDLSurface> icon;
    Size repos;
  };
  inline void SetKoeReplayInfo(std::unique_ptr<KoeReplayInfo> in) {
    koe_replay_info_.swap(in);
  }

 private:
  std::unique_ptr<KoeReplayInfo> koe_replay_info_;

  System& system_;
};
