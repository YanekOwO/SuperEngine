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

#include "libsiglus/backlog.hpp"

#include "core/input.hpp"
#include "core/object_internal/drawer/file.hpp"
#include "libsiglus/runtime_checkpoint.hpp"
#include "libsiglus/save_load.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/sound_system.hpp"
#include "systems/text_system.hpp"

#include <algorithm>
#include <stdexcept>

namespace libsiglus {
SiglusBacklog::SiglusBacklog(SiglusRuntime& runtime)
    : runtime_(runtime),
      actions_(ButtonActionTable::ParseSiglus(*runtime.gameexe)) {}

bool SiglusBacklog::CanOpen() const {
  const auto& config = runtime_.local_config;
  return runtime_.message_back && runtime_.message_back->HasMessages() &&
         !config.disable_msgback && config.syscommenu.msg_back.exist &&
         config.syscommenu.msg_back.enable;
}

void SiglusBacklog::ConsumeInput() {
  if (runtime_.input_event_listener)
    runtime_.input_event_listener->ClearState();
  if (runtime_.script_input)
    runtime_.script_input->Clear();
}

std::shared_ptr<SiglusBacklog::Session> SiglusBacklog::Open(int owner) {
  if (IsOpen())
    return session_;
  if (!CanOpen())
    return {};
  session_ = std::make_shared<Session>();
  session_->owner = owner;
  Build();
  session_->scroll = max_scroll_;
  session_->open = true;
  if (runtime_.input_event_listener)
    mouse_ = runtime_.input_event_listener->mouse_pos;
  Position();
  runtime_.system->text().SetSkipMode(false);
  ConsumeInput();
  runtime_.YieldScripts();
  return session_;
}

void SiglusBacklog::Close() {
  if (session_)
    session_->open = false;
  pressed_.reset();
  pressed_row_.reset();
  ConsumeInput();
}

void SiglusBacklog::Restore(std::shared_ptr<Session> session) {
  Close();
  session_ = std::move(session);
  if (!IsOpen())
    return;
  if (session_->owner < 0 || session_->owner > 1)
    throw std::runtime_error("Invalid saved backlog owner");
  Build();
  Scroll(0);
  ConsumeInput();
}

std::optional<GraphicsObject> SiglusBacklog::Image(const std::string& name) {
  if (name.empty())
    return {};
  GraphicsObject object;
  object.Param().SetVisible(1);
  object.SetDrawer(std::make_unique<GraphicsObjectOfFile>(
      name, runtime_.images->GetSurfaceNamed(name)));
  return object;
}

void SiglusBacklog::Build() {
  auto& g = *runtime_.gameexe;
  const auto integer = [&](const std::string& name, int index, int fallback) {
    return g(name).IntAt(index).value_or(fallback);
  };
  const int width = std::max(1, integer("MSGBK.WINDOW_SIZE", 0, 640));
  const int height = std::max(1, integer("MSGBK.WINDOW_SIZE", 1, 480));
  window_ = Rect::REC(integer("MSGBK.WINDOW_POS", 0, 0),
                      integer("MSGBK.WINDOW_POS", 1, 0), width, height);
  const int left = integer("MSGBK.DISP_MARGIN", 0, 0);
  const int top = integer("MSGBK.DISP_MARGIN", 1, 0);
  const int right = integer("MSGBK.DISP_MARGIN", 2, 0);
  const int bottom = integer("MSGBK.DISP_MARGIN", 3, 0);
  clip_ = Rect::REC(window_.x() + left, window_.y() + top,
                    std::max(1, width - left - right),
                    std::max(1, height - top - bottom));
  message_x_ = window_.x() + integer("MSGBK.MESSAGE_POS", 0, 100);
  style_ = {.size = std::max(1, integer("MSGBK.MOJI_SIZE", 0, 24)),
            .xspace = integer("MSGBK.MOJI_SPACE", 0, 0),
            .yspace = integer("MSGBK.MOJI_SPACE", 1, 0),
            .count = std::max(1, integer("MSGBK.MOJI_CNT", 0, 35)),
            .color = integer("MSGBK.MOJI_COLOR", 0, 0),
            .shadow_color = integer("MSGBK.MOJI_SHADOW_COLOR", 0, 1),
            .outline_color = integer("MSGBK.MOJI_FUCHI_COLOR", 0, 1)};
  active_style_ = style_;
  active_style_.color = integer("MSGBK.ACTIVE_MOJI_COLOR", 0, style_.color);
  active_style_.outline_color =
      integer("MSGBK.ACTIVE_MOJI_FUCHI_COLOR", 0, style_.outline_color);
  active_style_.shadow_color =
      integer("MSGBK.ACTIVE_MOJI_SHADOW_COLOR", 0, style_.shadow_color);
  message_action_ = integer("MSGBK.MSG_CLICK_ACTION", 0, 1);
  background_ = Image(g("MSGBK.BACK_FILE").Str().value_or(""));
  filter_ = Image(g("MSGBK.FILTER_FILE").Str().value_or(""));
  if (!filter_) {
    auto surface = std::make_shared<SDLSurface>(window_.size());
    surface->Fill(RGBAColour(integer("MSGBK.FILTER_COLOR", 0, 0),
                             integer("MSGBK.FILTER_COLOR", 1, 0),
                             integer("MSGBK.FILTER_COLOR", 2, 0),
                             integer("MSGBK.FILTER_COLOR", 3, 192)));
    filter_.emplace();
    filter_->Param().SetVisible(1);
    filter_->SetDrawer(std::make_unique<GraphicsObjectOfFile>("", surface));
  }
  for (auto* object : {&background_, &filter_}) {
    if (*object) {
      (*object)->Param().SetX(window_.x());
      (*object)->Param().SetY(window_.y());
    }
  }
  rows_.clear();
  content_height_ = 0;
  auto context = MakeSerializationContext(runtime_).StringRenderer();
  for (const auto& entry : runtime_.message_back->GetEntries()) {
    if (entry.msg.empty() && !entry.pct_flag)
      continue;
    Row row;
    row.entry = entry;
    row.top = content_height_;
    row.object.Param().SetVisible(1);
    if (entry.pct_flag) {
      auto picture = Image(entry.image_file);
      if (picture)
        row.object = std::move(*picture);
    } else {
      const auto name =
          entry.display_name.empty() ? "" : entry.display_name + "\n";
      row.object.SetDrawer(
          std::make_unique<StringDrawer>(name + entry.msg, style_, context));
    }
    row.height = style_.size;
    if (row.object.HasDrawer()) {
      const auto surface = row.object.GetDrawer().CurrentSurface(row.object);
      if (surface)
        row.height = entry.pct_flag
                         ? row.object.GetDrawer().PixelHeight(row.object)
                         : surface->GetSize().height();
    }
    content_height_ += row.height + std::max(style_.size, style_.yspace);
    rows_.push_back(std::move(row));
  }
  max_scroll_ = std::max(0, content_height_ - clip_.height());
  buttons_.clear();
  for (std::size_t row = 0; row < rows_.size(); ++row) {
    const auto add = [&](const char* name, Action action) {
      const std::string key = std::string("MSGBK_ITEM.") + name;
      auto object = Image(g(key + ".FILE").Str().value_or(""));
      if (object)
        buttons_.push_back(
            {std::move(*object), action,
             Point(integer(key + ".POS", 0, 0), integer(key + ".POS", 1, 0)),
             integer(key + ".ACTION", 0, 0), integer(key + ".SE", 0, -1), row});
    };
    if (!rows_[row].entry.koe_no_list.empty())
      add("KOE_BTN", Action::Voice);
    if (runtime_.Saves().HasBacklogCheckpoint(rows_[row].entry.checkpoint_id))
      add("LOAD_BTN", Action::Load);
  }
  const std::pair<const char*, Action> items[] = {
      {"CLOSE_BTN", Action::Close},
      {"MSG_UP_BTN", Action::Up},
      {"MSG_DOWN_BTN", Action::Down},
      {"SLIDER", Action::Slider}};
  for (const auto& [name, action] : items) {
    const std::string key = std::string("MSGBK_ITEM.") + name;
    auto object = Image(g(key + ".FILE").Str().value_or(""));
    if (!object)
      continue;
    buttons_.push_back({std::move(*object), action,
                        Point(window_.x() + integer(key + ".POS", 0, 0),
                              window_.y() + integer(key + ".POS", 1, 0)),
                        integer(key + ".ACTION", 0, 0),
                        integer(key + ".SE", 0, -1)});
  }
  slider_top_ = window_.y() + integer("MSGBK_ITEM.SLIDER.POS", 1, 0);
  slider_bottom_ = window_.y() + integer("MSGBK_ITEM.SLIDER.POS", 2, height);
  pressed_.reset();
}

void SiglusBacklog::Position() {
  if (!session_)
    return;
  const int origin = clip_.y() + std::max(0, clip_.height() - content_height_) -
                     session_->scroll;
  for (auto& row : rows_) {
    row.object.Param().SetX(message_x_ +
                            (row.entry.pct_flag ? row.entry.image_x : 0));
    row.object.Param().SetY(origin + row.top);
    row.object.Param().clipping_region = clip_;
  }
  for (auto& button : buttons_) {
    button.object.Param().SetX(button.position.x() +
                               (button.row ? message_x_ : 0));
    int y = button.position.y();
    if (button.row) {
      y += origin + rows_[*button.row].top;
      button.object.Param().clipping_region = clip_;
    }
    if (button.action == Action::Slider) {
      const int height = button.object.GetDrawer().PixelHeight(button.object);
      const int travel = std::max(0, slider_bottom_ - slider_top_ - height);
      y = slider_top_ +
          (max_scroll_
               ? static_cast<int>(static_cast<long long>(session_->scroll) *
                                  travel / max_scroll_)
               : travel);
    }
    button.object.Param().SetY(y);
  }
}

void SiglusBacklog::Scroll(int pixels) {
  if (!session_)
    return;
  session_->scroll = static_cast<int>(std::clamp<long long>(
      static_cast<long long>(session_->scroll) + pixels, 0, max_scroll_));
  Position();
}

std::optional<std::size_t> SiglusBacklog::Hit() {
  for (std::size_t i = buttons_.size(); i > 0; --i) {
    auto& object = buttons_[i - 1].object;
    if (object.GetDrawer().HitTest(object, mouse_, {}, false))
      return i - 1;
  }
  return {};
}

bool SiglusBacklog::RowActionEnabled(std::size_t row) const {
  if (row >= rows_.size())
    return false;
  const auto& entry = rows_[row].entry;
  return message_action_ == 1
             ? !entry.koe_no_list.empty()
             : message_action_ == 2 &&
                   runtime_.Saves().HasBacklogCheckpoint(entry.checkpoint_id);
}

std::optional<std::size_t> SiglusBacklog::HitRow() const {
  if (!clip_.Contains(mouse_) || mouse_.x() < message_x_ ||
      mouse_.x() >= message_x_ + style_.count * (style_.size + style_.xspace))
    return {};
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    const auto& row = rows_[i];
    if (mouse_.y() >= row.object.Param().y() &&
        mouse_.y() < row.object.Param().y() + row.height && RowActionEnabled(i))
      return i;
  }
  return {};
}

