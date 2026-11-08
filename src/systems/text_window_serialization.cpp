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

#include "systems/text_window.hpp"

#include "core/message_stream.hpp"
#include "core/object.hpp"
#include "core/object_internal/objdrawer.hpp"
#include "core/serialization_context.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"
#include "systems/text_waku.hpp"

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <stdexcept>

// Persistent fields belong to the window. Construction and frame rebuilding
// use host services without running window-initialization script callbacks.
template <class Archive>
void TextWindow::save(Archive& ar, const unsigned int) const {
  ar & sorter_ & window_num_ & history_view_ & page_ & textbox_config_ &
      namebox_config_ & layout_ & current_name_ & default_font_size_ &
      use_indentation_ & default_colour_ & font_colour_ & action_on_pause_ &
      overflow_check_size_ & is_visible_ & namebox_characters_ &
      next_char_italic_ & selection_size_ & message_state_ & koe_replay_button_;
  const bool revealing = HasPendingMessageText();
  const bool deferring = message_stream_ && message_stream_->deferring;
  ar & revealing & deferring;
  const auto faces = CaptureFaces();
  ar & faces;
  for (const auto* waku : {textbox_waku_.get(), namebox_waku_.get()}) {
    const bool exists = waku != nullptr;
    ar & exists;
    if (exists)
      ar & waku->objects_ & waku->faces_ & waku->face_positions_;
  }
}

template <class Archive>
void TextWindow::load(Archive& ar, const unsigned int) {
  const int constructed_window = window_num_;
  const bool constructed_history = history_view_;
  ar & sorter_ & window_num_ & history_view_ & page_ & textbox_config_ &
      namebox_config_ & layout_ & current_name_ & default_font_size_ &
      use_indentation_ & default_colour_ & font_colour_ & action_on_pause_ &
      overflow_check_size_ & is_visible_ & namebox_characters_ &
      next_char_italic_ & selection_size_ & message_state_ & koe_replay_button_;
  bool revealing = false, deferring = false;
  ar & revealing & deferring;
  if (constructed_window != window_num_ ||
      constructed_history != history_view_ || !message_state_)
    throw std::runtime_error("Invalid saved message window");
  SavedFaces faces;
  ar & faces;
  // Replaying commands rebuilds pixels without recording text or running hooks.
  const auto layout = layout_;
  const auto name = current_name_;
  const auto colour = font_colour_;
  const auto italic = next_char_italic_;
  const auto buttons = koe_replay_button_;
  const bool visible = is_visible_;
  if (page_)
    Restore(*page_, {},
            revealing ? ReplayMode::Animated : ReplayMode::Immediate);
  else {
    ClearWin();
    SetNameWithoutDisplayImpl(name);
  }
  layout_ = layout;
  font_colour_ = colour;
  next_char_italic_ = italic;
  koe_replay_button_ = buttons;
  is_visible_ = visible;
  if (deferring)
    EnsureMessageStream().deferring = true;
  RestoreFaces(faces);
  // Buttons are template state, not saved state. Do not inherit an existing
  // frame's buttons when loading into a previously populated window.
  textbox_waku_.reset();
  namebox_waku_.reset();
  system_.text().InitializeSavedWindow(*this);
  for (auto* slot : {&textbox_waku_, &namebox_waku_}) {
    bool exists;
    ar & exists;
    if (!exists) {
      slot->reset();
      continue;
    }
    if (!*slot)
      throw std::runtime_error("Missing saved message window frame");
    auto& waku = **slot;
    ar & waku.objects_ & waku.faces_ & waku.face_positions_;
    if (!history_view_)
      system_.text().InitializeMwndButtonFrameActions(waku);
  }
  if (!message_state_->waku_file.empty())
    SetWakuMainFile(message_state_->waku_file);
  if (!message_state_->filter_file.empty())
    SetWakuFilterFile(message_state_->filter_file);
  ClearMouseCapture();
}

namespace boost::serialization {
template <class Archive>
void save_construct_data(Archive& ar,
                         const TextWindow* window,
                         const unsigned int) {
  const int id = window->window_number();
  const bool history = window->history_view();
  ar & id & history;
}

template <class Archive>
void load_construct_data(Archive& ar, TextWindow* window, const unsigned int) {
  int id;
  bool history;
  ar & id & history;
  auto* system = SerializationContext::Get(ar).system;
  if (!system)
    throw std::runtime_error("Saved message window requires a text system");
  TextSystem::ConstructSavedWindow(window, *system, id, history);
}
}  // namespace boost::serialization

#define INSTANTIATE_WINDOW(Out, In)                              \
  template void TextWindow::save<Out>(Out&, unsigned int) const; \
  template void TextWindow::load<In>(In&, unsigned int);         \
  template void boost::serialization::save_construct_data<Out>(  \
      Out&, const TextWindow*, unsigned int);                    \
  template void boost::serialization::load_construct_data<In>(   \
      In&, TextWindow*, unsigned int);
INSTANTIATE_WINDOW(boost::archive::binary_oarchive,
                   boost::archive::binary_iarchive)
INSTANTIATE_WINDOW(boost::archive::text_oarchive, boost::archive::text_iarchive)
#undef INSTANTIATE_WINDOW
