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

#include "systems/text_window.hpp"

#include "core/message_stream.hpp"
#include "machine/rlmachine.hpp"
#include "systems/itext_system.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"
#include "systems/text_waku.hpp"
#include "utilities/assertx.hpp"

#include <utf8.h>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

struct TextWindow::FaceSlot {
  explicit FaceSlot(const FaceSlotConfig& config)
      : x(config.x),
        y(config.y),
        is_behind(config.is_behind),
        hide_other_windows(config.hide_other_windows),
        unknown(config.unknown) {}

  int x, y;

  // 0 if layered in front or window background. 1 if behind.
  int is_behind;

  // Speculation: This makes ALMA work correctly and doesn't appear to harm
  // P_BRIDE.
  int hide_other_windows;

  // Unknown.
  int unknown;

  // The current face loaded. NULL whenever no face is loaded.
  std::shared_ptr<const SDLSurface> face_surface;
  std::string filename;
};

// -----------------------------------------------------------------------
// TextWindow
// -----------------------------------------------------------------------

TextWindow::TextWindow(System& system,
                       int window_num,
                       ITextSystem* text_impl,
                       const InitParams& params,
                       bool history_view)
    : sorter_(system.text().mwnd_config().order_, 0),
      history_view_(history_view),
      text_impl_(text_impl),
      window_num_(window_num),
      textbox_config_(params.textbox),
      layout_(params.layout),
      default_font_size_(params.default_font_size),
      use_indentation_(params.use_indentation),
      default_colour_(params.default_colour),
      font_colour_(params.default_colour),
      action_on_pause_(params.action_on_pause),
      overflow_check_size_(params.overflow_check_size),
      is_visible_(false),
      namebox_config_(params.namebox),
      namebox_characters_(0),
      next_char_italic_(false),
      system_(system) {
  ASSERTX_NE(text_impl, nullptr);
  ASSERTX_LE(0, params.name_mod);
  ASSERTX_LE(params.name_mod, 2);
  layout_.SetNameMode(static_cast<NameMode>(params.name_mod));

  for (std::size_t i = 0; i < params.face_slots.size(); ++i) {
    if (params.face_slots[i]) {
      face_slot_[i] = std::make_unique<FaceSlot>(*params.face_slots[i]);
    }
  }
}

TextWindow::~TextWindow() = default;

void MwndMessageState::ClearKoe() {
  current_koe = -1;
  current_character = -1;
  current_koe_played = false;
  current_koe_no_auto_mode = false;
}

void MwndMessageState::MarkNovelClear(TextWindow& window) {
  window.EndMultiMessage();
  block_started = false;
  auto_mode_base_chars = window.GetCharacterCount();
  ClearKoe();
}

void MwndMessageState::MarkMessageClearReady(TextWindow& window,
                                             LocalConfig& config) {
  clear_ready = true;
  MarkNovelClear(window);
  if (config.async_msg_mode_once) {
    config.async_msg_mode = false;
    config.async_msg_mode_once = false;
  }
}

const MessagePage& TextWindow::GetPage() { return EnsurePage(); }

MessagePage& TextWindow::EnsurePage() {
  if (!page_)
    NewPage();
  return *page_;
}

void TextWindow::NewPage() {
  page_.emplace();
  page_->initial_state = CaptureMessageState();
}

MessagePageState TextWindow::CaptureMessageState() const {
  MessagePageState state;
  state.layout = layout_;
  state.default_font_size = default_font_size_;
  state.default_color = default_colour_;
  state.color = font_colour_;
  state.name = current_name_;
  state.next_char_italic = next_char_italic_;
  for (int i = 0; i < kNumFaceSlots; ++i)
    if (face_slot_[i] && !face_slot_[i]->filename.empty())
      state.faces.push_back({face_slot_[i]->filename, i});
  return state;
}

bool TextWindow::Submit(MessageCommand command) {
  auto& page = EnsurePage();
  if (!ApplyMessageCommand(command, false))
    return false;
  if (std::holds_alternative<msgcmd::Name>(command))
    ++page.number_of_chars_on_page;
  page.history.push_back(std::move(command));
  return true;
}

