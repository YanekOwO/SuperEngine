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

#include <fontconfig/fontconfig.h>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

#include "systems/text_system.hpp"

#include "core/gameexe.hpp"
#include "core/stage.hpp"
#include "machine/rlmachine.hpp"
#include "machine/serialization.hpp"
#include "modules/jump.hpp"
#include "systems/graphics_system.hpp"
#include "systems/itext_system.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"
#include "systems/text_factory.hpp"
#include "systems/text_key_cursor.hpp"
#include "systems/text_page.hpp"
#include "systems/text_waku.hpp"
#include "systems/text_window.hpp"
#include "utf8.h"
#include "utilities/exception.hpp"
#include "utilities/graphics.hpp"
#include "utilities/string_utilities.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

const unsigned int MAX_PAGE_HISTORY = 100;

const int FULLWIDTH_NUMBER_SIGN = 0xFF03;
const int FULLWIDTH_A = 0xFF21;
const int FULLWIDTH_B = 0xFF22;
const int FULLWIDTH_ZERO = 0xFF10;
const int FULLWIDTH_NINE = 0xFF19;

constexpr int kSeparateNameWindowMode = 1;

struct KoeReplayConfig {
  std::string icon_name;
  int x_offset = 0;
  int y_offset = 0;
};

static std::optional<KoeReplayConfig> ReadKoeReplayConfig(Gameexe& gexe) {
  GameexeInterpretObject replay_icon(gexe("KOEREPLAYICON"));
  auto name = replay_icon("NAME").Str();
  if (!name || name->empty())
    return std::nullopt;

  KoeReplayConfig config;
  config.icon_name = std::move(name.value());

  auto reppos = replay_icon("REPPOS").IntVec();
  if (reppos && reppos->size() == 2) {
    config.x_offset = reppos->at(0);
    config.y_offset = reppos->at(1);
  }

  return config;
}

static std::unique_ptr<TextWindow::KoeReplayInfo> BuildKoeReplayInfo(
    System& system,
    Gameexe& gexe) {
  auto config = ReadKoeReplayConfig(gexe);
  if (!config)
    return nullptr;

  auto koe = std::make_unique<TextWindow::KoeReplayInfo>();
  koe->icon = system.graphics().GetSurfaceNamed(config->icon_name);
  koe->repos = Size(config->x_offset, config->y_offset);
  return koe;
}

// -----------------------------------------------------------------------
// TextSystem
// -----------------------------------------------------------------------
TextSystem::TextSystem(System& system,
                       Stage& stage,
                       Gameexe& gexe,
                       std::unique_ptr<ITextSystem> impl,
                       MwndConfig mwnd_config)
    : text_impl_(std::move(impl)),
      local_config_(system.local_config()),
      global_config_(system.global_config()),
      active_window_(0),
      stage_(stage),
      mwnd_config_(std::move(mwnd_config)),
      is_reading_backlog_(false),
      current_pageset_(),
      in_pause_state_(false),
      system_visible_(true),
      skip_mode_(false),
      kidoku_read_(false),
      in_selection_mode_(false),
      system_(system) {
  stage_.mwnd_order = mwnd_config_.order_;
  for (int layer : {kLayerFg, kLayerBg, kLayerNext})
    stage_.mwnd.Resize(layer,
                       layer == kLayerFg || mwnd_config_.layered_windows()
                           ? mwnd_config_.window_capacity()
                           : 0);
  text_impl_->InitSystem();

  // Iterate over all the NAMAE keys, which is a feature that Clannad English
  // Edition uses to translate the Japanese names into English.
  for (auto it : gexe.Filter("NAMAE")) {
    try {
      // Data in the Gameexe.ini file is implicitly in Shift-JIS and needs to
      // be converted to UTF-8.
      std::string key = cp932toUTF8(it.StrAt(0).value(), 0);
      std::string value = cp932toUTF8(it.StrAt(1).value(), 0);
      namae_mapping_[key] = value;
    } catch (...) {
      // Gameexe.ini file is malformed.
    }
  }
  previous_page_it_ = previous_page_sets_.end();
}

TextSystem::~TextSystem() {
  resolved_fonts_.clear();
  font_cache_.clear();
  text_impl_->QuitSystem();
}

void TextSystem::ExecuteTextSystem() {
  auto current = FindMwnd(kLayerFg, active_window_);
  if (!mwnd_config_.layered_windows() && current &&
      ShowWindow(active_window_) && current->IsVisible() && in_pause_state_ &&
      !IsReadingBacklog()) {
    if (!text_key_cursor_)
      SetKeyCursor(0);
    text_key_cursor_->Execute();
  }
  for (int layer : {kLayerBg, kLayerFg, kLayerNext})
    for (const auto& window : stage_.mwnd[layer])
      if (window)
        window->Execute(layer == kLayerFg &&
                        ShowWindow(window->window_number()));
}

void TextSystem::RenderWindow(TextWindow& window) {
  if (!system_visible() || !ShowWindow(window.window_number()) ||
      system_.graphics().is_interface_hidden())
    return;
  window.Render();
}

