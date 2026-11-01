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
#include "core/message_page.hpp"
#include "core/mwnd_config.hpp"
#include "core/rect.hpp"
#include "core/text_layout.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class Point;
class RLMachine;
class SDLSurface;
class System;
class TextSystem;
class ITextSystem;
struct IFont;
class TextWaku;
class TextWindowButton;
class TextWindow;
struct MessageStreamState;
struct LocalConfig;

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
  void MarkNovelClear(TextWindow& window);
  void MarkMessageClearReady(TextWindow& window, LocalConfig& config);
};

constexpr int kNumFaceSlots = MwndConfig::kNumFaceSlots;

// Message content can be reconstructed from a MessagePage,
// though there are some notable exceptions, specifically Select_LongOperation.
//
// This class has all sorts of complex, rarely used text rendering options,
// including multiple co-ordinate systems, which I'm sure was done to give
// reverse engineers a headache.
class TextWindow {
 public:
  using FaceSlotConfig = MwndConfig::FaceSlot;
  using TextboxConfig = MwndConfig::Textbox;
  using NameboxConfig = MwndConfig::Namebox;
  using InitParams = MwndConfig::Window;

  TextWindow(System& system,
             int window_num,
             ITextSystem* text_impl,
             const InitParams& params,
             bool history_view = false);
  ~TextWindow();

  std::shared_ptr<TextWindow> Clone() const;
  const MessagePage& GetPage();
  inline bool HasPage() const { return page_.has_value(); }
  void NewPage();
  void Restore(const MessagePage& page,
               std::optional<RGBColour> initial_color = std::nullopt);

  // [Recorded in MessagePage] ------------------------------------------
  // Apply and record a non-text command. Submit text through SubmitText.
  bool Submit(MessageCommand command);
  // Lay out the complete submission before queuing drawing records. Returns
  // the accepted UTF-8 byte count; a rejected suffix can be retried later.
  std::size_t SubmitText(msgcmd::Text command);

  const std::shared_ptr<MwndMessageState>& message_state() const {
    return message_state_;
  }
  std::pair<int, int> sorter() const { return sorter_; }
  void set_layer(int layer) { sorter_.second = layer; }
  void ClearMouseCapture();
  std::size_t capture_generation() const { return capture_generation_; }

  TextLayout& Layout() { return layout_; }
  const TextLayout& Layout() const { return layout_; }

  // Configuration edits affect subsequent geometry/rendering work.
  // Frames are set through SetTextboxWaku.
  inline TextboxConfig& GetTextboxConfig() { return textbox_config_; }
  inline const TextboxConfig& GetTextboxConfig() const {
    return textbox_config_;
  }
  inline void SetTextboxConfig(TextboxConfig config) {
    textbox_config_ = std::move(config);
  }

  // Configuration edits take effect in subsequent geometry/rendering work.
  // Names are rasterized on submission; frames are set through SetNameboxWaku.
  inline NameboxConfig& GetNameboxConfig() { return namebox_config_; }
  inline const NameboxConfig& GetNameboxConfig() const {
    return namebox_config_;
  }
  inline void SetNameboxConfig(NameboxConfig config) {
    namebox_config_ = std::move(config);
  }

  void Execute(bool update_controls = true);
  void ShowWaitIcon(bool page);
  void HideWaitIcon();
  void SetWakuMainFile(const std::string& file);
  void SetWakuFilterFile(const std::string& file);

  int window_number() const { return window_num_; }

  void SetTextboxWaku(int waku_set, std::unique_ptr<TextWaku> waku);
  void SetNameboxWaku(int waku_set, std::unique_ptr<TextWaku> waku);
  inline TextWaku* GetTextboxWaku() const { return textbox_waku_.get(); }
  inline TextWaku* GetNameboxWaku() const { return namebox_waku_.get(); }
  bool IsTextboxWakuVisible() const;
  bool IsNameboxWakuVisible() const;

  int GetDefaultFontSize() const { return default_font_size_; }

  inline void set_use_indentation(const int i) { use_indentation_ = i; }
  inline int use_indentation() const { return use_indentation_; }

  inline RGBColour GetDefaultTextColor() const { return default_colour_; }
  inline RGBColour GetFontColor() const { return font_colour_; }

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

  [[nodiscard]] bool HasName() const { return !current_name_.empty(); }

  // Set and access the position of the animated icon that appears when waiting
  // for user input. The TextWindow only owns the position which is queried by
  // the TextKeyCursor object which owns the actual image and other
  // properties. This is an artifact of how RealLive handles this.
  Point KeycursorPosition(const Size& cursor_size) const;

  void SetVisible(bool in) {
    if (!in)
      ClearMouseCapture();
    is_visible_ = in;
  }
  bool IsVisible() const { return is_visible_; }

  void set_action_on_pause(const int i) { action_on_pause_ = i; }
  bool action_on_pause() const { return action_on_pause_; }

  void SetFaceSlotPosition(int index, Point position);

  void Render();

  // Clears the text window of all text and resets the insertion
  // point.
  void ClearWin();

  // Queue a batch for window-driven reveal;
  // ordinary page operations outside preparation still flush immediately.
  void PrepareMessageText(const std::function<void(TextWindow&)>& prepare);

  // Multi-message submissions keep queuing between preparation batches and
  // reveal independently for each stream.
  void BeginMultiMessage();
  void EndMultiMessage();
  bool HasPendingMessageText() const;

  // Number of characters on the current page, used for auto-mode timing.
  int GetCharacterCount() const;
  // A nonpositive Siglus overflow reserve disables the fit check.
  bool CanFitMessage(bool after_newline) const;

  void AdvanceMessageText(std::chrono::milliseconds delta);
  void FlushMessageText();

  void RenderNameInBox(const std::string& utf8str);

 private:
  std::pair<int, int> sorter_;
  std::optional<MessagePage> page_;
  MessagePage& EnsurePage();
  MessagePageState CaptureMessageState() const;
  bool ApplyMessageCommand(const MessageCommand& command, bool replay);
  TextLayout::Result DisplayTextImpl(const msgcmd::Text& command);
  void QueueText(std::string_view text,
                 const TextLayout::Result& layout,
                 const std::shared_ptr<IFont>& font);
  void SetNameImpl(const std::string& name, const std::string& next_char);
  void SetNameWithoutDisplayImpl(const std::string& name);
  void FaceOpenImpl(const std::string& filename, int index, bool replay);
  void FaceCloseImpl(int index, bool replay);
  void DisplayRubyTextImpl(const std::string& text);
  void NextMessageImpl(const msgcmd::NextMessage& command);
  bool restoring_ = false;
  bool history_view_ = false;
  std::unique_ptr<MessageStreamState> message_stream_;
  MessageStreamState& EnsureMessageStream();
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

  // Our numeric window identifier.
  int window_num_;

  TextboxConfig textbox_config_;
  // The window decorations for the text window
  std::unique_ptr<TextWaku> textbox_waku_;

  TextLayout layout_;
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
  int overflow_check_size_;

  bool is_visible_;
  std::size_t capture_generation_ = 0;

  using NameMode = TextLayout::NameMode;

  NameboxConfig namebox_config_;
  std::unique_ptr<TextWaku> namebox_waku_;

  // Name glyph bounds, excluding the two-pixel shadow added by RenderText.
  Size GetNameboxTextSize() const;

  // The number of name characters, clamped to namebox_config_.minimum_size.
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
