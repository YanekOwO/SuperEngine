// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2009 Elliot Glaysher
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

#include "long_operations/select_long_operation.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "core/gameexe.hpp"
#include "core/rlevent_listener.hpp"
#include "libreallive/expression.hpp"
#include "libreallive/parser.hpp"
#include "log/domain_logger.hpp"
#include "machine/long_operation.hpp"
#include "machine/rlmachine.hpp"
#include "systems/graphics_system.hpp"
#include "systems/renderable.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"
#include "systems/event_system.hpp"
#include "utilities/string_utilities.hpp"

using libreallive::CommandElement;
using libreallive::SelectElement;
using std::placeholders::_1;

// -----------------------------------------------------------------------
// SelectLongOperation
// -----------------------------------------------------------------------
SelectLongOperation::SelectLongOperation(RLMachine& machine,
                                         const SelectElement& commandElement)
    : machine_(machine), return_value_(-1) {
  for (SelectElement::Param const& param : commandElement.raw_params()) {
    Option o;
    o.shown = true;
    o.enabled = true;
    o.use_colour = false;

    std::string evaluated_native =
        libreallive::EvaluatePRINT(machine, param.text);
    o.str = cp932toUTF8(evaluated_native, machine.GetTextEncoding());

    for (auto const& condition : param.cond_parsed) {
      switch (condition.effect) {
        // TODO(erg): Someday, I might need to support the other options, but
        // for now, I've never seen anything other than hide.
        case SelectElement::OPTION_HIDE: {
          bool value = false;
          if (condition.condition != "") {
            const char* location = condition.condition.c_str();
            libreallive::Expression condition(
                libreallive::ExpressionParser::GetExpression(location));
            value = !condition->GetIntegerValue(machine);
          }

          o.shown = value;
          break;
        }
        case SelectElement::OPTION_TITLE: {
          bool enabled = false;
          if (condition.condition != "") {
            const char* location = condition.condition.c_str();
            libreallive::Expression condition(
                libreallive::ExpressionParser::GetExpression(location));
            enabled = !condition->GetIntegerValue(machine);
          }

          bool use_colour = false;
          int colour_index = 0;
          if (!enabled && condition.effect_argument != "") {
            const char* location = condition.effect_argument.c_str();
            libreallive::Expression effect_argument(
                libreallive::ExpressionParser::GetExpression(location));
            colour_index = !effect_argument->GetIntegerValue(machine);
            use_colour = true;
          }

          o.enabled = enabled;
          o.use_colour = use_colour;
          o.colour_index = colour_index;
          break;
        }
        default:
          std::cerr << "Unsupported option in select statement "
                    << "(condition: "
                    << libreallive::ParsableToPrintableString(
                           condition.condition)
                    << ", effect: " << condition.effect << ", effect_argument: "
                    << libreallive::ParsableToPrintableString(
                           condition.effect_argument)
                    << ")" << std::endl;
          break;
      }
    }

    options_.push_back(o);
  }
}

SelectLongOperation::~SelectLongOperation() {}

void SelectLongOperation::SelectByIndex(int num) {
  if (machine_.GetSystem().sound().HasSe(1))
    machine_.GetSystem().sound().PlaySe(1);
  machine_.GetSystem().TakeSelectionSnapshot(machine_);
  return_value_ = num;
}

bool SelectLongOperation::SelectByText(const std::string& str) {
  std::vector<Option>::iterator it =
      find_if(options_.begin(), options_.end(),
              [&](Option& o) { return o.str == str; });

  if (it != options_.end() && it->shown) {
    SelectByIndex(distance(options_.begin(), it));
    return true;
  }

  return false;
}

std::vector<std::string> SelectLongOperation::GetOptions() const {
  std::vector<std::string> opt;
  for (Option const& option : options_) {
    opt.push_back(option.str);
  }

  return opt;
}

bool SelectLongOperation::operator()(RLMachine& machine) {
  if (return_value_ != -1) {
    machine.set_store_register(return_value_);
    return true;
  } else {
    return false;
  }
}

