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

#include "machine/rlmachine.hpp"
#include "systems/graphics_system.hpp"
#include "systems/itext_system.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/selection_element.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"
#include "systems/text_page.hpp"
#include "systems/text_system.hpp"
#include "systems/text_waku.hpp"
#include "utilities/assertx.hpp"
#include "utilities/string_utilities.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
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
};

// -----------------------------------------------------------------------
// TextWindow
// -----------------------------------------------------------------------

TextWindow::TextWindow(System& system,
                       int window_num,
                       ITextSystem* text_impl,
                       const InitParams& params)
    : sorter_(system.text().mwnd_config().order_, 0),
      text_impl_(text_impl),
      screen_width_(params.screen_size.width()),
      screen_height_(params.screen_size.height()),
      window_num_(window_num),
      waku_set_(params.waku_set),
      layout_(params.layout),
      glyph_render_offset_(0, 0),
      last_token_was_name_(false),
      default_font_size_(params.default_font_size),
      use_indentation_(params.use_indentation),
      default_colour_(params.default_colour),
      font_colour_(params.default_colour),
      action_on_pause_(params.action_on_pause),
      origin_(params.origin),
      x_distance_from_origin_(params.x_distance_from_origin),
      y_distance_from_origin_(params.y_distance_from_origin),
      upper_box_padding_(params.upper_box_padding),
      lower_box_padding_(params.lower_box_padding),
      left_box_padding_(params.left_box_padding),
      right_box_padding_(params.right_box_padding),
      window_attr_mod_(params.window_attr_mod),
      colour_(params.colour),
      is_filter_(params.is_filter),
      is_visible_(false),
      keycursor_type_(params.keycursor_type),
      keycursor_pos_(params.keycursor_pos),
      name_mod_(NameMode::Inline),
      name_waku_set_(params.namebox.waku_set),
      name_font_size_in_pixels_(0),
      name_waku_dir_set_(params.namebox.waku_dir_set),
      name_x_spacing_(params.namebox.x_spacing),
      horizontal_namebox_padding_(params.namebox.horizontal_padding),
      vertical_namebox_padding_(params.namebox.vertical_padding),
      namebox_x_offset_(params.namebox.x_offset),
      namebox_y_offset_(params.namebox.y_offset),
      namebox_centering_(params.namebox.centering),
      minimum_namebox_size_(params.namebox.minimum_size),
      name_size_(params.namebox.character_size),
      namebox_characters_(0),
      state_(State::Normal),
      next_char_italic_(false),
      system_(system) {
  ASSERTX_NE(text_impl, nullptr);
  SetNameMod(params.name_mod);

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

void MwndMessageState::MarkNovelClear(TextPage& page) {
  block_started = false;
  auto_mode_base_chars = page.number_of_chars_on_page();
  ClearKoe();
}

void MwndMessageState::MarkMessageClearReady(TextPage& page) {
  clear_ready = true;
  MarkNovelClear(page);
}

TextPage& TextWindow::GetPage() {
  if (!page_)
    NewPage();
  return *page_;
}

void TextWindow::NewPage() {
  page_ = std::make_unique<TextPage>(system_.gameexe(), *this);
}

void TextWindow::ClearMouseCapture() {
  for (auto* waku : {textbox_waku_.get(), namebox_waku_.get()})
    if (waku)
      waku->ClearMouseCapture();
}

std::shared_ptr<TextWindow> TextWindow::Clone() const {
  auto result = std::make_shared<TextWindow>(
      system_, window_num_, text_impl_,
      system_.text().mwnd_config().GetWindow(window_num_));
  result->sorter_ = sorter_;
  result->screen_width_ = screen_width_;
  result->screen_height_ = screen_height_;
  result->window_num_ = window_num_;
  result->waku_set_ = waku_set_;
  result->layout_ = layout_;
  result->glyph_render_offset_ = glyph_render_offset_;
  result->last_token_was_name_ = last_token_was_name_;
  result->current_name_ = current_name_;
  result->default_font_size_ = default_font_size_;
  result->use_indentation_ = use_indentation_;
  result->default_colour_ = default_colour_;
  result->font_colour_ = font_colour_;
  result->action_on_pause_ = action_on_pause_;
  result->origin_ = origin_;
  result->x_distance_from_origin_ = x_distance_from_origin_;
  result->y_distance_from_origin_ = y_distance_from_origin_;
  result->upper_box_padding_ = upper_box_padding_;
  result->lower_box_padding_ = lower_box_padding_;
  result->left_box_padding_ = left_box_padding_;
  result->right_box_padding_ = right_box_padding_;
  result->window_attr_mod_ = window_attr_mod_;
  result->colour_ = colour_;
  result->is_filter_ = is_filter_;
  result->is_visible_ = is_visible_;
  result->keycursor_type_ = keycursor_type_;
  result->keycursor_pos_ = keycursor_pos_;
  result->name_mod_ = name_mod_;
  result->name_waku_set_ = name_waku_set_;
  result->name_font_size_in_pixels_ = name_font_size_in_pixels_;
  result->name_waku_dir_set_ = name_waku_dir_set_;
  result->name_x_spacing_ = name_x_spacing_;
  result->horizontal_namebox_padding_ = horizontal_namebox_padding_;
  result->vertical_namebox_padding_ = vertical_namebox_padding_;
  result->namebox_x_offset_ = namebox_x_offset_;
  result->namebox_y_offset_ = namebox_y_offset_;
  result->namebox_centering_ = namebox_centering_;
  result->minimum_namebox_size_ = minimum_namebox_size_;
  result->name_size_ = name_size_;
  result->namebox_characters_ = namebox_characters_;
  result->state_ = state_;
  result->next_char_italic_ = next_char_italic_;
  result->koe_replay_button_ = koe_replay_button_;

  result->text_surface_ = text_surface_ ? text_surface_->Clone() : nullptr;
  result->name_surface_ = name_surface_ ? name_surface_->Clone() : nullptr;
  result->textbox_waku_ = textbox_waku_ ? textbox_waku_->Clone() : nullptr;
  result->namebox_waku_ = namebox_waku_ ? namebox_waku_->Clone() : nullptr;
  *result->message_state_ = *message_state_;
  if (page_)
    result->page_ = std::make_unique<TextPage>(page_->CloneFor(*result));
  for (int i = 0; i < kNumFaceSlots; ++i)
    result->face_slot_[i] =
        face_slot_[i] ? std::make_unique<FaceSlot>(*face_slot_[i]) : nullptr;
  if (koe_replay_info_)
    result->koe_replay_info_ =
        std::make_unique<KoeReplayInfo>(*koe_replay_info_);
  for (const auto& selection : selections_) {
    auto clone = std::make_unique<SelectionElement>(*selection);
    clone->OnMouseover([system = &system_] {
      if (system->sound().HasSe(0))
        system->sound().PlaySe(0);
    });
    result->selections_.push_back(std::move(clone));
  }
  return result;
}

void TextWindow::SetTextboxWaku(int waku_set, std::unique_ptr<TextWaku> waku) {
  ASSERTX_NE(waku.get(), nullptr);
  if (textbox_waku_)
    waku->ResetObjectsFrom(*textbox_waku_);
  waku_set_ = waku_set;
  textbox_waku_ = std::move(waku);
}

void TextWindow::SetNameboxWaku(int waku_set, std::unique_ptr<TextWaku> waku) {
  ASSERTX_NE(waku.get(), nullptr);
  if (namebox_waku_)
    waku->ResetObjectsFrom(*namebox_waku_);
  name_waku_set_ = waku_set;
  namebox_waku_ = std::move(waku);
}

void TextWindow::Execute(bool update_controls) {
  const bool controls_visible = update_controls && IsVisible() &&
                                !system_.graphics().is_interface_hidden();
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
         !system_.graphics().is_interface_hidden();
}

bool TextWindow::IsNameboxWakuVisible() const {
  return namebox_waku_ && IsTextboxWakuVisible() && name_surface_ &&
         state_ == State::Normal;
}

void TextWindow::ShowWaitIcon(bool page) {
  if (!textbox_waku_)
    return;
  const Point position = GetTextSurfaceRect().origin() +
                         Size(layout_.insertion_x, layout_.insertion_y);
  textbox_waku_->SetWaitIcon(page, position);
}

void TextWindow::HideWaitIcon() {
  if (textbox_waku_)
    textbox_waku_->HideWaitIcon();
}

void TextWindow::SetWakuMainFile(const std::string& file) {
  if (textbox_waku_)
    textbox_waku_->SetMainSurface(
        file.empty() ? nullptr : system_.graphics().GetSurfaceNamed(file));
}

void TextWindow::SetWakuFilterFile(const std::string& file) {
  if (textbox_waku_)
    textbox_waku_->SetFilterSurface(
        file.empty() ? nullptr : system_.graphics().GetSurfaceNamed(file));
}

void TextWindow::SetTextboxPadding(const std::vector<int>& pos_data) {
  upper_box_padding_ = pos_data.at(0);
  lower_box_padding_ = pos_data.at(1);
  left_box_padding_ = pos_data.at(2);
  right_box_padding_ = pos_data.at(3);
}

void TextWindow::SetName(const std::string& utf8name,
                         const std::string& next_char) {
  if (name_mod_ == NameMode::Inline) {
    // Display the name in one pass
    PrintTextToFunction(
        [this](const std::string& current, const std::string& rest) {
          return this->DisplayCharacter(current, rest);
        },
        utf8name, next_char);
    SetIndentation();
  }

  SetNameWithoutDisplay(utf8name);
}

void TextWindow::SetNameWithoutDisplay(const std::string& utf8name) {
  current_name_ = utf8name;

  if (name_mod_ == NameMode::SeparateWindow) {
    namebox_characters_ = 0;
    try {
      namebox_characters_ = utf8::distance(utf8name.begin(), utf8name.end());
    } catch (...) {
      // If utf8name isn't a real UTF-8 string, possibly overestimate:
      namebox_characters_ = utf8name.size();
    }
    namebox_characters_ = std::max(namebox_characters_, minimum_namebox_size_);
    RenderNameInBox(utf8name);
  }

  last_token_was_name_ = true;
}

void TextWindow::RenderNameInBox(const std::string& utf8str) {
  RGBColour shadow = RGBAColour::Black().rgb();
  name_surface_ = system_.text().RenderText(utf8str, font_size_in_pixels(), 0,
                                            0, font_colour_, &shadow, 0);
}

void TextWindow::SetDefaultTextColor(const std::vector<int>& colour) {
  SetDefaultTextColor(RGBColour(colour.at(0), colour.at(1), colour.at(2)));
}

void TextWindow::SetFontColor(const std::vector<int>& colour) {
  SetFontColor(RGBColour(colour.at(0), colour.at(1), colour.at(2)));
}

void TextWindow::SetWindowPosition(const std::vector<int>& pos_data) {
  origin_ = pos_data.at(0);
  x_distance_from_origin_ = pos_data.at(1);
  y_distance_from_origin_ = pos_data.at(2);
}

Size TextWindow::GetTextWindowSize() const { return layout_.GetNormalSize(); }

Size TextWindow::GetTextSurfaceSize() const {
  return layout_.GetExtendedSize();
}

Rect TextWindow::GetWindowRect() const {
  ASSERTX_NE(textbox_waku_, nullptr);
  Size waku_size = textbox_waku_->GetSize(GetTextSurfaceSize());
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
  switch (origin_) {
    case 0:
    case 2:
      x = x_distance_from_origin_;
      break;
    case 1:
    case 3:
      x = screen_width_ - waku_size.width() - x_distance_from_origin_;
      break;
    [[unlikely]] default:
      throw std::logic_error("Invalid origin = " + std::to_string(origin_));
  }

  switch (origin_) {
    case 0:  // Top and left
    case 1:  // Top and right
      y = y_distance_from_origin_;
      break;
    case 2:  // Bottom and left
    case 3:  // Bottom and right
      y = screen_height_ - waku_size.height() - y_distance_from_origin_;
      break;
    [[unlikely]] default:
      throw std::logic_error("Invalid origin = " + std::to_string(origin_));
  }

  // Now that we have the coordinate that the programmer wanted to position the
  // box at, possibly move the box so it fits on screen.
  if ((x + waku_size.width()) > screen_width_)
    x -= (x + waku_size.width()) - screen_width_;
  if (x < 0)
    x = 0;

  if ((y + waku_size.height()) > screen_height_)
    y -= (y + waku_size.height()) - screen_height_;
  if (y < 0)
    y = 0;

  return Rect(x, y, waku_size);
}

Rect TextWindow::GetTextSurfaceRect() const {
  Rect window = GetWindowRect();

  Point textOrigin =
      window.origin() + Size(left_box_padding_, upper_box_padding_);

  Size rectSize = GetTextSurfaceSize();
  rectSize += Size(right_box_padding_, lower_box_padding_);

  return Rect(textOrigin, rectSize);
}

Rect TextWindow::GetNameboxWakuRect() const {
  // Like the main GetWindowRect(), we need to ask the waku what size it wants
  // to be.
  Size boxSize = namebox_waku_->GetSize(GetNameboxTextArea());

  // The waku is offset from the top left corner of the text window.
  Rect r = GetWindowRect();
  return Rect(Point(r.x() + namebox_x_offset_,
                    r.y() + namebox_y_offset_ - boxSize.height()),
              boxSize);
}

Size TextWindow::GetNameboxTextArea() const {
  // TODO(erg): This seems excessively wide.
  return Size(
      2 * horizontal_namebox_padding_ + namebox_characters_ * name_size_,
      vertical_namebox_padding_ + name_size_);
}

void TextWindow::SetNameMod(const int in) {
  ASSERTX_LE(0, in);
  ASSERTX_LE(in, 2);
  name_mod_ = static_cast<NameMode>(in);
}

void TextWindow::SetNameSpacingBetweenCharacters(int pos_data) {
  name_x_spacing_ = pos_data;
}

void TextWindow::SetNameboxPadding(const std::vector<int>& pos_data) {
  if (pos_data.size() >= 1)
    horizontal_namebox_padding_ = pos_data.at(0);
  if (pos_data.size() >= 2)
    vertical_namebox_padding_ = pos_data.at(1);
}

void TextWindow::SetNameboxPosition(const std::vector<int>& pos_data) {
  namebox_x_offset_ = pos_data.at(0);
  namebox_y_offset_ = pos_data.at(1);
}

void TextWindow::SetKeycursorMod(const std::vector<int>& keycur) {
  keycursor_type_ = keycur.at(0);
  keycursor_pos_ = Point(keycur.at(1), keycur.at(2));
}

Point TextWindow::KeycursorPosition(const Size& cursor_size) const {
  switch (keycursor_type_) {
    case 0:
      return GetTextSurfaceRect().lower_right() - cursor_size;
    case 1:
      return GetTextSurfaceRect().origin() + layout_.GetInsertionPoint();
    case 2:
      return GetTextSurfaceRect().origin() + keycursor_pos_;
    [[unlikely]] default:
      throw std::logic_error("Invalid keycursor type = " +
                             std::to_string(keycursor_type_));
  }
}

void TextWindow::FaceOpen(const std::string& filename, int index) {
  if (face_slot_[index]) {
    face_slot_[index]->face_surface =
        system_.graphics().GetSurfaceNamed(filename);

    if (face_slot_[index]->hide_other_windows) {
      system_.text().HideAllTextWindowsExcept(window_number());
    }
  }
}

void TextWindow::FaceClose(int index) {
  if (face_slot_[index]) {
    face_slot_[index]->face_surface.reset();

    if (face_slot_[index]->hide_other_windows) {
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

void TextWindow::NextCharIsItalic() { next_char_italic_ = true; }

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
// TODO(erg): Make this pass the #WINDOW_ATTR colour off wile rendering the
// waku_backing.
void TextWindow::Render() {
  if (text_surface_ && IsVisible()) {
    Size surface_size = text_surface_->GetSize();

    // POINT
    Point box = GetWindowRect().origin();

    Point textOrigin = GetTextSurfaceRect().origin();

    textbox_waku_->renderer_ = system_.graphics().GetSceneRenderer();
    textbox_waku_->Render(box, surface_size, GetColour(), GetIsFilter());
    RenderFaces(1);

    switch (state_) {
      case State::Selection:
        std::for_each(
            selections_.begin(), selections_.end(),
            [](std::unique_ptr<SelectionElement>& e) { e->Render(); });
        break;
      case State::Normal:
        if (name_surface_) {
          Rect r = GetNameboxWakuRect();

          if (namebox_waku_) {
            namebox_waku_->renderer_ = system_.graphics().GetSceneRenderer();
            // TODO(erg): The waku needs to be adjusted to be the minimum size
            // of the window in characters
            namebox_waku_->Render(r.origin(), GetNameboxTextArea(), GetColour(),
                                  GetIsFilter());
          }

          bool center_w = namebox_waku_->center_w_,
               center_h = namebox_waku_->center_h_;
          if (!namebox_centering_)
            center_h = center_w = false;
          Point insertion_point = InsertionPoint(
              r, Size(horizontal_namebox_padding_, vertical_namebox_padding_),
              name_surface_->GetSize(), center_w, center_h);

          name_surface_->RenderToScreen(
              name_surface_->GetRect(),
              Rect(insertion_point, name_surface_->GetSize()), 255);
        }

        RenderFaces(0);
        RenderKoeReplayButtons();

        text_surface_->RenderToScreen(Rect(Point(0, 0), surface_size),
                                      Rect(textOrigin, surface_size), 255);

        break;
    }
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
  layout_.Reset();
  font_colour_ = default_colour_;
  koe_replay_button_.clear();

  // Allocate the text window surface
  if (!text_surface_)
    text_surface_ = std::make_shared<SDLSurface>(GetTextSurfaceSize());
  text_surface_->Fill(RGBAColour::Clear());

  name_surface_ = nullptr;
  current_name_.clear();
}

bool TextWindow::DisplayCharacter(const std::string& current,
                                  const std::string& rest) {
  // If this text page is already full, save some time and reject
  // early.
  if (IsFull())
    return false;

  SetVisible(true);

  if (!current.empty()) {
    int cur_codepoint = Codepoint(current);
    bool indent_after_spacing = false;

    // But if the last character was a lenticular bracket, we need to indent
    // now. See doc/notes/NamesAndIndentation.txt for more details.
    if (last_token_was_name_) {
      switch (name_mod_) {
        case NameMode::Inline:
        case NameMode::Disable:
          if (IsOpeningQuoteMark(cur_codepoint))
            indent_after_spacing = true;
          break;

        case NameMode::SeparateWindow:
          break;
      }
    }

    std::shared_ptr<IFont> font =
        system_.text().LoadFont(font_size_in_pixels());
    FontFace font_face{.font = font,
                       .is_italic = std::exchange(next_char_italic_, false)};

    // If our font is not monospaced ASCII, we aren't using
    // the recommended font so we'll try laying out the text so that
    // kerning looks better. This is the common case.
    std::optional<int> width;
    if (cur_codepoint < 127 && !font->IsMonospace())
      width = font->GetCharWidth(cur_codepoint);

    std::optional<Point> insertion_point =
        layout_.PlaceCharacter(cur_codepoint, width, rest);
    if (!insertion_point.has_value())
      return false;

    std::optional<RGBColour> shadow;
    if (system_.text().font_shadow())
      shadow = RGBAColour::Black().rgb();

    text_impl_->RenderGlyphOnto(current, font_face, font_colour_, shadow,
                                *insertion_point + glyph_render_offset_,
                                text_surface_);

    if (indent_after_spacing)
      SetIndentation();
  }

  last_token_was_name_ = false;

  return true;
}

bool TextWindow::IsFull() const { return layout_.IsFull(); }

void TextWindow::KoeMarker(int id) {
  if (!koe_replay_info_)
    return;
  Point p = layout_.GetInsertionPoint() + koe_replay_info_->repos;
  koe_replay_button_.emplace_back(p, id);
}

void TextWindow::SetIndentation() {
  layout_.indent_pixels = layout_.insertion_x;
}
void TextWindow::ResetIndentation() { layout_.indent_pixels = 0; }

void TextWindow::MarkRubyBegin() { layout_.RubyBegin(); }

void TextWindow::DisplayRubyText(const std::string& utf8str) {
  if (std::optional<Rect> ruby_area = layout_.PlaceRubyText(utf8str)) {
    std::shared_ptr<IFont> ifont =
        system_.text().LoadFont(layout_.ruby_font_size);
    std::shared_ptr<SDLSurface> ruby_surface =
        text_impl_->RenderText(utf8str, FontFace(ifont), font_colour_);

    Point origin = ruby_area->origin();
    int delta_x =
        (ruby_area->size().width() - ruby_surface->GetSize().width()) / 2;
    origin += Point(delta_x, 0);

    text_surface_->blitFROMSurface(*ruby_surface, ruby_surface->GetRect(),
                                   Rect(origin, ruby_area->size()));
  }

  last_token_was_name_ = false;
}

void TextWindow::SetRGBAF(const std::vector<int>& attr) {
  ASSERTX_GE(attr.size(), 5);
  SetColour(RGBAColour(attr.at(0), attr.at(1), attr.at(2), attr.at(3)));
  SetFilter(attr.at(4));
}

void TextWindow::SetMousePosition(const Point& pos, bool update_waku) {
  if (state_ == State::Selection) {
    for_each(selections_.begin(), selections_.end(),
             [&](std::unique_ptr<SelectionElement>& e) {
               e->SetMousePosition(pos);
             });
  }

  if (update_waku)
    textbox_waku_->SetMousePosition(pos);
  else
    textbox_waku_->ClearMousePosition();
}

bool TextWindow::HasMwndButtonAt(const Point& pos) const {
  return IsVisible() && !system_.graphics().is_interface_hidden() &&
         textbox_waku_ && textbox_waku_->HasButtonAt(pos);
}

bool TextWindow::HandleMouseClick(const Point& pos, bool pressed) {
  if (state_ == State::Selection) {
    bool found =
        std::any_of(selections_.begin(), selections_.end(),
                    [&](auto& e) { return e->HandleMouseClick(pos, pressed); });

    if (found)
      return true;
  }

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

  if (IsVisible() && !system_.graphics().is_interface_hidden()) {
    return textbox_waku_->HandleMouseClick(pos, pressed);
  }

  return false;
}

void TextWindow::StartSelection(std::vector<Selection> choices) {
  state_ = State::Selection;

  std::shared_ptr<IFont> ifont = system_.text().LoadFont(font_size_in_pixels());

  selections_.clear();
  selections_.reserve(choices.size());
  for (auto& choice : choices) {
    std::shared_ptr<SDLSurface> normal =
        text_impl_->RenderText(choice.text, FontFace(ifont), font_colour_);

    // clone and create an inverted surface
    auto inverted = normal->Clone();
    inverted->Apply(
        [](RGBAColour c) {
          c.set_alpha(255 - c.a());
          return c;
        },
        inverted->GetRect());

    // Figure out xpos and ypos
    Point position =
        GetTextSurfaceRect().origin() + layout_.GetInsertionPoint();
    layout_.insertion_y += layout_.GetLineHeight();

    auto sel = std::make_unique<SelectionElement>(normal, inverted, position);
    sel->OnMouseover([this]() {
      constexpr int kHoverSoundEffect = 0;
      if (system_.sound().HasSe(kHoverSoundEffect))
        system_.sound().PlaySe(kHoverSoundEffect);
    });
    sel->OnSelect(std::move(choice.callback));

    selections_.emplace_back(std::move(sel));
  }
}

void TextWindow::EndSelection() {
  state_ = State::Normal;
  selections_.clear();
  ClearWin();
}
