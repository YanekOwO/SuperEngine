// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2013 Elliot Glaysher
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
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
// -----------------------------------------------------------------------

#include "long_operations/button_object_select_long_operation.hpp"

#include "core/object.hpp"
#include "core/object_internal/objdrawer.hpp"
#include "core/stage.hpp"
#include "machine/rlmachine.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"

#include <optional>

ButtonObjectSelectLongOperation::ButtonObjectSelectLongOperation(
    RLMachine& machine,
    int group)
    : machine_(machine),
      group_(group),
      cancelable_(false),
      has_return_value_(false),
      return_value_(-1),
      button_actions_(
          ButtonActionTable::ParseReallive(machine.GetSystem().gameexe())),
      currently_hovering_button_(NULL),
      currently_pressed_button_(NULL) {
  for (GraphicsObject& obj : machine.stage().objects[kLayerFg]) {
    if (obj.Param().IsButton() && obj.Param().GetButtonGroup() == group_) {
      buttons_.emplace_back(&obj, static_cast<GraphicsObject*>(NULL));
    } else if (obj.HasChildren()) {
      for (auto& child : obj.GetChildren()) {
        if (child && child->Param().IsButton() &&
            child->Param().GetButtonGroup() == group_) {
          buttons_.emplace_back(child.get(), &obj);
        }
      }
    }
  }

  // Initialize overrides on all buttons that we'll use.
  for (ButtonPair& button_pair : buttons_) {
    SetButtonOverride(button_pair.first, ButtonState::Normal);
  }
}

ButtonObjectSelectLongOperation::~ButtonObjectSelectLongOperation() {
  // Disable overrides on all graphics objects we've dealt with.
  for (ButtonPair& button_pair : buttons_) {
    button_pair.first->Param().ClearButtonOverrides();
  }
}

void ButtonObjectSelectLongOperation::OnEvent(std::shared_ptr<Event> event) {
  bool result = std::visit(
      [&](const auto& event) -> bool {
        using T = std::decay_t<decltype(event)>;

        if constexpr (std::same_as<T, MouseMotion>) {
          const Point point = event.pos;
          GraphicsObject* hovering_button = NULL;

          for (ButtonPair& button_pair : buttons_) {
            if (button_pair.first->HasDrawer()) {
              GraphicsObjectData* data = button_pair.first->GetDrawer<>();
              std::optional<ParentObjState> parent_state =
                  button_pair.second
                      ? std::make_optional(
                            ParentObjState::BuildFrom(*button_pair.second))
                      : std::nullopt;
              if (data->HitTest(*button_pair.first, point, parent_state))
                hovering_button = button_pair.first;
            }
          }

          if (currently_hovering_button_ != hovering_button) {
            if (currently_hovering_button_) {
              SetButtonOverride(currently_hovering_button_,
                                ButtonState::Normal);

              if (currently_hovering_button_ == currently_pressed_button_)
                currently_pressed_button_ = NULL;
            }

            if (hovering_button)
              SetButtonOverride(hovering_button, ButtonState::Hit);
          }

          currently_hovering_button_ = hovering_button;
        }

        if constexpr (std::same_as<T, MouseDown> || std::same_as<T, MouseUp>) {
          constexpr bool pressed = std::same_as<T, MouseDown>;

          if (event.button == MouseButton::LEFT) {
            if (pressed) {
              currently_pressed_button_ = currently_hovering_button_;
              if (currently_pressed_button_)
                SetButtonOverride(currently_pressed_button_, ButtonState::Push);
            } else {
              if (currently_hovering_button_ &&
                  currently_hovering_button_ == currently_pressed_button_) {
                has_return_value_ = true;
                return_value_ =
                    currently_pressed_button_->Param().GetButtonNumber();
                SetButtonOverride(currently_pressed_button_, ButtonState::Hit);
              }
            }

            // Changes override properties doesn't automatically refresh the
            // screen the way mouse movement does.
            machine_.GetSystem().graphics().ForceRefresh();

            return true;
          } else if (event.button == MouseButton::RIGHT && !pressed &&
                     cancelable_) {
            has_return_value_ = true;
            return_value_ = -1;
          }

          return false;
        }

        return false;
      },
      *event);

  if (result)
    *event = std::monostate();
}

bool ButtonObjectSelectLongOperation::operator()(RLMachine& machine) {
  if (has_return_value_) {
    machine.set_store_register(return_value_);
    return true;
  } else {
    return false;
  }
}

void ButtonObjectSelectLongOperation::SetButtonOverride(GraphicsObject* object,
                                                        ButtonState state) {
  const int action = object->Param().GetButtonAction();
  if (action < 0 ||
      static_cast<std::size_t>(action) >= button_actions_.GetCount())
    return;

  const auto entry = button_actions_.GetEntry(action);
  const ButtonActionTable::State* selected_state = nullptr;
  switch (state) {
    case ButtonState::Normal:
      selected_state = &entry.normal;
      break;
    case ButtonState::Hit:
      selected_state = &entry.hit;
      break;
    case ButtonState::Push:
      selected_state = &entry.push;
      break;
  }

  object->Param().SetButtonOverrides(
      selected_state->pattern, selected_state->rep_pos.x(),
      selected_state->rep_pos.y(), selected_state->rep_tr,
      selected_state->rep_bright, selected_state->rep_dark);
}