bool TextWindow::ApplyMessageCommand(const MessageCommand& command,
                                     bool replay) {
  return std::visit(
      [this, replay](const auto& cmd) -> bool {
        using T = std::decay_t<decltype(cmd)>;
        if constexpr (std::is_same_v<T, msgcmd::Text>) {
          return DisplayTextImpl(cmd).consumed_bytes == cmd.accepted_bytes;
        } else if constexpr (std::is_same_v<T, msgcmd::Name>) {
          SetNameImpl(cmd.name, cmd.next_char);
        } else if constexpr (std::is_same_v<T, msgcmd::NameWithoutDisplay>) {
          SetNameWithoutDisplayImpl(cmd.name);
        } else if constexpr (std::is_same_v<T, msgcmd::SetNameMode>) {
          ASSERTX_LE(0, cmd.mode);
          ASSERTX_LE(cmd.mode, 2);
          layout_.SetNameMode(static_cast<NameMode>(cmd.mode));
        } else if constexpr (std::is_same_v<T, msgcmd::KoeMarker>) {
          if (replay && koe_replay_info_)
            koe_replay_button_.emplace_back(
                layout_.GetInsertionPoint() + koe_replay_info_->repos, cmd.id);
        } else if constexpr (std::is_same_v<T, msgcmd::HardBreak>) {
          layout_.HardBreak();
        } else if constexpr (std::is_same_v<T, msgcmd::NextMessage>) {
          NextMessageImpl(cmd);
        } else if constexpr (std::is_same_v<T, msgcmd::SetIndentation>) {
          layout_.SetIndentation();
        } else if constexpr (std::is_same_v<T, msgcmd::ClearIndentation>) {
          layout_.ClearIndentation();
        } else if constexpr (std::is_same_v<T, msgcmd::SetFontColor>) {
          font_colour_ = cmd.color;
        } else if constexpr (std::is_same_v<T, msgcmd::SetDefaultTextColor>) {
          default_colour_ = cmd.color;
        } else if constexpr (std::is_same_v<T, msgcmd::SetFontSize>) {
          layout_.SetFontSize(cmd.size);
        } else if constexpr (std::is_same_v<T, msgcmd::RubyBegin>) {
          layout_.RubyBegin();
        } else if constexpr (std::is_same_v<T, msgcmd::RubyText>) {
          DisplayRubyTextImpl(cmd.ruby);
        } else if constexpr (std::is_same_v<T, msgcmd::SetInsertion>) {
          layout_.SetInsertionPoint(cmd.point);
        } else if constexpr (std::is_same_v<T, msgcmd::SetGlyphRenderOffset>) {
          layout_.SetGlyphRenderOffset(cmd.offset);
        } else if constexpr (std::is_same_v<T, msgcmd::FaceOpen>) {
          FaceOpenImpl(cmd.filename, cmd.index, replay);
        } else if constexpr (std::is_same_v<T, msgcmd::FaceClose>) {
          FaceCloseImpl(cmd.index, replay);
        } else if constexpr (std::is_same_v<T, msgcmd::MarkItalic>) {
          next_char_italic_ = true;
        } else {
          static_assert(std::is_same_v<T, void>, "Unhandled message command");
        }
        return true;
      },
      command);
}

void TextWindow::Restore(const MessagePage& page,
                         std::optional<RGBColour> initial_color,
                         ReplayMode mode) {
  // Copy first: callers may restore this window's own page.
  MessagePage restored = page;
  const auto& state = restored.initial_state;
  const bool was_restoring = std::exchange(restoring_, true);
  try {
    layout_ = state.layout;
    text_surface_.reset();
    ClearWin();
    layout_ = state.layout;
    default_font_size_ = state.default_font_size;
    default_colour_ = state.default_color;
    font_colour_ = initial_color.value_or(state.color);
    next_char_italic_ = state.next_char_italic;
    if (!state.name.empty())
      SetNameWithoutDisplayImpl(state.name);
    // Recreating a namebox must not change the saved speaker transition.
    layout_ = state.layout;
    for (auto& slot : face_slot_)
      if (slot) {
        slot->face_surface.reset();
        slot->filename.clear();
      }
    for (const auto& face : state.faces)
      FaceOpenImpl(face.filename, face.index, true);
    if (mode == ReplayMode::Animated)
      EnsureMessageStream().deferring = true;
    for (const auto& command : restored.history)
      ApplyMessageCommand(command, true);
    if (mode == ReplayMode::Immediate)
      FlushMessageText();
    else
      EnsureMessageStream().deferring = false;
    page_ = std::move(restored);
  } catch (...) {
    restoring_ = was_restoring;
    throw;
  }
  restoring_ = was_restoring;
}