void TextSystem::Render() {
  for (const auto& window : stage_.mwnd[kLayerFg])
    if (window)
      RenderWindow(*window);
  auto current = FindMwnd(kLayerFg, active_window_);
  if (!mwnd_config_.layered_windows() && system_visible() && current &&
      ShowWindow(active_window_) && current->IsVisible() && in_pause_state_ &&
      !IsReadingBacklog() && !system_.graphics().is_interface_hidden()) {
    if (!text_key_cursor_)
      SetKeyCursor(0);
    text_key_cursor_->Render(*current);
  }
}

std::shared_ptr<TextWindow> TextSystem::FindMwnd(int layer, int index) const {
  const auto& windows = stage_.mwnd[layer];
  return index >= 0 && static_cast<std::size_t>(index) < windows.size()
             ? windows[index]
             : nullptr;
}

bool TextSystem::IsForegroundWindow(const TextWindow* window) const {
  return window && FindMwnd(kLayerFg, window->window_number()).get() == window;
}

void TextSystem::HideTextWindow(int id) {
  if (auto window = FindMwnd(kLayerFg, id))
    window->SetVisible(false);
}

void TextSystem::HideAllTextWindows() {
  for (const auto& window : stage_.mwnd[kLayerFg])
    if (window)
      window->SetVisible(false);
}

void TextSystem::HideAllTextWindowsExcept(int id) {
  for (const auto& window : stage_.mwnd[kLayerFg])
    if (window && window->window_number() != id)
      window->SetVisible(false);
}

void TextSystem::ShowTextWindow(int id) {
  if (auto window = FindMwnd(kLayerFg, id))
    window->SetVisible(true);
}

void TextSystem::ShowAllTextWindows() {
  for (const auto& window : stage_.mwnd[kLayerFg])
    if (window)
      window->SetVisible(true);
}

void TextSystem::ClearAllTextWindows() {
  for (const auto& window : stage_.mwnd[kLayerFg])
    if (window)
      window->ClearWin();
}

void TextSystem::SetVisualOverride(int win_number, bool show_window) {
  window_visual_override_[win_number] = show_window;
}

void TextSystem::SetVisualOverrideAll(bool show_window) {
  for (int i = 0; i < 64; ++i) {
    window_visual_override_[i] = show_window;
  }
}

void TextSystem::ClearVisualOverrides() { window_visual_override_.clear(); }

std::shared_ptr<TextWindow> TextSystem::GetMwnd(
    int window_id,
    std::function<std::shared_ptr<TextWindow>()> orelse) {
  if (window_id < 0)
    throw std::out_of_range("Negative message-window index");
  if (!mwnd_config_.layered_windows() &&
      static_cast<std::size_t>(window_id) >= stage_.mwnd.Size(kLayerFg))
    stage_.mwnd.Resize(kLayerFg, static_cast<std::size_t>(window_id) + 1);
  return stage_.mwnd.Get(kLayerFg, window_id, std::move(orelse));
}

std::shared_ptr<TextWindow> TextSystem::GetMwnd(int window_id) {
  return GetMwnd(kLayerFg, window_id);
}

std::shared_ptr<TextWindow> TextSystem::GetMwnd(int layer, int window_id) {
  if (window_id < 0 || !mwnd_config_.HasWindow(window_id))
    throw std::out_of_range("Invalid message-window index");
  return stage_.mwnd.Get(layer, window_id, [this, window_id]() {
    Gameexe& gexe = system_.gameexe();
    TextWindow::InitParams params = mwnd_config_.GetWindow(window_id);
    if (params.window_attr_mod == 0) {
      params.colour = global_config_.filter_color;
      params.is_filter = global_config_.reallive.window_filter != 0;
    }
    auto tw = std::make_shared<TextWindow>(system_, window_id, text_impl_.get(),
                                           params);

    TextFactory waku_factory(mwnd_config_);
    tw->SetTextboxWaku(params.waku_set, waku_factory.CreateWaku(
                                            system_, *tw, params.waku_set, 0));
    if (mwnd_config_.HasWaku(params.waku_set)) {
      const auto& faces = mwnd_config_.GetWaku(params.waku_set).face_positions;
      for (std::size_t i = 0; i < faces.size() && i < MwndConfig::kNumFaceSlots;
           ++i)
        tw->SetFaceSlotPosition(static_cast<int>(i), faces[i]);
    }
    if (tw->GetNameMod() == kSeparateNameWindowMode &&
        params.namebox.has_waku) {
      tw->SetNameboxWaku(
          params.namebox.waku_set,
          waku_factory.CreateWaku(system_, *tw, params.namebox.waku_set, 0));
    }

    const auto& waku = mwnd_config_.GetWaku(params.waku_set);
    tw->message_state()->waku_file = waku.main_file;
    tw->message_state()->filter_file = waku.filter_file;
    tw->ClearWin();
    tw->SetKoeReplayInfo(BuildKoeReplayInfo(system_, gexe));
    return tw;
  });
}

