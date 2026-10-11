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

#include "systems/text_factory.hpp"

#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"
#include "systems/text_waku_normal.hpp"
#include "systems/text_waku_type4.hpp"
#include "systems/text_window.hpp"
#include "systems/text_window_button.hpp"

#include <algorithm>
#include <memory>

namespace {

Rect ResolveButtonRect(const Rect& window_rect,
                       const MwndConfig::Button& config,
                       const Size& surface_size) {
  const Size size = config.explicit_size.value_or(surface_size);
  int x = window_rect.x();
  int y = window_rect.y();
  const bool align_own_size = config.explicit_size.has_value();
  switch (config.position_base) {
    case 1:
      x = window_rect.x2() - config.position.x() -
          (align_own_size ? size.width() : 0);
      y += config.position.y();
      break;
    case 2:
      x += config.position.x();
      y = window_rect.y2() - config.position.y() -
          (align_own_size ? size.height() : 0);
      break;
    case 3:
      x = window_rect.x2() - config.position.x() -
          (align_own_size ? size.width() : 0);
      y = window_rect.y2() - config.position.y() -
          (align_own_size ? size.height() : 0);
      break;
    case 0:
    default:
      x += config.position.x();
      y += config.position.y();
      break;
  }
  return Rect(Point(x, y), size);
}

}  // namespace

TextFactory::TextFactory(const MwndConfig& config) : config_(config) {}

std::unique_ptr<TextWaku> TextFactory::CreateWaku(System& system,
                                                  TextWindow& window,
                                                  int setno,
                                                  int no) {
  const MwndConfig::Waku& config = config_.GetWaku(setno, no);
  if (config.style == MwndConfig::WakuStyle::Stretch)
    return CreateWakuType4(system, window, config);
  return CreateWakuNormal(system, window, config);
}

std::unique_ptr<TextWaku> TextFactory::CreateWakuNormal(
    System& system,
    TextWindow& window,
    const MwndConfig::Waku& config) {
  using namespace std::chrono_literals;
  auto result = std::make_unique<TextWakuNormal>();
  GraphicsSystem& graphics = system.graphics();
  TextSystem& text = system.text();

  if (!config.main_file.empty())
    result->SetWakuMain(graphics.GetSurfaceNamed(config.main_file));
  if (!config.filter_file.empty())
    result->SetWakuBacking(graphics.GetSurfaceNamed(config.filter_file));
  result->SetFilterConfig(config.filter_margin, config.filter_colour,
                          config.use_config_colour, config.use_config_opacity);
  result->SetRenderFilter(config.draw_filter);
  const MwndConfig::Icon* key_icon = config_.GetIcon(config.key_icon_no);
  const MwndConfig::Icon* page_icon = config_.GetIcon(config.page_icon_no);
  std::shared_ptr<SDLSurface> key_surface, page_surface;
  if (key_icon && !key_icon->file.empty())
    key_surface = graphics.GetSurfaceNamed(key_icon->file);
  if (page_icon && !page_icon->file.empty())
    page_surface = graphics.GetSurfaceNamed(page_icon->file);
  result->SetWaitIcons(key_surface, key_icon ? *key_icon : MwndConfig::Icon{},
                       page_surface,
                       page_icon ? *page_icon : MwndConfig::Icon{},
                       config.icon_position_type, config.icon_position_base,
                       config.icon_position, system.event().GetClock());

  const Size text_size = window.GetTextSurfaceSize();
  const Rect window_rect = window.GetWindowRect(result->GetSize(text_size));
  for (const auto& button_config : config.buttons) {
    std::shared_ptr<SDLSurface> surface;
    Size surface_size = button_config.explicit_size.value_or(Size());
    if (!button_config.file.empty()) {
      surface = graphics.GetSurfaceNamed(button_config.file);
      if (surface && !button_config.explicit_size) {
        const int pattern =
            std::clamp(button_config.cut_no, 0,
                       std::max(surface->GetNumPatterns() - 1, 0));
        surface_size = surface->GetPattern(pattern).rect.size();
      }
    }
    const bool valid_siglus_action =
        button_config.slot_id < 0 ||
        (button_config.action_no >= 0 &&
         static_cast<std::size_t>(button_config.action_no) <
             config_.button_actions().GetCount());
    auto button = std::make_unique<TextWindowButton>(
        system.event().GetClock(), valid_siglus_action,
        ResolveButtonRect(window_rect, button_config, surface_size));
    button->SetUsePatternGeometry(!button_config.explicit_size.has_value());
    button->SetAlphaHitTest(button_config.slot_id >= 0);
    button->on_hit_ = [&text, button_config] {
      text.PlayMwndButtonSound(button_config, TextSystem::MwndButtonSound::Hit);
    };
    button->on_pressed_ = [&text, button_config] {
      text.PlayMwndButtonSound(button_config,
                               TextSystem::MwndButtonSound::Push);
    };
    button->on_release_ = [&text, button_config] {
      text.PlayMwndButtonSound(button_config,
                               TextSystem::MwndButtonSound::Decide);
      text.ExecuteMwndButton(button_config);
    };
    button->SetSurface(surface, button_config.slot_id >= 0
                                    ? button_config.cut_no
                                    : button_config.reallive_pattern);
    if (valid_siglus_action && button_config.action_no >= 0 &&
        static_cast<std::size_t>(button_config.action_no) <
            config_.button_actions().GetCount()) {
      button->SetActionTableEntry(
          config_.button_actions().GetEntry(button_config.action_no),
          button_config.cut_no);
    }
    if (button_config.action == MwndConfig::ButtonAction::BackPage ||
        button_config.action == MwndConfig::ButtonAction::ForwardPage) {
      button->time_between_invocations_ = 250ms;
      button->on_repeat_ = [&text, button_config] {
        text.ExecuteMwndButton(button_config);
      };
    }

    button->on_update_ = [&text, button_config](TextWindowButton& item) {
      const auto state = text.GetMwndButtonPresentation(button_config);
      item.SetPresentation(state.visible, state.enabled, state.touch_enabled,
                           state.activated);
    };
    button->on_update_(*button);
    result->AddButton(button_config.name, std::move(button));
  }
  return result;
}

std::unique_ptr<TextWaku> TextFactory::CreateWakuType4(
    System& system,
    TextWindow& window,
    const MwndConfig::Waku& config) {
  auto result = std::make_unique<TextWakuType4>();
  if (!config.main_file.empty())
    result->SetMainWaku(system.graphics().GetSurfaceNamed(config.main_file));
  result->SetArea(config.filter_margin.y(), config.filter_margin.y2(),
                  config.filter_margin.x(), config.filter_margin.x2());
  result->SetFilter(config.filter_file.empty()
                        ? nullptr
                        : system.graphics().GetSurfaceNamed(config.filter_file),
                    config.filter_colour, config.use_config_colour,
                    config.use_config_opacity);
  auto overlay_base = CreateWakuNormal(system, window, config);
  auto* overlay = dynamic_cast<TextWakuNormal*>(overlay_base.release());
  if (overlay) {
    overlay->SetWakuMain(nullptr);
    overlay->SetWakuBacking(nullptr);
    overlay->SetRenderFilter(false);
    result->SetOverlay(std::unique_ptr<TextWakuNormal>(overlay));
  }
  return result;
}
