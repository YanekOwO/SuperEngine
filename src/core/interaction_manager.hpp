// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
// -----------------------------------------------------------------------

#pragma once

#include "core/button_action_table.hpp"
#include "core/event.hpp"
#include "core/object.hpp"
#include "utilities/clock.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

class InputListener;
class Stage;
class Group;
class TextWaku;
class TextWindow;

class InteractionManager {
 public:
  InteractionManager(Stage& stage,
                     InputListener& input,
                     ButtonActionTable button_actions);

  void Update();
  struct ButtonPresentation {
    bool visible = true;
    bool enabled = true;
    bool touch_enabled = true;
    bool activated = false;
  };
  enum class MwndButtonEvent { Hit, Push, Decide, Repeat };
  using PresentationHandler =
      std::function<ButtonPresentation(const GraphicsObject&, int slot)>;
  using ButtonEventHandler = std::function<
      void(std::shared_ptr<GraphicsObject>, int slot, MwndButtonEvent)>;
  void SetMwndButtonHandlers(PresentationHandler presentation,
                             ButtonEventHandler event,
                             std::shared_ptr<Clock> clock);
  // Called after InputListener, before message-wait listeners. Returns whether
  // this event belongs to a window control and must be consumed.
  bool HandleMwndButtonEvent(const Event& event);

 private:
  struct MwndButton {
    std::weak_ptr<TextWindow> window;
    std::weak_ptr<GraphicsObject> object;
    TextWaku* waku;
    int slot;
    std::size_t window_generation;
    ParentObjState parent;
    ButtonPresentation presentation;
    bool visible;
    ButtonState inherited_state;
    std::tuple<int, int, int, int, int, int, int, std::size_t> sorter;
  };
  std::vector<MwndButton> CollectMwndButtons(int layer);
  std::optional<MwndButton> RefreshMwndButtons();
  void EmitMwndButton(const MwndButton&, MwndButtonEvent);
  void ApplyMwndButton(const MwndButton& button, bool foreground);

  Stage& stage_;
  InputListener& input_;
  ButtonActionTable button_actions_;
  PresentationHandler mwnd_presentation_;
  ButtonEventHandler mwnd_event_;
  std::shared_ptr<Clock> clock_;
  std::optional<MwndButton> captured_;
  std::weak_ptr<GraphicsObject> hovered_;
  bool captured_enabled_ = false;
  bool swallow_decide_ = false;
  bool consumed_decide_ = false;
  bool last_decide_down_ = false;
  std::optional<Clock::timepoint_t> last_repeat_;

  void UpdateGroup(Group& group, std::optional<int> hit_button_no);
  void UpdateLayer(int layer);
};