std::optional<std::pair<int, int>> SiglusBacklog::ReplayVoice(std::size_t row) {
  if (!IsOpen() || row >= rows_.size())
    return {};
  const auto& entry = rows_[row].entry;
  if (entry.koe_no_list.empty())
    return {};
  if (session_->voice_row != static_cast<int>(row)) {
    session_->voice_row = row;
    session_->voice_offset = 0;
  }
  const auto index = session_->voice_offset++ % entry.koe_no_list.size();
  const int voice = entry.koe_no_list[index];
  const int character =
      index < entry.chara_no_list.size() ? entry.chara_no_list[index] : -1;
  if (character >= 0)
    runtime_.system->sound().KoePlay(voice, character);
  else
    runtime_.system->sound().KoePlay(voice);
  return std::pair(voice, character);
}

bool SiglusBacklog::RequestRollback(std::size_t row) {
  if (!IsOpen() || row >= rows_.size())
    return false;
  const auto id = rows_[row].entry.checkpoint_id;
  if (!runtime_.Saves().HasBacklogCheckpoint(id) ||
      !runtime_.Saves().OpenBacklogLoad(id))
    return false;
  Close();
  return true;
}

void SiglusBacklog::Activate(Button& button) {
  if (button.sound >= 0)
    runtime_.system->sound().PlaySe(button.sound);
  switch (button.action) {
    case Action::Close:
      Close();
      break;
    case Action::Up:
      Scroll(-style_.size * 3);
      break;
    case Action::Down:
      Scroll(style_.size * 3);
      break;
    case Action::Slider:
      break;
    case Action::Voice:
      ReplayVoice(*button.row);
      break;
    case Action::Load:
      RequestRollback(*button.row);
      break;
  }
}