// -----------------------------------------------------------------------
// NormalSelectLongOperation
// -----------------------------------------------------------------------
NormalSelectLongOperation::NormalSelectLongOperation(
    RLMachine& machine,
    const libreallive::SelectElement& commandElement)
    : SelectLongOperation(machine, commandElement),
      text_window_(machine.GetSystem().text().GetCurrentMwnd()) {
  static DomainLogger logger("NormalSelectLongOperation");

  TextSystem& ts = machine.GetSystem().text();
  ts.set_in_selection_mode(true);
  text_window_->SetVisible(true);

  std::vector<TextWindow::Selection> choices;
  choices.reserve(options_.size());
  for (size_t i = 0; i < options_.size(); ++i) {
    auto& opt = options_[i];
    if (!opt.shown)
      continue;
    if (opt.use_colour) {
      logger(Severity::Warn)
          << "Colour for normal selections is not implemented (option = "
          << opt.str << ", color_index = " << opt.colour_index << ')';
    }
    if (!opt.enabled) {
      logger(Severity::Warn) << "Disabled state for normal selections is not "
                                "implemented (option = "
                             << opt.str << ')';
    }

    choices.emplace_back(opt.str, [this, idx = i]() { SelectByIndex(idx); });
  }

  text_window_->StartSelection(std::move(choices));
}

NormalSelectLongOperation::~NormalSelectLongOperation() {
  text_window_->EndSelection();
  machine_.GetSystem().text().set_in_selection_mode(false);
}

void NormalSelectLongOperation::OnEvent(std::shared_ptr<Event> event) {
  const bool result = std::visit(
      [&](const auto& event) -> bool {
        using T = std::decay_t<decltype(event)>;

        if constexpr (std::same_as<T, MouseMotion>)
          this->OnMouseMotion(event.pos);

        if constexpr (std::same_as<T, MouseDown> || std::same_as<T, MouseUp>)
          return this->OnMouseButtonStateChanged(event.button,
                                                 std::same_as<T, MouseDown>);

        return false;
      },
      *event);

  if (result)
    *event = std::monostate();
}

void NormalSelectLongOperation::OnMouseMotion(const Point& pos) {
  // Tell the text system about the move
  machine_.GetSystem().text().SetMousePosition(pos);
}

bool NormalSelectLongOperation::OnMouseButtonStateChanged(
    MouseButton mouseButton,
    bool pressed) {
  switch (mouseButton) {
    case MouseButton::LEFT: {
      Point pos = machine_.GetSystem().rlEvent().GetCursorPos();
      machine_.GetSystem().text().HandleMouseClick(pos, pressed);
      return true;
      break;
    }
    case MouseButton::RIGHT: {
      if (pressed) {
        machine_.GetSystem().ShowSyscomMenu(machine_);
        return true;
      }
      break;
    }
    default:
      break;
  }

  return false;
}