std::size_t TextWindow::SubmitText(msgcmd::Text command) {
  if (command.text.empty())
    return 0;
  auto& page = EnsurePage();
  const auto result = DisplayTextImpl(command);
  if (result.consumed_bytes == 0)
    return 0;
  command.accepted_bytes = result.consumed_bytes;
  page.number_of_chars_on_page += static_cast<int>(result.glyphs.size());
  page.history.emplace_back(std::move(command));
  return result.consumed_bytes;
}

void TextWindow::ClearMouseCapture() {
  ++capture_generation_;
  if (textbox_waku_)
    textbox_waku_->ClearMouseCapture();
  if (namebox_waku_)
    namebox_waku_->ClearMouseCapture();
}

TextWindow::SavedFaces TextWindow::CaptureFaces() const {
  SavedFaces faces;
  for (int i = 0; i < kNumFaceSlots; ++i) {
    if (const auto& slot = face_slot_[i])
      faces[i] =
          SavedFace{FaceSlotConfig{slot->x, slot->y, slot->is_behind,
                                   slot->hide_other_windows, slot->unknown},
                    slot->filename};
  }
  return faces;
}

void TextWindow::RestoreFaces(const SavedFaces& faces) {
  for (int i = 0; i < kNumFaceSlots; ++i) {
    if (!faces[i]) {
      face_slot_[i].reset();
      continue;
    }
    face_slot_[i] = std::make_unique<FaceSlot>(faces[i]->config);
    if (!faces[i]->filename.empty())
      FaceOpenImpl(faces[i]->filename, i, true);
  }
}

std::shared_ptr<TextWindow> TextWindow::Clone() const {
  auto result = std::make_shared<TextWindow>(
      system_, window_num_, text_impl_,
      system_.text().mwnd_config().GetWindow(window_num_), history_view_);
  result->sorter_ = sorter_;
  result->window_num_ = window_num_;
  result->SetTextboxConfig(GetTextboxConfig());
  result->layout_ = layout_;
  result->selection_size_ = selection_size_;
  result->current_name_ = current_name_;
  result->default_font_size_ = default_font_size_;
  result->use_indentation_ = use_indentation_;
  result->default_colour_ = default_colour_;
  result->font_colour_ = font_colour_;
  result->action_on_pause_ = action_on_pause_;
  result->overflow_check_size_ = overflow_check_size_;
  result->is_visible_ = is_visible_;
  result->SetNameboxConfig(GetNameboxConfig());
  result->namebox_characters_ = namebox_characters_;
  result->next_char_italic_ = next_char_italic_;
  result->koe_replay_button_ = koe_replay_button_;

  result->text_surface_ = text_surface_ ? text_surface_->Clone() : nullptr;
  if (message_stream_)
    result->message_stream_ =
        std::make_unique<MessageStreamState>(*message_stream_);
  result->name_surface_ = name_surface_ ? name_surface_->Clone() : nullptr;
  result->textbox_waku_ = textbox_waku_ ? textbox_waku_->Clone() : nullptr;
  result->namebox_waku_ = namebox_waku_ ? namebox_waku_->Clone() : nullptr;
  *result->message_state_ = *message_state_;
  if (page_)
    result->page_ = *page_;
  for (int i = 0; i < kNumFaceSlots; ++i)
    result->face_slot_[i] =
        face_slot_[i] ? std::make_unique<FaceSlot>(*face_slot_[i]) : nullptr;
  if (koe_replay_info_)
    result->koe_replay_info_ =
        std::make_unique<KoeReplayInfo>(*koe_replay_info_);
  return result;
}

void TextWindow::SetTextboxWaku(int waku_set,
                                std::unique_ptr<TextWaku> waku,
                                bool initialize_actions) {
  ASSERTX_NE(waku.get(), nullptr);
  if (textbox_waku_)
    waku->ResetObjectsFrom(*textbox_waku_);
  if (textbox_waku_)
    waku->ResetButtonsFrom(*textbox_waku_);
  if (textbox_waku_)
    waku->ResetFacesFrom(*textbox_waku_);
  textbox_config_.waku_set = waku_set;
  textbox_waku_ = std::move(waku);
  if (initialize_actions && !history_view_)
    system_.text().InitializeMwndButtonFrameActions(*textbox_waku_);
}

void TextWindow::SetNameboxWaku(int waku_set,
                                std::unique_ptr<TextWaku> waku,
                                bool initialize_actions) {
  ASSERTX_NE(waku.get(), nullptr);
  if (namebox_waku_)
    waku->ResetObjectsFrom(*namebox_waku_);
  if (namebox_waku_)
    waku->ResetButtonsFrom(*namebox_waku_);
  namebox_config_.waku_set = waku_set;
  namebox_waku_ = std::move(waku);
  if (initialize_actions && !history_view_)
    system_.text().InitializeMwndButtonFrameActions(*namebox_waku_);
}