bool TextSystem::HasCurrentWindow() const {
  return static_cast<bool>(FindMwnd(kLayerFg, active_window_));
}

std::shared_ptr<TextWindow> TextSystem::GetCurrentMwnd() {
  return GetMwnd(active_window_);
}

bool TextSystem::IsMwndButtonEnabled(const MwndConfig::Button& button) const {
  return GetMwndButtonPresentation(button).enabled;
}

void TextSystem::InitializeMwndButtonFrameActions(TextWaku& waku) {
  if (!mwnd_frame_action_handler_)
    return;
  const auto buttons = waku.button_objects_;
  for (const auto& object : buttons)
    if (object && object->Param().button.config &&
        object->Param().button.config->frame_action)
      mwnd_frame_action_handler_(object,
                                 *object->Param().button.config->frame_action);
}

TextSystem::MwndButtonPresentation TextSystem::GetMwndButtonPresentation(
    const MwndConfig::Button& button,
    bool has_script_call) const {
  MwndActionStatus status;
  if (mwnd_action_status_handler_)
    status = mwnd_action_status_handler_(button);

  using Action = MwndConfig::ButtonAction;
  if (!status.supported) {
    switch (button.action) {
      case Action::None:
      case Action::HideWindow:
      case Action::MessageLog:
      case Action::ReadSkip:
      case Action::AutoMode:
      case Action::ClearWindow:
      case Action::BackPage:
      case Action::ForwardPage:
        status.supported = true;
        break;
      case Action::RealliveFarcall:
        status.supported = system_.machine_ != nullptr;
        break;
      default:
        break;
    }
  }

  if (button.action == Action::ReadSkip) {
    status.exists = local_config_.syscommenu.read_skip.exist;
    status.enabled = status.enabled &&
                     local_config_.syscommenu.read_skip.enable && kidoku_read_;
    status.mode = skip_mode_ ? 1 : 0;
  } else if (button.action == Action::AutoMode) {
    status.exists = local_config_.syscommenu.auto_mode.exist;
    status.enabled =
        status.enabled && local_config_.syscommenu.auto_mode.enable;
    status.mode = EffectiveAutoMode(local_config_, global_config_) ? 1 : 0;
  }

  const bool has_call =
      has_script_call ||
      (button.call &&
       (static_cast<bool>(mwnd_call_handler_) ||
        (button.action == Action::RealliveFarcall && system_.machine_)));
  const bool mode_matches =
      !button.required_mode || *button.required_mode == status.mode;
  const auto& disabled = local_config_.mwnd_btn_disable;
  const bool slot_enabled =
      button.slot_id < 0 ||
      static_cast<std::size_t>(button.slot_id) >= disabled.size() ||
      !disabled[button.slot_id];
  return MwndButtonPresentation{
      .visible = mode_matches,
      .enabled = (status.supported || has_call) && status.exists &&
                 status.enabled && !local_config_.mwnd_btn_disable_all &&
                 slot_enabled,
      .touch_enabled = !local_config_.disable_mwnd_btn_touch,
      .activated = !button.required_mode &&
                   ((button.action == Action::ReadSkip && skip_mode_) ||
                    (button.action == Action::AutoMode &&
                     EffectiveAutoMode(local_config_, global_config_))),
  };
}

void TextSystem::ExecuteMwndButton(const MwndConfig::Button& button) {
  if (button.call) {
    if (button.action == MwndConfig::ButtonAction::RealliveFarcall &&
        system_.machine_ && button.call->entrypoint) {
      try {
        Farcall(*system_.machine_, std::stoi(button.call->scene),
                *button.call->entrypoint);
      } catch (const std::exception&) {
      }
    } else if (mwnd_call_handler_) {
      mwnd_call_handler_(*button.call);
    }
    return;
  }

  if (mwnd_action_handler_ && mwnd_action_handler_(button, true))
    return;

  using Action = MwndConfig::ButtonAction;
  switch (button.action) {
    case Action::HideWindow:
      HideTextWindow(active_window_);
      break;
    case Action::MessageLog:
    case Action::BackPage:
      BackPage();
      break;
    case Action::ForwardPage:
      ForwardPage();
      break;
    case Action::ReadSkip:
      SetSkipMode(button.mode == 0);
      break;
    case Action::AutoMode:
      global_config_.enable_auto_mode = button.mode == 0;
      break;
    case Action::ClearWindow:
      system_.graphics().ToggleInterfaceHidden();
      break;
    default:
      break;
  }
}

void TextSystem::PlayMwndButtonSound(const MwndConfig::Button& button,
                                     MwndButtonSound event) {
  const MwndConfig::ButtonSound* sounds =
      mwnd_config_.GetButtonSound(button.se_no);
  if (!sounds)
    return;
  int sound = -1;
  switch (event) {
    case MwndButtonSound::Hit:
      sound = sounds->hit;
      break;
    case MwndButtonSound::Push:
      sound = sounds->push;
      break;
    case MwndButtonSound::Decide:
      sound = sounds->decide;
      break;
  }
  if (sound >= 0)
    system_.sound().PlaySe(sound);
}