// -----------------------------------------------------------------------
// ButtonSelectLongOperation
// -----------------------------------------------------------------------
ButtonSelectLongOperation::ButtonSelectLongOperation(
    RLMachine& machine,
    const libreallive::SelectElement& commandElement,
    int selbtn_set)
    : SelectLongOperation(machine, commandElement),
      highlighted_item_(-1),
      normal_frame_(0),
      select_frame_(0),
      push_frame_(0),
      dontsel_frame_(0),
      mouse_down_(false) {
  // Load all the data about this #SELBTN from the Gameexe.ini file.
  Gameexe& gexe = machine.GetSystem().gameexe();
  GameexeInterpretObject selbtn(gexe("SELBTN", selbtn_set));

  std::vector<int> vec = selbtn("BASEPOS").ToIntVec();
  basepos_x_ = vec.at(0);
  basepos_y_ = vec.at(1);

  vec = selbtn("REPPOS").ToIntVec();
  reppos_x_ = vec.at(0);
  reppos_y_ = vec.at(1);

  vec = selbtn("CENTERING").ToIntVec();
  int center_x = vec.at(0);
  int center_y = vec.at(1);

  moji_size_ = selbtn("MOJISIZE").Int().value();

  // Retrieve the parameters needed to render as a color mask.
  std::shared_ptr<TextWindow> window =
      machine.GetSystem().text().GetCurrentMwnd();
  window_bg_colour_ = window->GetColour();
  window_filter_ = window->GetIsFilter();

  int default_colour_num_ = selbtn("MOJIDEFAULTCOL").Int().value();
  int select_colour_num_ = selbtn("MOJISELECTCOL").Int().value();
  if (default_colour_num_ == select_colour_num_)
    select_colour_num_ = 1;      // For CLANNAD
  if (select_colour_num_ == -1)  // For little busters
    select_colour_num_ = default_colour_num_;

  GraphicsSystem& gs = machine.GetSystem().graphics();
  if (auto name = selbtn("NAME").Str(); name.has_value() && !name->empty())
    name_surface_ = gs.GetSurfaceNamed(name.value());
  if (auto name = selbtn("BACK").Str(); name.has_value() && !name->empty())
    back_surface_ = gs.GetSurfaceNamed(name.value());

  if (auto tmp = selbtn("NORMAL").IntVec()) {
    normal_frame_ = tmp->at(0);
    normal_frame_offset_ = Point(tmp->at(1), tmp->at(2));
  }
  if (auto tmp = selbtn("SELECT").IntVec()) {
    select_frame_ = tmp->at(0);
    select_frame_offset_ = Point(tmp->at(1), tmp->at(2));
  }
  if (auto tmp = selbtn("PUSH").IntVec()) {
    push_frame_ = tmp->at(0);
    push_frame_offset_ = Point(tmp->at(1), tmp->at(2));
  }
  if (auto tmp = selbtn("DONTSEL").IntVec()) {
    dontsel_frame_ = tmp->at(0);
    dontsel_frame_offset_ = Point(tmp->at(1), tmp->at(2));
  }

  // Pick the correct font colour
  vec = gexe("COLOR_TABLE", default_colour_num_).ToIntVec();
  RGBColour default_colour(vec.at(0), vec.at(1), vec.at(2));
  vec = gexe("COLOR_TABLE", select_colour_num_).ToIntVec();
  RGBColour select_colour(vec.at(0), vec.at(1), vec.at(2));
  vec = gexe("COLOR_TABLE", 255).ToIntVec();
  RGBColour shadow_colour(vec.at(0), vec.at(1), vec.at(2));

  // Build graphic representations of the choices to display to the user.
  TextSystem& ts = machine.GetSystem().text();
  int shown_option_count = std::count_if(options_.begin(), options_.end(),
                                         [&](Option& o) { return o.shown; });

  // Calculate out the bounding rectangles for all the options.
  Size screen_size = machine.GetSystem().graphics().screen_size();
  int baseposx = 0;
  if (center_x) {
    int totalwidth = ((shown_option_count - 1) * reppos_x_);
    if (back_surface_)
      totalwidth += back_surface_->GetSize().width();
    else
      totalwidth += name_surface_->GetPattern(normal_frame_).rect.width();
    baseposx = (screen_size.width() / 2) - (totalwidth / 2);
  } else {
    baseposx = basepos_x_;
  }

  int baseposy = 0;
  if (center_y) {
    int totalheight = ((shown_option_count - 1) * reppos_y_);
    if (back_surface_)
      totalheight += back_surface_->GetSize().height();
    baseposy = (screen_size.height() / 2) - (totalheight / 2);
  } else {
    baseposy = basepos_y_;
  }

  for (size_t i = 0; i < options_.size(); i++) {
    if (options_[i].shown) {
      const std::string& text = options_[i].str;

      RGBColour text_colour = default_colour;
      RGBColour text_selection_colour = select_colour;

      if (options_[i].use_colour) {
        std::vector<int> vec =
            gexe("COLOR_TABLE", options_[i].colour_index).ToIntVec();
        text_colour = RGBColour(vec.at(0), vec.at(1), vec.at(2));

        if (!options_[i].enabled)
          text_selection_colour = text_colour;
      }

      ButtonOption o;
      o.id = i;
      o.enabled = options_[i].enabled;
      o.default_surface =
          ts.RenderText(text, moji_size_, 0, 0, text_colour, &shadow_colour, 0);
      o.select_surface = ts.RenderText(
          text, moji_size_, 0, 0, text_selection_colour, &shadow_colour, 0);
      if (back_surface_) {
        o.bounding_rect = Rect(baseposx, baseposy, back_surface_->GetSize());
      } else {
        o.bounding_rect =
            Rect(baseposx, baseposy, name_surface_->GetPattern(0).rect.size());
      }

      buttons_.push_back(o);

      baseposx += reppos_x_;
      baseposy += reppos_y_;
    }
  }
}