void TextWindow::Execute(bool update_controls) {
  const bool controls_visible =
      update_controls && IsVisible() && !system_.text().IsInterfaceHidden();
  const auto execute = [controls_visible](TextWaku* waku) {
    if (!waku)
      return;
    if (controls_visible)
      waku->Execute();
    else
      waku->TextWaku::Execute();
  };
  execute(textbox_waku_.get());
  execute(namebox_waku_.get());
}

bool TextWindow::IsTextboxWakuVisible() const {
  const auto& text = system_.text();
  return textbox_waku_ && IsVisible() && text_surface_ &&
         text.system_visible() && text.ShowWindow(window_num_) &&
         !system_.text().IsInterfaceHidden();
}

bool TextWindow::IsNameboxWakuVisible() const {
  return namebox_waku_ && IsTextboxWakuVisible() && name_surface_;
}

void TextWindow::ShowWaitIcon(bool page) {
  if (!textbox_waku_)
    return;
  const Point position =
      GetTextSurfaceRect().origin() + layout_.GetInsertionPoint();
  textbox_waku_->SetWaitIcon(page, position);
}

void TextWindow::HideWaitIcon() {
  if (textbox_waku_)
    textbox_waku_->HideWaitIcon();
}

void TextWindow::SetWakuMainFile(const std::string& file) {
  if (textbox_waku_)
    textbox_waku_->SetMainSurface(
        file.empty() ? nullptr : system_.text().LoadImage(file));
}

void TextWindow::SetWakuFilterFile(const std::string& file) {
  if (textbox_waku_)
    textbox_waku_->SetFilterSurface(
        file.empty() ? nullptr : system_.text().LoadImage(file));
}

void TextWindow::SetNameImpl(const std::string& utf8name,
                             const std::string& next_char) {
  if (layout_.GetNameMode() == NameMode::Inline && !utf8name.empty()) {
    const auto font = system_.text().LoadFont(layout_.GetFontSize());
    const TextLayout::FontMetrics metrics{
        font->IsMonospace(),
        [font](int codepoint) { return font->GetCharWidth(codepoint); }};
    const auto result = layout_.LayoutName(utf8name, metrics, next_char);
    QueueText(utf8name, result, font);
  } else {
    (void)layout_.LayoutName(utf8name, {}, next_char);
  }

  SetNameWithoutDisplayImpl(utf8name);
}

void TextWindow::SetNameWithoutDisplayImpl(const std::string& utf8name) {
  current_name_ = utf8name;

  if (layout_.GetNameMode() == NameMode::SeparateWindow) {
    namebox_characters_ = 0;
    try {
      namebox_characters_ = utf8::distance(utf8name.begin(), utf8name.end());
    } catch (...) {
      // If utf8name isn't a real UTF-8 string, possibly overestimate:
      namebox_characters_ = utf8name.size();
    }
    namebox_characters_ =
        std::max(namebox_characters_, namebox_config_.minimum_size);
    RenderNameInBox(utf8name);
  }

  layout_.NameWithoutDisplay();
}

void TextWindow::RenderNameInBox(const std::string& utf8str) {
  RGBColour shadow = RGBAColour::Black().rgb();
  const int font_size = namebox_config_.siglus_layout
                            ? namebox_config_.character_size
                            : layout_.GetFontSize();
  const int x_spacing =
      namebox_config_.siglus_layout ? namebox_config_.x_spacing : 0;
  name_surface_ = system_.text().RenderText(utf8str, font_size, x_spacing, 0,
                                            font_colour_, &shadow, 0);
}

Size TextWindow::GetTextWindowSize() const {
  return selection_size_.value_or(layout_.GetNormalSize());
}

Size TextWindow::GetTextSurfaceSize() const {
  return selection_size_.value_or(layout_.GetExtendedSize());
}

Size TextWindow::GetWakuContentSize() const {
  if (!selection_size_)
    return GetTextSurfaceSize();
  // Siglus MESSAGE_MARGIN is measured from the outside of the frame;
  // the nine-slice renderer takes the size between its border pieces.
  const Size outer =
      *selection_size_ +
      Size(textbox_config_.left_box_padding + textbox_config_.right_box_padding,
           textbox_config_.upper_box_padding +
               textbox_config_.lower_box_padding);
  const Size border = textbox_waku_->GetSize(Size(0, 0));
  return Size(std::max(0, outer.width() - border.width()),
              std::max(0, outer.height() - border.height()));
}