void TextSystem::ExpireOldPages() {
  while (previous_page_sets_.size() > MAX_PAGE_HISTORY)
    previous_page_sets_.pop_front();
}

void TextSystem::OnEvent(std::shared_ptr<Event> event) {
  if (!CurrentlySkipping() || in_selection_mode_)
    return;

  const bool result = std::visit(
      [&](auto& event) -> bool {
        using T = std::decay_t<decltype(event)>;
        if constexpr (std::same_as<T, KeyDown> || std::same_as<T, MouseDown>)
          return true;
        else
          return false;
      },
      *event);

  if (result) {  // quit skipping mode, and discard the event
    SetSkipMode(false);
    *event = std::monostate();
  }
}

std::vector<int> TextSystem::GetActiveWindows() {
  std::vector<int> result;
  for (const auto& window : stage_.mwnd[kLayerFg])
    if (window && window->HasPage())
      result.push_back(window->window_number());
  return result;
}

void TextSystem::CollectCurrentPages() {
  current_pageset_.clear();
  for (const auto& window : stage_.mwnd[kLayerFg])
    if (window && window->HasPage())
      current_pageset_.emplace(window->window_number(),
                               window->GetPage().CloneFor(*window, window));
}

void TextSystem::Snapshot() {
  CollectCurrentPages();
  const bool all_empty =
      std::all_of(current_pageset_.begin(), current_pageset_.end(),
                  [](const auto& entry) { return entry.second.empty(); });
  if (!all_empty) {
    previous_page_sets_.push_back(current_pageset_);
    ExpireOldPages();
  }
}

void TextSystem::NewPageOnWindow(int window_id) {
  NewPageOnWindow(GetMwnd(window_id));
}

void TextSystem::NewPageOnWindow(std::shared_ptr<TextWindow> window) {
  window->NewPage();
  previous_page_it_ = previous_page_sets_.end();
  ExpireOldPages();
}

TextPage& TextSystem::GetCurrentPage() {
  return GetPageOnWindow(GetCurrentMwnd());
}

TextPage& TextSystem::GetPageOnWindow(std::shared_ptr<TextWindow> window) {
  return window->GetPage();
}

void TextSystem::BackPage() {
  if (!is_reading_backlog_)
    CollectCurrentPages();
  is_reading_backlog_ = true;

  if (previous_page_it_ != previous_page_sets_.begin()) {
    previous_page_it_ = std::prev(previous_page_it_);

    // Clear all windows
    ClearAllTextWindows();
    HideAllTextWindows();

    ReplayPageSet(*previous_page_it_, false);
  }
}

void TextSystem::ForwardPage() {
  if (!is_reading_backlog_)
    CollectCurrentPages();
  is_reading_backlog_ = true;

  if (previous_page_it_ != previous_page_sets_.end()) {
    previous_page_it_ = std::next(previous_page_it_);

    // Clear all windows
    ClearAllTextWindows();
    HideAllTextWindows();

    if (previous_page_it_ != previous_page_sets_.end())
      ReplayPageSet(*previous_page_it_, false);
    else
      ReplayPageSet(current_pageset_, false);
  }
}

void TextSystem::ReplayPageSet(PageSet& set, bool is_current_page) {
  for (PageSet::iterator it = set.begin(); it != set.end(); ++it) {
    try {
      auto window = GetMwnd(it->first);
      auto page = it->second.CloneFor(*window);
      page.Replay(is_current_page);
    } catch (rlvm::Exception& e) {
      // Currently, the text system can throw on a few unimplemented situations,
      // such as ruby across lines.

      // Ignore what would normally be an ignored command when encountered from
      // the main loop.
    }
  }
}

bool TextSystem::IsReadingBacklog() const { return is_reading_backlog_; }

void TextSystem::StopReadingBacklog() {
  is_reading_backlog_ = false;

  // Clear all windows
  ClearAllTextWindows();
  HideAllTextWindows();
  CollectCurrentPages();
  ReplayPageSet(current_pageset_, true);
}

std::string TextSystem::InterpretName(const std::string& utf8name) {
  auto it = namae_mapping_.find(utf8name);
  if (it == namae_mapping_.end())
    return utf8name;
  return it->second;
}

void TextSystem::SetKeyCursor(int new_cursor) {
  if (new_cursor == -1) {
    text_key_cursor_.reset();
  } else if (!text_key_cursor_ ||
             text_key_cursor_->cursor_number() != new_cursor) {
    text_key_cursor_ = std::make_shared<TextKeyCursor>(system(), new_cursor);
  }
}

int TextSystem::GetCursorNumber() const {
  if (text_key_cursor_)
    return text_key_cursor_->cursor_number();
  else
    return -1;
}

void TextSystem::UpdateWindowsForChangeToWindowAttr() {
  const auto& colour = global_config_.filter_color;
  const std::vector<int> attr = {colour.r(), colour.g(), colour.b(), colour.a(),
                                 global_config_.reallive.window_filter};
  // Check each text window to see if it needs updating
  for (const auto& layer : stage_.mwnd.storage)
    for (const auto& window : layer)
      if (window && !window->GetAttrMod())
        window->SetRGBAF(attr);
}