ButtonSelectLongOperation::~ButtonSelectLongOperation() = default;

void ButtonSelectLongOperation::OnEvent(std::shared_ptr<Event> event) {
  const bool result = std::visit(
      [&](const auto& event) -> bool {
        using T = std::decay_t<decltype(event)>;

        if constexpr (std::same_as<T, MouseMotion>)
          this->OnMouseMotion(event.pos);

        if constexpr (std::same_as<T, MouseDown> || std::same_as<T, MouseUp>)
          return this->OnMouseButtonStateChanged(event.button,
                                                 std::same_as<T, MouseDown>);

        return false;
      },
      *event);

  if (result)
    *event = std::monostate();
}

void ButtonSelectLongOperation::OnMouseMotion(const Point& p) {
  for (size_t i = 0; i < buttons_.size(); i++) {
    if (buttons_[i].bounding_rect.Contains(p)) {
      if (options_[i].enabled) {
        if (highlighted_item_ != i && machine_.GetSystem().sound().HasSe(0)) {
          machine_.GetSystem().sound().PlaySe(0);
        }

        highlighted_item_ = i;
      }
      return;
    }
  }

  highlighted_item_ = -1;
}

bool ButtonSelectLongOperation::OnMouseButtonStateChanged(
    MouseButton mouseButton,
    bool pressed) {
  switch (mouseButton) {
    case MouseButton::LEFT: {
      mouse_down_ = pressed;
      if (!pressed) {
        Point pos = machine_.GetSystem().rlEvent().GetCursorPos();
        for (size_t i = 0; i < buttons_.size(); i++) {
          if (buttons_[i].bounding_rect.Contains(pos) && options_[i].enabled) {
            SelectByIndex(buttons_[i].id);
            break;
          }
        }

        return true;
      }
      break;
    }
    case MouseButton::RIGHT: {
      if (pressed) {
        machine_.GetSystem().ShowSyscomMenu(machine_);
        return true;
      }
      break;
    }
    default:
      break;
  }

  return false;
}

void ButtonSelectLongOperation::Render() {
  for (size_t i = 0; i < buttons_.size(); i++) {
    int frame = normal_frame_;
    Point offset = normal_frame_offset_;
    if (!buttons_[i].enabled) {
      offset = dontsel_frame_offset_;
      frame = dontsel_frame_;
    } else if (i == highlighted_item_) {
      if (mouse_down_) {
        offset = push_frame_offset_;
        frame = push_frame_;
      } else {
        offset = select_frame_offset_;
        frame = select_frame_;
      }
    }

    Rect bounding_rect = buttons_[i].bounding_rect;
    bounding_rect = Rect(bounding_rect.origin() + offset, bounding_rect.size());

    if (back_surface_) {
      back_surface_->RenderToScreenAsColorMask(back_surface_->GetRect(),
                                               bounding_rect, window_bg_colour_,
                                               window_filter_);
    }
    if (name_surface_) {
      name_surface_->RenderToScreen(name_surface_->GetPattern(frame).rect,
                                    bounding_rect);
    }

    if (i == highlighted_item_) {
      RenderTextSurface(buttons_[i].select_surface, bounding_rect);
    } else {
      RenderTextSurface(buttons_[i].default_surface, bounding_rect);
    }
  }
}

void ButtonSelectLongOperation::RenderTextSurface(
    const std::shared_ptr<SDLSurface>& text_surface,
    const Rect& bounding_rect) {
  // Render the correct text in the correct place.
  Rect text_bounding_rect = text_surface->GetSize().CenteredIn(bounding_rect);
  text_surface->RenderToScreen(text_surface->GetRect(), text_bounding_rect);
}