Rect TextWindow::GetWindowRect() const {
  ASSERTX_NE(textbox_waku_, nullptr);
  Size waku_size = textbox_waku_->GetSize(GetWakuContentSize());
  return GetWindowRect(waku_size);
}

Rect TextWindow::GetWindowRect(Size waku_size) const {
  // This absolutely needs to know the size of the on main backing waku if we
  // want to draw things correctly! If we are going to offset this text box
  // from the top or the bottom, we MUST know what the size of the image
  // graphic is if we want accurate calculations, because some image graphics
  // are significantly larger than GetTextWindowSize() + the paddings.
  //
  // RealLive is definitely correcting programmer errors which places textboxes
  // offscreen. For example, take P_BRAVE (please!): #WINDOW.002.POS=2:78,6,
  // and a waku that refers to PM_WIN.g00 for it's image. That image is 800x300
  // pixels. The image is still centered perfectly, even though it's supposed
  // to be shifted 78 pixels right since the origin is the bottom
  // left. Expanding this number didn't change the position offscreen.

  int x, y;
  switch (textbox_config_.origin) {
    case 0:
    case 2:
      x = textbox_config_.x_distance_from_origin;
      break;
    case 1:
    case 3:
      x = textbox_config_.screen_size.width() - waku_size.width() -
          textbox_config_.x_distance_from_origin;
      break;
    [[unlikely]] default:
      throw std::logic_error("Invalid origin = " +
                             std::to_string(textbox_config_.origin));
  }

  switch (textbox_config_.origin) {
    case 0:  // Top and left
    case 1:  // Top and right
      y = textbox_config_.y_distance_from_origin;
      break;
    case 2:  // Bottom and left
    case 3:  // Bottom and right
      y = textbox_config_.screen_size.height() - waku_size.height() -
          textbox_config_.y_distance_from_origin;
      break;
    [[unlikely]] default:
      throw std::logic_error("Invalid origin = " +
                             std::to_string(textbox_config_.origin));
  }

  // Now that we have the coordinate that the programmer wanted to position the
  // box at, possibly move the box so it fits on screen.
  if ((x + waku_size.width()) > textbox_config_.screen_size.width())
    x -= (x + waku_size.width()) - textbox_config_.screen_size.width();
  if (x < 0)
    x = 0;

  if ((y + waku_size.height()) > textbox_config_.screen_size.height())
    y -= (y + waku_size.height()) - textbox_config_.screen_size.height();
  if (y < 0)
    y = 0;

  return Rect(x, y, waku_size);
}

Rect TextWindow::GetTextSurfaceRect() const {
  Rect window = GetWindowRect();

  Point textOrigin = window.origin() + Size(textbox_config_.left_box_padding,
                                            textbox_config_.upper_box_padding);

  Size rectSize = GetTextSurfaceSize();
  rectSize += Size(textbox_config_.right_box_padding,
                   textbox_config_.lower_box_padding);

  return Rect(textOrigin, rectSize);
}

Rect TextWindow::GetNameboxWakuRect() const {
  // Like the main GetWindowRect(), we need to ask the waku what size it wants
  // to be.
  Size boxSize = namebox_waku_->GetSize(GetNameboxTextArea());

  // The waku is offset from the top left corner of the text window.
  Rect r = GetWindowRect();
  if (namebox_config_.siglus_layout) {
    const Point anchor =
        r.origin() + Point(namebox_config_.x_offset, namebox_config_.y_offset);
    return namebox_config_.siglus_layout->GetWindowRect(anchor, boxSize,
                                                        GetNameboxTextSize());
  }
  return Rect(Point(r.x() + namebox_config_.x_offset,
                    r.y() + namebox_config_.y_offset - boxSize.height()),
              boxSize);
}

Size TextWindow::GetNameboxTextArea() const {
  if (namebox_config_.siglus_layout)
    return namebox_config_.siglus_layout->GetContentSize(GetNameboxTextSize());
  // TODO(erg): This seems excessively wide.
  return Size(
      2 * namebox_config_.horizontal_padding +
          namebox_characters_ * namebox_config_.character_size,
      namebox_config_.vertical_padding + namebox_config_.character_size);
}