void SiglusBacklog::OnEvent(std::shared_ptr<Event> event) {
  if (!event)
    return;
  if (const auto* key = std::get_if<KeyUp>(event.get());
      key && closing_key_ == key->code) {
    closing_key_.reset();
    *event = std::monostate();
    return;
  }
  if (const auto* key = std::get_if<KeyDown>(event.get());
      key && closing_key_ == key->code) {
    *event = std::monostate();
    return;
  }
  if (!IsOpen())
    return;
  const bool input = std::visit(
      [&](const auto& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, MouseMotion>) {
          mouse_ = runtime_.presenter
                       ? runtime_.presenter->DisplayToScreenPoint(e.pos)
                       : e.pos;
          if (pressed_ && buttons_[*pressed_].action == Action::Slider) {
            auto& button = buttons_[*pressed_];
            const int height =
                button.object.GetDrawer().PixelHeight(button.object);
            const int travel =
                std::max(1, slider_bottom_ - slider_top_ - height);
            session_->scroll = static_cast<int>(std::clamp<long long>(
                static_cast<long long>(mouse_.y() - slider_offset_ -
                                       slider_top_) *
                    max_scroll_ / travel,
                0, max_scroll_));
            Position();
          }
          return true;
        } else if constexpr (std::is_same_v<T, KeyDown>) {
          if (e.code == KeyCode::UP)
            Scroll(-style_.size * 3);
          else if (e.code == KeyCode::DOWN)
            Scroll(style_.size * 3);
          else if (e.code == KeyCode::PAGEUP)
            Scroll(-clip_.height());
          else if (e.code == KeyCode::PAGEDOWN)
            Scroll(clip_.height());
          else if (e.code == KeyCode::HOME)
            Scroll(-max_scroll_);
          else if (e.code == KeyCode::END)
            Scroll(max_scroll_);
          else if (IsCancelKey(e.code) || IsDecideKey(e.code)) {
            closing_key_ = e.code;
            Close();
          }
          return true;
        } else if constexpr (std::is_same_v<T, KeyUp>) {
          return true;
        } else if constexpr (std::is_same_v<T, MouseDown>) {
          if (e.button == MouseButton::WHEELUP)
            Scroll(-style_.size * 3);
          else if (e.button == MouseButton::WHEELDOWN)
            Scroll(style_.size * 3);
          else if (e.button == MouseButton::LEFT) {
            pressed_ = Hit();
            pressed_row_ = pressed_ ? std::nullopt : HitRow();
            if (pressed_ && buttons_[*pressed_].action == Action::Slider)
              slider_offset_ =
                  mouse_.y() - buttons_[*pressed_].object.Param().y();
          }
          return true;
        } else if constexpr (std::is_same_v<T, MouseUp>) {
          if (e.button == MouseButton::RIGHT)
            Close();
          else if (e.button == MouseButton::LEFT) {
            if (pressed_ && Hit() == pressed_)
              Activate(buttons_[*pressed_]);
            else if (pressed_row_ && HitRow() == pressed_row_) {
              if (message_action_ == 1)
                ReplayVoice(*pressed_row_);
              else if (message_action_ == 2)
                RequestRollback(*pressed_row_);
            }
            pressed_.reset();
            pressed_row_.reset();
          }
          return true;
        }
        return false;
      },
      *event);
  if (input)
    *event = std::monostate();
}