bool TextSystem::ShowWindow(int win_num) const {
  std::map<int, bool>::const_iterator it =
      window_visual_override_.find(win_num);
  if (it != window_visual_override_.end())
    return it->second;
  else
    return true;
}

void TextSystem::SetMousePosition(const Point& pos) {
  auto target = mwnd_capture_window_ ? mwnd_capture_window_->lock() : nullptr;
  if (target && !IsForegroundWindow(target.get()))
    target.reset();
  const auto& windows = stage_.mwnd[kLayerFg];
  if (!target)
    for (auto it = windows.rbegin(); it != windows.rend(); ++it)
      if (*it && ShowWindow((*it)->window_number()) &&
          (*it)->HasMwndButtonAt(pos)) {
        target = *it;
        break;
      }
  for (const auto& window : windows)
    if (window && ShowWindow(window->window_number()))
      window->SetMousePosition(pos, window == target);
}

bool TextSystem::HandleMouseClick(const Point& pos, bool pressed) {
  if (!system_visible())
    return false;
  if (!pressed && mwnd_capture_window_) {
    auto window = mwnd_capture_window_->lock();
    mwnd_capture_window_.reset();
    if (window && IsForegroundWindow(window.get()))
      (void)window->HandleMouseClick(pos, false);
    SetMousePosition(pos);
    return true;
  }
  const auto& windows = stage_.mwnd[kLayerFg];
  for (auto it = windows.rbegin(); it != windows.rend(); ++it)
    if (*it && ShowWindow((*it)->window_number()) &&
        (*it)->HandleMouseClick(pos, pressed)) {
      if (pressed)
        mwnd_capture_window_ = *it;
      return true;
    }
  return false;
}

void TextSystem::TakeSavepointSnapshot() {
  savepoint_active_window_ = active_window();
  savepoint_cursor_number_ = GetCursorNumber();
}

bool parseInteger(std::string::const_iterator& begin,
                  std::string::const_iterator end,
                  int& out_value) {
  std::string::const_iterator str_begin = begin;

  if (begin != end && *begin == '-')
    begin++;
  while (begin != end && *begin >= '0' && *begin <= '9')
    begin++;

  if (str_begin == begin)
    return false;

  out_value = std::stoi(std::string(str_begin, begin));
  return true;
}