Size TextWindow::GetNameboxTextSize() const {
  if (!name_surface_)
    return Size(0, namebox_config_.character_size);
  const Size surface = name_surface_->GetSize();
  return Size(std::max(surface.width() - 2, 0),
              std::max(surface.height() - 2, 0));
}

Point TextWindow::KeycursorPosition(const Size& cursor_size) const {
  switch (textbox_config_.keycursor_type) {
    case 0:
      return GetTextSurfaceRect().lower_right() - cursor_size;
    case 1:
      return GetTextSurfaceRect().origin() + layout_.GetInsertionPoint();
    case 2:
      return GetTextSurfaceRect().origin() + textbox_config_.keycursor_pos;
    [[unlikely]] default:
      throw std::logic_error("Invalid keycursor type = " +
                             std::to_string(textbox_config_.keycursor_type));
  }
}

void TextWindow::FaceOpenImpl(const std::string& filename,
                              int index,
                              bool replay) {
  if (index >= 0 && index < kNumFaceSlots && face_slot_[index]) {
    face_slot_[index]->face_surface = system_.text().LoadImage(filename);
    face_slot_[index]->filename = filename;

    if (!replay && face_slot_[index]->hide_other_windows) {
      system_.text().HideAllTextWindowsExcept(window_number());
    }
  }
}

void TextWindow::FaceCloseImpl(int index, bool replay) {
  if (index >= 0 && index < kNumFaceSlots && face_slot_[index]) {
    face_slot_[index]->face_surface.reset();
    face_slot_[index]->filename.clear();

    if (!replay && face_slot_[index]->hide_other_windows) {
      system_.text().HideAllTextWindowsExcept(window_number());
    }
  }
}

void TextWindow::SetFaceSlotPosition(int index, Point position) {
  if (index < 0 || index >= kNumFaceSlots)
    return;
  if (!face_slot_[index])
    face_slot_[index] = std::make_unique<FaceSlot>(FaceSlotConfig{});
  face_slot_[index]->x = position.x();
  face_slot_[index]->y = position.y();
}

static Point InsertionPoint(const Rect& waku_rect,
                            const Size& padding,
                            const Size& surface_size,
                            bool center_w,
                            bool center_h) {
  Point insertion_point = waku_rect.origin() + padding;
  if (center_w) {
    int half_width = (waku_rect.width() - 2 * padding.width()) / 2;
    int half_text_width = surface_size.width() / 2;
    insertion_point += Point(half_width - half_text_width, 0);
  }
  if (center_h) {
    int half_height = (waku_rect.height() - 2 * padding.height()) / 2;
    int half_text_height = surface_size.height() / 2;
    insertion_point += Point(0, half_height - half_text_height);
  }
  return insertion_point;
}

Rect TextWindow::GetNameboxTextRect() const {
  const Size surface_size = name_surface_->GetSize();
  if (namebox_config_.siglus_layout) {
    const Point anchor =
        GetWindowRect().origin() +
        Point(namebox_config_.x_offset, namebox_config_.y_offset);
    return Rect(namebox_config_.siglus_layout->GetTextOrigin(
                    anchor, GetNameboxTextSize()),
                surface_size);
  }
  const Point origin = InsertionPoint(
      GetNameboxWakuRect(),
      Size(namebox_config_.horizontal_padding,
           namebox_config_.vertical_padding),
      surface_size, namebox_config_.centering && namebox_waku_->center_w_,
      namebox_config_.centering && namebox_waku_->center_h_);
  return Rect(origin, surface_size);
}
// TODO(erg): Make this pass the #WINDOW_ATTR colour off wile rendering the
// waku_backing.
void TextWindow::Render() {
  if (text_surface_ && IsVisible()) {
    Size surface_size = text_surface_->GetSize();

    // POINT
    Point box = GetWindowRect().origin();

    Point textOrigin =
        GetTextSurfaceRect().origin() - Size(0, layout_.GetRubyTopPadding());

    textbox_waku_->renderer_ = system_.text().GetSceneRenderer();
    textbox_waku_->Render(box, GetWakuContentSize(), textbox_config_.colour,
                          textbox_config_.is_filter);
    RenderFaces(1);

    if (name_surface_) {
      Rect r = GetNameboxWakuRect();

      if (namebox_waku_) {
        namebox_waku_->renderer_ = system_.text().GetSceneRenderer();
        // TODO(erg): The waku needs to be adjusted to be the minimum size
        // of the window in characters
        namebox_waku_->Render(r.origin(), GetNameboxTextArea(),
                              textbox_config_.colour,
                              textbox_config_.is_filter);
      }

      name_surface_->RenderToScreen(name_surface_->GetRect(),
                                    GetNameboxTextRect(), 255);
    }

    RenderFaces(0);
    textbox_waku_->RenderFaces(box);
    RenderKoeReplayButtons();

    text_surface_->RenderToScreen(Rect(Point(0, 0), surface_size),
                                  Rect(textOrigin, surface_size), 255);
  }
}