void SiglusBacklog::Render() {
  if (!IsOpen())
    return;
  if (filter_)
    filter_->GetDrawer().Render(*filter_);
  if (background_)
    background_->GetDrawer().Render(*background_);
  const auto active_row = HitRow();
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    auto& row = rows_[i];
    if (row.object.HasDrawer()) {
      if (!row.entry.pct_flag)
        static_cast<StringDrawer&>(row.object.GetDrawer())
            .SetStyle(active_row == i ? active_style_ : style_);
      row.object.GetDrawer().Render(row.object);
    }
  }
  const auto hit = Hit();
  for (std::size_t i = 0; i < buttons_.size(); ++i) {
    auto& button = buttons_[i];
    auto& param = button.object.Param();
    param.ClearButtonOverrides();
    if (button.action_no >= 0 &&
        static_cast<std::size_t>(button.action_no) < actions_.GetCount()) {
      const auto state = pressed_ == i ? ButtonState::Push
                         : hit == i    ? ButtonState::Hit
                                       : ButtonState::Normal;
      const auto action = actions_.GetEntry(button.action_no).GetState(state);
      param.SetButtonOverrides(action.pattern, action.rep_pos.x(),
                               action.rep_pos.y(), action.rep_tr,
                               action.rep_bright, action.rep_dark);
    }
    button.object.GetDrawer().Render(button.object);
  }
}
}  // namespace libsiglus