std::shared_ptr<SDLSurface> TextSystem::RenderText(const std::string& utf8str,
                                                   int size,
                                                   int xspace,
                                                   int yspace,
                                                   const RGBColour& colour,
                                                   RGBColour* shadow_colour,
                                                   int max_chars_in_line) {
  const int line_max_width =
      (max_chars_in_line > 0) ? (size + xspace) * max_chars_in_line : INT_MAX;

  // On the first pass, we figure out how large of a surface we need for
  // rendering the text.
  int current_size = size;
  int total_height = 0;
  int max_width = 0;
  int current_line_width = 0;
  int current_line_height = 0;
  bool should_break = false;
  std::string::const_iterator it = utf8str.begin();
  std::string::const_iterator strend = utf8str.end();
  while (it != strend) {
    int codepoint = utf8::next(it, strend);
    bool add_char = true;
    bool is_emoji = false;

    if (codepoint == '#') {
      add_char = false;
      codepoint = utf8::next(it, strend);
      switch (codepoint) {
        case 'D':
        case 'd': {
          should_break = true;
          break;
        }
        case 'S':
        case 's': {
          int val;
          if (parseInteger(it, strend, val)) {
            current_size = val;
          } else {
            current_size = size;
          }
          break;
        }
        case 'C':
        case 'c': {
          // Consume an integer. Or don't.
          int val;
          parseInteger(it, strend, val);
        }
        case 'X':
        case 'x':
        case 'Y':
        case 'y':
        case '#':
          break;
        default: {
          // Ooops. This isn't a control code.
          add_char = true;
        }
      }
    } else if (codepoint == FULLWIDTH_NUMBER_SIGN) {
      // The codepoint is a fullwidth '#'. If the codepoint after this is a
      // fullwidth 'A' or 'B', followed by two fullwidth digits, we have an
      // emoji code, and should treat it like a fullwidth space during the
      // allocation phase.
      std::string::const_iterator n = it;
      int next_codepoint = utf8::next(n, strend);
      if (next_codepoint == FULLWIDTH_A || next_codepoint == FULLWIDTH_B) {
        int num_one = utf8::next(n, strend);
        int num_two = utf8::next(n, strend);
        if (num_one >= FULLWIDTH_ZERO && num_one <= FULLWIDTH_NINE &&
            num_two >= FULLWIDTH_ZERO && num_two <= FULLWIDTH_NINE) {
          // This is an emoji mark. We should consume all input so far.
          it = n;
          is_emoji = true;
        }
      }
    }

    int added_width = 0;
    if (add_char)
      added_width = LoadFont(current_size)->GetCharWidth(codepoint) + xspace;
    else if (is_emoji)
      added_width = size + xspace;

    if (added_width) {
      if (should_break || current_line_width + added_width > line_max_width) {
        total_height += current_line_height + yspace;
        current_line_height = current_size;
        current_line_width = added_width;
        should_break = false;
      } else {
        current_line_width += added_width;
        current_line_height = std::max(current_line_height, current_size);
      }

      max_width = std::max(max_width, current_line_width);
    }
  }
  total_height += current_line_height;
  should_break = false;

  // If this text has a shadow, our surface needs to have a final two pixels
  // added to the bottom and right to accommodate it.
  if (shadow_colour) {
    total_height += 2;
    max_width += 2;
  }

  // If we have an empty width or height, make sure we create a non-empty
  // surface to render onto.
  if (max_width == 0)
    max_width = 1;
  if (total_height == 0)
    total_height = 1;

  auto surface = std::make_shared<SDLSurface>(Size(max_width, total_height));
  surface->Fill(RGBAColour::Clear());

  RGBColour current_colour = colour;
  int currentX = 0;
  int currentY = 0;
  current_size = size;
  current_line_height = 0;
  it = utf8str.begin();
  std::string::const_iterator cur_end = it;
  while (it != strend) {
    int codepoint = utf8::next(cur_end, strend);
    std::string character(it, cur_end);
    bool add_char = true;
    bool is_emoji = false;
    int emoji_id = 0;
    std::shared_ptr<const SDLSurface> emoji_surface;

    if (codepoint == '#') {
      add_char = false;
      codepoint = utf8::next(cur_end, strend);
      switch (codepoint) {
        case 'D':
        case 'd': {
          should_break = true;
          break;
        }
        case 'S':
        case 's': {
          int val;
          if (parseInteger(cur_end, strend, val)) {
            current_size = val;
          } else {
            current_size = size;
          }
          break;
        }
        case 'C':
        case 'c': {
          // Consume an integer. Or don't.
          int val;
          if (parseInteger(cur_end, strend, val)) {
            Gameexe& gexe = system().gameexe();
            auto colvec = gexe("COLOR_TABLE", val)
                              .IntVec()
                              .value_or(std::vector<int>{colour.r(), colour.g(),
                                                         colour.b()});
            current_colour =
                RGBColour(colvec.at(0), colvec.at(1), colvec.at(2));
          } else {
            current_colour = colour;
          }
        }
        case 'X':
        case 'x':
        case 'Y':
        case 'y':
        case '#':
          break;
        default: {
          // Ooops. This isn't a control code.
          add_char = true;
        }
      }
    } else if (codepoint == FULLWIDTH_NUMBER_SIGN) {
      // The codepoint is a fullwidth '#'. If the codepoint after this is a
      // fullwidth 'A' or 'B', followed by two fullwidth digits, we have an
      // emoji code, and should treat it like a fullwidth space during the
      // allocation phase.
      std::string::const_iterator n = cur_end;
      int next_codepoint = utf8::next(n, strend);
      if (next_codepoint == FULLWIDTH_A || next_codepoint == FULLWIDTH_B) {
        int num_one = utf8::next(n, strend);
        int num_two = utf8::next(n, strend);
        if (num_one >= FULLWIDTH_ZERO && num_one <= FULLWIDTH_NINE &&
            num_two >= FULLWIDTH_ZERO && num_two <= FULLWIDTH_NINE) {
          // This is an emoji mark. We should consume all input so far.
          cur_end = n;

          // Get the emoji number from the text.
          num_one -= FULLWIDTH_ZERO;
          num_two -= FULLWIDTH_ZERO;
          emoji_id = num_one * 10 + num_two;

          // Look up what g00 surface we should use for emoji.
          emoji_surface = system().graphics().GetEmojiSurface();
          is_emoji = true;
          add_char = false;
        }
      }
    }

    int item_width = 0;
    if (add_char) {
      // If we add this character, will we horizontally overflow?
      item_width = LoadFont(current_size)->GetCharWidth(codepoint);
    } else if (is_emoji) {
      // Whatever the real size is, we only allocate the incoming size. This
      // means that if the emoji is larger than |size|, we'll draw text over
      // it. This is to be bug for bug compatible with RealLive.
      item_width = size;
    }

    if (item_width) {
      if (should_break || currentX + item_width > max_width) {
        currentX = 0;
        currentY += current_line_height + yspace;
        current_line_height = 0;
        should_break = false;
      }
    }

    if (add_char) {
      const int decoration = EffectiveFontShadow(local_config_, global_config_);
      std::optional<RGBColour> shadow;
      if ((decoration & 3) && shadow_colour != nullptr)
        shadow = *shadow_colour;

      FontFace font{.font = LoadFont(current_size),
                    .is_italic = false,
                    .is_shadow = (decoration & 1) != 0,
                    .is_futidoru = (decoration & 2) != 0};
      Size s =
          text_impl_->RenderGlyphOnto(character, font, current_colour, shadow,
                                      Point(currentX, currentY), surface);

      currentX += s.width() + xspace;
      current_line_height = std::max(current_line_height, current_size);
    } else if (is_emoji) {
      if (emoji_surface) {
        // Emoji surfaces don't have internal pattnos. Instead, assume that
        // icons are square and laid out to the right, sort of like mouse
        // cursors.
        int height = emoji_surface->GetSize().height();
        Rect src = Rect(height * emoji_id, 0, Size(height, height));
        Rect dst = Rect(currentX, currentY, Size(height, height));
        emoji_surface->BlitToSurface(*surface, src, dst, 255, false);
      }

      currentX += size + xspace;
      current_line_height = std::max(current_line_height, current_size);
    }

    it = cur_end;
  }

  return surface;
}