void TextWindow::RenderFaces(int behind) {
  for (int i = 0; i < kNumFaceSlots; ++i) {
    if (face_slot_[i] && face_slot_[i]->face_surface &&
        face_slot_[i]->is_behind == behind) {
      const std::shared_ptr<const SDLSurface>& surface =
          face_slot_[i]->face_surface;

      Rect dest(GetWindowRect().x() + face_slot_[i]->x,
                GetWindowRect().y() + face_slot_[i]->y, surface->GetSize());
      surface->RenderToScreen(surface->GetRect(), dest, 255);
    }
  }
}

void TextWindow::RenderKoeReplayButtons() {
  if (!koe_replay_info_)
    return;
  for (const auto& [pt, _] : koe_replay_button_) {
    koe_replay_info_->icon->RenderToScreen(
        Rect(Point(0, 0), koe_replay_info_->icon->GetSize()),
        Rect(GetTextSurfaceRect().origin() + pt,
             koe_replay_info_->icon->GetSize()),
        255);
  }
}

void TextWindow::ClearWin() {
  message_stream_.reset();
  layout_.Reset();
  font_colour_ = default_colour_;
  koe_replay_button_.clear();

  // Allocate the text window surface
  if (!text_surface_)
    text_surface_ = std::make_shared<SDLSurface>(
        GetTextSurfaceSize() + Size(0, layout_.GetRubyTopPadding()));
  text_surface_->Fill(RGBAColour::Clear());

  name_surface_ = nullptr;
  current_name_.clear();
}

MessageStreamState& TextWindow::EnsureMessageStream() {
  if (!message_stream_)
    message_stream_ = std::make_unique<MessageStreamState>();
  return *message_stream_;
}

void TextWindow::PrepareMessageText(
    const std::function<void(TextWindow&)>& prepare) {
  auto& state = EnsureMessageStream();
  const bool was_deferring = std::exchange(state.deferring, true);
  try {
    prepare(*this);
  } catch (...) {
    state.deferring = was_deferring;
    throw;
  }

  state.deferring = was_deferring;
  AdvanceMessageText(std::chrono::milliseconds::zero());
}

void TextWindow::BeginMultiMessage() {
  system_.local_config().multi_msg_mode = true;
  EnsureMessageStream().deferring = true;
}

void TextWindow::EndMultiMessage() {
  system_.local_config().multi_msg_mode = false;
  if (message_stream_) {
    message_stream_->deferring = false;
    if (message_stream_->stream.Empty())
      message_stream_.reset();
  }
}

void TextWindow::NextMessageImpl(const msgcmd::NextMessage& command) {
  // Legacy next_msg creates a default-styled stream at the previous stream's
  // final layout position. It does not insert a line break of its own.
  layout_.NextMessage(command.layout);
  font_colour_ = command.color;
  next_char_italic_ = false;
  if (message_stream_ && message_stream_->deferring)
    message_stream_->stream.NextStream();
}

bool TextWindow::HasPendingMessageText() const {
  return message_stream_ && !message_stream_->stream.Empty();
}

int TextWindow::GetCharacterCount() const {
  return page_ ? std::max(0, page_->number_of_chars_on_page) : 0;
}

bool TextWindow::CanFitMessage(bool after_newline) const {
  return overflow_check_size_ <= 0 ||
         layout_.CanFitMessage(overflow_check_size_, after_newline);
}

void TextWindow::AdvanceMessageText(std::chrono::milliseconds delta) {
  if (!restoring_ && system_.text().IsReadingBacklog() &&
      system_.text().IsLiveWindow(this))
    return;
  if (!message_stream_)
    return;
  const bool flush =
      system_.ShouldFastForward() ||
      EffectiveMessageNowait(system_.local_config(), system_.global_config());
  const auto delay = std::chrono::milliseconds(
      flush ? 0
            : EffectiveMessageSpeed(system_.local_config(),
                                    system_.global_config()));
  if (message_stream_->stream.Advance(delta, delay, [this](const auto& draw) {
        MessageStreamState::Draw(*text_impl_, text_surface_, draw);
      }))
    system_.text().RequestRefresh();
  if (!message_stream_->deferring && message_stream_->stream.Empty())
    message_stream_.reset();
}