void TextSystem::Reset() {
  is_reading_backlog_ = false;

  current_pageset_.clear();
  previous_page_sets_.clear();
  previous_page_it_ = previous_page_sets_.end();

  window_visual_override_.clear();
  stage_.mwnd.Clear();

  system_visible_ = true;
  in_pause_state_ = false;
  in_selection_mode_ = false;
  kidoku_read_ = false;
  skip_mode_ = false;
  mwnd_capture_window_.reset();
}

void TextSystem::ApplyConfiguration() { UpdateWindowsForChangeToWindowAttr(); }

void TextSystem::SetKidokuRead(const int in) {
  bool value_changed = kidoku_read_ != in;

  kidoku_read_ = in;
  if (value_changed && !kidoku_read_ && skip_mode_) {
    // Auto leave skip mode when we stop reading previously read text.
    SetSkipMode(false);
  }
}

void TextSystem::SetSkipMode(int in) { skip_mode_ = in; }

template <class Archive>
void TextSystem::load(Archive& ar, unsigned int version) {
  int win, cursor_num;
  ar & win & cursor_num;

  set_active_window(win);
  SetKeyCursor(cursor_num);
}

template <class Archive>
void TextSystem::save(Archive& ar, unsigned int version) const {
  ar & savepoint_active_window_ & savepoint_cursor_number_;
}

// -----------------------------------------------------------------------

// Explicit instantiations for text archives (since we hide the
// implementation)

template void TextSystem::save<boost::archive::text_oarchive>(
    boost::archive::text_oarchive& ar,
    unsigned int version) const;

template void TextSystem::load<boost::archive::text_iarchive>(
    boost::archive::text_iarchive& ar,
    unsigned int version);

// -----------------------------------------------------------------------

std::string parseNames(MemoryHandle& memory, const std::string& input) {
  std::string output;
  const char* cur = input.c_str();

  const char LOWER_BYTE_FULLWIDTH_ASTERISK = 0x96;
  const char LOWER_BYTE_FULLWIDTH_PERCENT = 0x93;

  while (*cur) {
    if (cur[0] == 0x81 && (cur[1] == LOWER_BYTE_FULLWIDTH_ASTERISK ||
                           cur[1] == LOWER_BYTE_FULLWIDTH_PERCENT)) {
      char type = cur[1];
      cur += 2;

      string strindex;
      if (ReadFullwidthLatinLetter(cur, strindex)) {
        // Try to read a second character. We don't care if it fails.
        ReadFullwidthLatinLetter(cur, strindex);
      } else {
        // It was "as-is" character, not a name reference.
        output += (char)0x81;
        output += type;
        continue;
      }

      int index = ConvertLetterIndexToInt(strindex);
      if (type == LOWER_BYTE_FULLWIDTH_ASTERISK)
        output += memory.Read(StrBank::global_name, index);
      else
        output += memory.Read(StrBank::local_name, index);
    } else {
      CopyOneShiftJisCharacter(cur, output);
    }
  }

  return output;
}

bool TextSystem::CurrentlySkipping() const {
  return kidoku_read_ && skip_mode();
}

// ------------------------------------------------------------------------------
// Resolve an absolute or game-relative filename, then consult indexed assets.
// Siglus FONT_FILE entries often give only a basename for a file under dat/.
static std::optional<fs::path> FindFontAsset(System& system,
                                             const fs::path& file) {
  if (file.empty())
    return std::nullopt;

  const fs::path local =
      fs::path(system.gameexe()("__GAMEPATH").Str().value_or("")) / file;
  std::error_code error;
  if (fs::is_regular_file(local, error))
    return local;

  auto scanner = system.GetAssetScanner();
  if (!scanner || file.is_absolute() || !file.has_extension())
    return std::nullopt;
  std::string extension = file.extension().string().substr(1);
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  auto asset = scanner->FindFile(file.stem().string(), {extension});
  if (asset && fs::is_regular_file(*asset, error))
    return *asset;
  return std::nullopt;
}

using ResolvedFont = std::pair<fs::path, int>;

// Search one font set. A nonempty name requires an exact family match; an empty
// name accepts Fontconfig's default, optionally guided by language coverage.
static std::optional<ResolvedFont> MatchFont(FcConfig* config,
                                             FcSetName set_name,
                                             const std::string& name,
                                             const char* language = nullptr) {
  FcFontSet* fonts = FcConfigGetFonts(config, set_name);
  if (!fonts || fonts->nfont == 0)
    return std::nullopt;

  std::unique_ptr<FcPattern, decltype(&FcPatternDestroy)> pattern(
      FcPatternCreate(), FcPatternDestroy);
  if (!pattern)
    return std::nullopt;
  if (!name.empty())
    FcPatternAddString(pattern.get(), FC_FAMILY,
                       reinterpret_cast<const FcChar8*>(name.c_str()));
  if (language)
    FcPatternAddString(pattern.get(), FC_LANG,
                       reinterpret_cast<const FcChar8*>(language));
  FcConfigSubstitute(config, pattern.get(), FcMatchPattern);
  FcDefaultSubstitute(pattern.get());
  FcResult result;
  std::unique_ptr<FcPattern, decltype(&FcPatternDestroy)> match(
      FcFontSetMatch(config, &fonts, 1, pattern.get(), &result),
      FcPatternDestroy);
  if (!match)
    return std::nullopt;

  // Fontconfig substitutes unavailable families. Reject that substitution here
  // so the caller can try its remaining font sources before choosing a default.
  if (!name.empty()) {
    bool exact = false;
    for (int i = 0;; ++i) {
      FcChar8* family = nullptr;
      if (FcPatternGetString(match.get(), FC_FAMILY, i, &family) !=
          FcResultMatch)
        break;
      if (FcStrCmpIgnoreCase(
              family, reinterpret_cast<const FcChar8*>(name.c_str())) == 0) {
        exact = true;
        break;
      }
    }
    if (!exact)
      return std::nullopt;
  }

  FcChar8* file = nullptr;
  int index = 0;
  if (FcPatternGetString(match.get(), FC_FILE, 0, &file) != FcResultMatch)
    return std::nullopt;
  const fs::path path(reinterpret_cast<const char*>(file));
  std::error_code error;
  if (!fs::is_regular_file(path, error))
    return std::nullopt;
  // A font collection can hold several faces in one file; preserve the selected
  // face index when opening it through SDL_ttf.
  FcPatternGetInteger(match.get(), FC_INDEX, 0, &index);
  return ResolvedFont{path, index};
}

// Try an explicit file, bundled family, installed family, __GAMEFONT fallback,
// then a system default. Return an empty path if every source is unavailable.
static ResolvedFont ResolveFont(System& system, const std::string& name) {
  Gameexe& gexe = system.gameexe();
  if (auto file = FindFontAsset(system, name))
    return {*file, 0};

  std::unique_ptr<FcConfig, decltype(&FcConfigDestroy)> config(
      FcInitLoadConfigAndFonts(), FcConfigDestroy);
  if (config) {
    // Register bundled fonts on this private config, without installing them
    // globally or changing font matching for other games.
    for (const char* root : {"FONT_FILE.", "PRIVATE_FONT_FILE."}) {
      for (auto setting : gexe.Filter(root)) {
        if (!setting.key().ends_with(".NAME"))
          continue;
        if (auto value = setting.Str()) {
          if (auto file = FindFontAsset(system, *value))
            FcConfigAppFontAddFile(
                config.get(),
                reinterpret_cast<const FcChar8*>(file->string().c_str()));
        }
      }
    }
    if (!name.empty()) {
      // Search bundled fonts separately so an installed copy cannot take
      // precedence over the game's own version of the requested family.
      for (FcSetName fonts : {FcSetApplication, FcSetSystem})
        if (auto match = MatchFont(config.get(), fonts, name))
          return *match;
    }
  }

  if (auto gamefont = gexe("__GAMEFONT").Str()) {
    fs::path pth = fs::path(*gamefont);
    std::error_code error;
    if (fs::is_regular_file(pth, error))
      return {pth, 0};
  }

  if (config) {
    // Empty names and unavailable families still need a usable default. Prefer
    // Japanese coverage unless the game explicitly uses western-font mode.
    if (auto match = MatchFont(config.get(), FcSetSystem, "",
                               system.use_western_font() ? "en" : "ja"))
      return *match;
  }

  return {};
}

std::shared_ptr<IFont> TextSystem::LoadFont(int size) {
  const std::string& name = EffectiveFontName(local_config_, global_config_);

  // File resolution is independent of pixel size and synthetic bold styling.
  auto [resolved, inserted] = resolved_fonts_.try_emplace(name);
  if (inserted)
    resolved->second = ResolveFont(system_, name);
  const auto& [file, index] = resolved->second;

  if (!fs::exists(file))
    throw std::runtime_error("Failed to resolve font: " + name);

  // Family aliases can share a loaded font, but size, bold style and collection
  // face each require a distinct SDL_ttf instance.
  auto [it, created] = font_cache_.try_emplace(std::make_tuple(
      file, size, EffectiveFontBold(local_config_, global_config_), index));
  if (created) {
    try {
      it->second = text_impl_->GetFont(
          file, size, EffectiveFontBold(local_config_, global_config_), index);
    } catch (...) {
      // Allow a later attempt to retry instead of returning a cached null font.
      font_cache_.erase(it);
      throw;
    }
  }
  return it->second;
}