void TextWindow::FlushMessageText() {
  if (!restoring_ && system_.text().IsReadingBacklog() &&
      system_.text().IsLiveWindow(this))
    return;
  if (HasPendingMessageText()) {
    message_stream_->stream.Advance(
        std::chrono::milliseconds::zero(), std::chrono::milliseconds::zero(),
        [this](const auto& draw) {
          MessageStreamState::Draw(*text_impl_, text_surface_, draw);
        });
    system_.text().RequestRefresh();
    if (!message_stream_->deferring)
      message_stream_.reset();
  }
}

TextLayout::Result TextWindow::DisplayTextImpl(const msgcmd::Text& command) {
  if (command.text.empty() || layout_.IsFull())
    return {};
  const auto font = system_.text().LoadFont(layout_.GetFontSize());
  const TextLayout::FontMetrics metrics{
      font->IsMonospace(),
      [font](int codepoint) { return font->GetCharWidth(codepoint); }};
  auto result = layout_.LayoutText(
      command.text, metrics, command.final_lookahead, command.accepted_bytes);
  QueueText(command.text, result, font);
  return result;
}

void TextWindow::QueueText(std::string_view text,
                           const TextLayout::Result& layout,
                           const std::shared_ptr<IFont>& font) {
  if (layout.glyphs.empty())
    return;
  const int decoration =
      EffectiveFontShadow(system_.text().FontConfig(), system_.global_config());
  FontFace face{.font = font,
                .is_italic = next_char_italic_,
                .is_shadow = (decoration & 1) != 0,
                .is_futidoru = (decoration & 2) != 0};
  std::optional<RGBColour> shadow;
  if (face.is_shadow || face.is_futidoru)
    shadow = RGBAColour::Black().rgb();

  // Complete both geometry and rendering attributes before adding any of this
  // submission to the independently timed reveal stream.
  std::vector<MessageStream::Glyph> glyphs;
  glyphs.reserve(layout.glyphs.size());
  for (const auto& placement : layout.glyphs) {
    face.baseline_from_top = placement.baseline_from_top;
    glyphs.push_back(
        {std::string(text.substr(placement.byte_offset, placement.byte_count)),
         face, font_colour_, shadow, placement.position});
    face.is_italic = false;
  }
  next_char_italic_ = false;
  SetVisible(true);
  auto& state = EnsureMessageStream();
  for (auto& glyph : glyphs)
    state.stream.Enqueue(std::move(glyph));
  if (!state.deferring)
    FlushMessageText();
}

void TextWindow::DisplayRubyTextImpl(const std::string& utf8str) {
  // Empty annotations still close the span without loading a font or drawing.
  std::shared_ptr<SDLSurface> surface;
  if (!utf8str.empty() && layout_.IsRubyActive()) {
    const auto font = system_.text().LoadFont(layout_.GetRubyFontSize());
    surface = text_impl_->RenderText(utf8str, FontFace(font), font_colour_);
  }
  const auto destination =
      layout_.RubyEnd(utf8str, surface ? surface->GetSize() : Size());
  if (destination && surface) {
    auto& state = EnsureMessageStream();
    state.stream.Enqueue(MessageStream::Ruby{std::move(surface), *destination});
    if (!state.deferring)
      FlushMessageText();
  }
}

void TextWindow::SetMousePosition(const Point& pos, bool update_waku) {
  if (update_waku)
    textbox_waku_->SetMousePosition(pos);
  else
    textbox_waku_->ClearMousePosition();
}

bool TextWindow::HasMwndButtonAt(const Point& pos) const {
  return IsVisible() && !system_.text().IsInterfaceHidden() && textbox_waku_ &&
         textbox_waku_->HasButtonAt(pos);
}

bool TextWindow::HandleMouseClick(const Point& pos, bool pressed) {
  for (const auto& it : koe_replay_button_) {
    Rect r = Rect(GetTextSurfaceRect().origin() + it.first,
                  koe_replay_info_->icon->GetSize());
    if (r.Contains(pos)) {
      // We only want to actually replay the voice clip once, but we want to
      // catch both clicks.
      if (pressed)
        system_.sound().KoePlay(it.second);
      return true;
    }
  }

  if (IsVisible() && !system_.text().IsInterfaceHidden()) {
    return textbox_waku_->HandleMouseClick(pos, pressed);
  }

  return false;
}
