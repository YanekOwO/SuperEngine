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

#include "core/interaction_manager.hpp"

#include "core/button_action_table.hpp"
#include "core/group.hpp"
#include "core/input.hpp"
#include "core/object.hpp"
#include "core/object_internal/objdrawer.hpp"
#include "core/stage.hpp"
#include "systems/text_waku.hpp"
#include "systems/text_window.hpp"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace {

constexpr int kButtonSelect = static_cast<int>(ButtonState::Select);
constexpr int kButtonDisable = static_cast<int>(ButtonState::Disable);

struct HitCandidate {
  int owner_order;
  int owner_layer;
  int display_order;
  int z_order;
  int z_layer;
  int z_depth;
  int object_id;
  std::size_t traversal_order;
  int button_no;

  std::strong_ordering operator<=>(const HitCandidate& o) const {
    // greater means better candidate
    return std::tie(owner_order, owner_layer, display_order, z_order, z_layer,
                    z_depth, object_id, traversal_order) <=>
           std::tie(o.owner_order, o.owner_layer, o.display_order, o.z_order,
                    o.z_layer, o.z_depth, o.object_id, o.traversal_order);
  }
};

// Visitor to dfs traverse object tree, selecting best candidate to handle mouse
// state for each group
struct CandidateVisitor {
  const Point mouse_pos;
  const std::vector<std::shared_ptr<Group>>& groups;  // group data
  std::size_t traversal_order = 0;

  // output, best candidates for each group
  std::vector<std::optional<HitCandidate>> candidates;

  CandidateVisitor(Point pos, const std::vector<std::shared_ptr<Group>>& grp)
      : mouse_pos(pos),
        groups(grp),
        traversal_order(0),
        candidates(grp.size()) {}

  void Visit(GraphicsObject& object,
             std::optional<ParentObjState> parent,
             int object_id,
             int display_order,
             std::optional<std::pair<int, int>> owner_sorter = std::nullopt) {
    const std::size_t current_traversal_order = traversal_order++;
    const ObjectParameter& param = object.Param();
    if (!param.visible())
      return;

    const int group_no = param.GetButtonGroup();
    const bool has_active_group =
        group_no >= 0 && static_cast<std::size_t>(group_no) < groups.size() &&
        groups[static_cast<std::size_t>(group_no)] &&
        groups[static_cast<std::size_t>(group_no)]->status !=
            Group::Status::Disabled;
    if (has_active_group && object.HasDrawer() && param.IsButton() &&
        param.GetButtonAction() >= 0 &&
        param.GetButtonState() != kButtonSelect &&
        param.GetButtonState() != kButtonDisable && !param.click_disable &&
        object.GetDrawer().HitTest(object, mouse_pos, parent)) {
      const auto sorter =
          owner_sorter.value_or(std::make_pair(param.z_order, param.z_layer));
      HitCandidate candidate{.owner_order = sorter.first,
                             .owner_layer = sorter.second,
                             .display_order = display_order,
                             .z_order = param.z_order,
                             .z_layer = param.z_layer,
                             .z_depth = param.z_depth,
                             .object_id = object_id,
                             .traversal_order = current_traversal_order,
                             .button_no = param.GetButtonNumber()};
      std::optional<HitCandidate>& best =
          candidates[static_cast<std::size_t>(group_no)];
      if (!best || *best < candidate)
        best = candidate;
    }

    if (!object.HasChildren())
      return;

    ParentObjState child_parent = ParentObjState::BuildFrom(object);
    if (parent)
      child_parent.render_state =
          RenderState::Fold(child_parent.render_state, parent->render_state);
    for (auto& child : object.GetChildren()) {
      if (!child)
        continue;
      Visit(*child, child_parent, object_id, display_order, owner_sorter);
    }
  }
};

ButtonState ResolveButtonState(const ObjectParameter& param,
                               const StageElement<Group>::layer_type& groups,
                               std::optional<ButtonState> parent) {
  if (parent)
    return *parent;
  if (param.GetButtonState() == kButtonSelect)
    return ButtonState::Select;
  if (param.GetButtonState() == kButtonDisable)
    return ButtonState::Disable;

  const int group_no = param.GetButtonGroup();
  if (group_no < 0 || static_cast<std::size_t>(group_no) >= groups.size() ||
      !groups[static_cast<std::size_t>(group_no)])
    return ButtonState::Normal;

  const Group& group = *groups[static_cast<std::size_t>(group_no)];
  const int button_no = param.GetButtonNumber();
  if ((group.result == Group::Result::Decided &&
       group.decided_button_no == button_no) ||
      group.pushed_button_no == button_no)
    return ButtonState::Push;
  if (group.hit_button_no == button_no)
    return ButtonState::Hit;
  return ButtonState::Normal;
}

void ApplyButtonOverrides(
    GraphicsObject& object,
    const StageElement<Group>::layer_type& groups,
    const ButtonActionTable& button_actions,
    std::optional<ButtonState> parent_state = std::nullopt) {
  ObjectParameter& param = object.Param();
  const ButtonState state = ResolveButtonState(param, groups, parent_state);
  object.Param().button.real_state = state;
  object.Param().button.hit_state = ButtonState::Normal;
  const int group_no = param.GetButtonGroup();
  if (group_no >= 0 && static_cast<std::size_t>(group_no) < groups.size() &&
      groups[group_no]) {
    const auto& group = *groups[group_no];
    if (group.hit_button_no == param.GetButtonNumber())
      object.Param().button.hit_state = ButtonState::Hit;
    if (group.pushed_button_no == param.GetButtonNumber())
      object.Param().button.hit_state = ButtonState::Push;
  }
  const int action = param.GetButtonAction();
  if (!param.IsButton() || action < 0 ||
      static_cast<std::size_t>(action) >= button_actions.GetCount()) {
    param.ClearButtonOverrides();
    object.Param().button.hit_state = object.Param().button.real_state =
        ButtonState::Normal;
  } else {
    const ButtonActionTable::Entry entry = button_actions.GetEntry(action);
    const ButtonActionTable::State& action_state = entry.GetState(state);
    // Siglus action templates add to pat_no; RealLive uses absolute overrides.
    param.SetButtonOverrides(
        param.pattern_number + param.button.cut_no + action_state.pattern,
        action_state.rep_pos.x(), action_state.rep_pos.y(), action_state.rep_tr,
        action_state.rep_bright, action_state.rep_dark);
  }

  if (!object.HasChildren())
    return;

  std::optional<ButtonState> child_parent_state;
  if (state == ButtonState::Select || state == ButtonState::Disable)
    child_parent_state = state;
  for (auto& child : object.GetChildren()) {
    if (!child)
      continue;
    ApplyButtonOverrides(*child, groups, button_actions, child_parent_state);
  }
}

}  // namespace

InteractionManager::InteractionManager(Stage& stage,
                                       InputListener& input,
                                       ButtonActionTable button_actions)
    : stage_(stage),
      input_(input),
      button_actions_(std::move(button_actions)) {}

void InteractionManager::Update() {
  RefreshMwndButtons();
  if (captured_ && captured_enabled_ && clock_ && last_repeat_) {
    const auto object = captured_->object.lock();
    if (object && hovered_.lock() == object && object->Param().button.config &&
        (object->Param().button.config->action ==
             MwndConfig::ButtonAction::BackPage ||
         object->Param().button.config->action ==
             MwndConfig::ButtonAction::ForwardPage) &&
        clock_->GetTime() - *last_repeat_ >= std::chrono::milliseconds(250)) {
      last_repeat_ = clock_->GetTime();
      EmitMwndButton(*captured_, MwndButtonEvent::Repeat);
    }
  }
  for (int layer = kLayerFg; layer <= kLayerNext; ++layer)
    UpdateLayer(layer);
  consumed_decide_ = false;
}

void InteractionManager::UpdateGroup(Group& group,
                                     std::optional<int> hit_button_no) {
  if (group.status == Group::Status::Disabled)
    return;

  group.hit_button_no = hit_button_no;

  InputListener::State decide = input_.decide;
  if (consumed_decide_ || swallow_decide_) {
    decide = {};
    group.pressed_button_no.reset();
  }
  if (decide.on_down)
    group.pressed_button_no = group.hit_button_no;

  if (!decide.down && (decide.on_up || decide.down_up)) {
    const std::optional<int> pressed =
        group.pressed_button_no ? group.pressed_button_no : group.hit_button_no;
    const bool should_decide =
        pressed && group.hit_button_no && *pressed == *group.hit_button_no;
    group.pressed_button_no.reset();
    if (should_decide) {
      group.status = Group::Status::Disabled;
      group.cancel_enabled = false;
      group.decided_button_no = *pressed;
      group.result = Group::Result::Decided;
      group.result_button_no = *pressed;
      group.ClearTransientInteraction();
      return;
    }
  }

  if (decide.down && group.pressed_button_no && group.hit_button_no &&
      *group.pressed_button_no == *group.hit_button_no) {
    group.pushed_button_no = group.pressed_button_no;
  } else {
    group.pushed_button_no.reset();
  }

  const InputListener::State& cancel = input_.cancel;
  if (group.cancel_enabled && !cancel.down &&
      (cancel.on_up || cancel.down_up)) {
    const int result_button = group.hit_button_no.value_or(-1);
    group.status = Group::Status::Disabled;
    group.cancel_enabled = false;
    group.decided_button_no = -1;
    group.result = Group::Result::Canceled;
    group.result_button_no = result_button;
    group.ClearTransientInteraction();
  }
}

void InteractionManager::UpdateLayer(int layer) {
  auto& groups = stage_.groups[layer];
  CandidateVisitor visitor(input_.mouse_pos, groups);

  auto& objects = stage_.ObjectsForLayer(layer);
  for (std::size_t i = 0; i < objects.size(); ++i) {
    if (!objects[i])
      continue;
    visitor.Visit(*objects[i], std::nullopt, static_cast<int>(i), 0);
  }

  int display_order = 1;
  for (const auto& window : stage_.mwnd[layer]) {
    if (window) {
      const auto visit_waku = [&](TextWaku* waku, const Point& origin,
                                  int order) {
        const auto parent = TextWaku::ObjectParent(origin);
        for (std::size_t i = 0; i < waku->objects_.size(); ++i)
          if (waku->objects_[i])
            visitor.Visit(*waku->objects_[i], parent, static_cast<int>(i),
                          order, window->sorter());
      };
      if (window->IsTextboxWakuVisible())
        visit_waku(window->GetTextboxWaku(), window->GetWindowRect().origin(),
                   display_order);
      if (window->IsNameboxWakuVisible())
        visit_waku(window->GetNameboxWaku(),
                   window->GetNameboxWakuRect().origin(), display_order + 1);
    }
    display_order += 2;
  }

  auto candidates = std::move(visitor.candidates);

  for (std::size_t i = 0; i < groups.size(); ++i) {
    if (!groups[i])
      continue;
    std::optional<int> hit_button_no;
    if (candidates[i])
      hit_button_no = candidates[i]->button_no;
    UpdateGroup(*groups[i], hit_button_no);
  }

  for (auto& object : objects)
    if (object)
      ApplyButtonOverrides(*object, groups, button_actions_);

  for (const auto& window : stage_.mwnd[layer]) {
    if (!window)
      continue;
    for (auto* waku : {window->GetTextboxWaku(), window->GetNameboxWaku()}) {
      if (!waku)
        continue;
      for (const auto& object : waku->objects_)
        if (object)
          ApplyButtonOverrides(*object, groups, button_actions_);
    }
  }
  for (const auto& button : CollectMwndButtons(layer))
    ApplyMwndButton(button, layer == kLayerFg);
}

void InteractionManager::SetMwndButtonHandlers(PresentationHandler presentation,
                                               ButtonEventHandler event,
                                               std::shared_ptr<Clock> clock) {
  mwnd_presentation_ = std::move(presentation);
  mwnd_event_ = std::move(event);
  clock_ = std::move(clock);
}

std::vector<InteractionManager::MwndButton>
InteractionManager::CollectMwndButtons(int layer) {
  std::vector<MwndButton> result;
  std::size_t traversal = 0;
  const auto windows = stage_.mwnd[layer];
  for (std::size_t window_id = 0; window_id < windows.size(); ++window_id) {
    const auto& window = windows[window_id];
    if (!window)
      continue;
    const auto collect = [&](TextWaku* waku, const Rect& rect, bool visible,
                             int kind) {
      if (!waku)
        return;
      waku->button_visible_.resize(waku->button_objects_.size(), true);
      for (std::size_t slot = 0; slot < waku->button_objects_.size(); ++slot) {
        const auto& root = waku->button_objects_[slot];
        if (!root)
          continue;
        auto presentation = mwnd_presentation_ ? mwnd_presentation_(*root, slot)
                                               : ButtonPresentation{};
        waku->button_visible_[slot] = presentation.visible;
        const auto visit =
            [&](auto&& self, const std::shared_ptr<GraphicsObject>& object,
                ParentObjState parent, ButtonPresentation policy, bool drawn,
                ButtonState inherited_state) -> void {
          if (!object)
            return;
          const auto& param = object->Param();
          drawn = drawn && policy.visible && param.visible();
          if (object != root && mwnd_presentation_) {
            const auto child = mwnd_presentation_(*object, slot);
            policy.visible = policy.visible && child.visible;
            policy.enabled = policy.enabled && child.enabled;
            policy.touch_enabled = policy.touch_enabled && child.touch_enabled;
            policy.activated = child.activated;
            drawn = drawn && child.visible;
          }
          result.push_back(MwndButton{
              .window = window,
              .object = object,
              .waku = waku,
              .slot = static_cast<int>(slot),
              .window_generation = window->capture_generation(),
              .parent = parent,
              .presentation = policy,
              .visible = drawn,
              .inherited_state = inherited_state,
              .sorter = {window->sorter().first, window->sorter().second,
                         static_cast<int>(window_id) * 2 + kind,
                         root->Param().z_order, root->Param().z_layer,
                         root->Param().z_depth, static_cast<int>(slot),
                         traversal++}});
          ApplyMwndButton(result.back(), layer == kLayerFg);
          if (!policy.enabled || param.GetButtonState() == kButtonDisable)
            inherited_state = ButtonState::Disable;
          else if (param.GetButtonState() == kButtonSelect)
            inherited_state = ButtonState::Select;
          ParentObjState child_parent = ParentObjState::BuildFrom(*object);
          child_parent.render_state =
              RenderState::Fold(child_parent.render_state, parent.render_state);
          const auto children = object->GetChildren();
          for (const auto& child : children)
            self(self, child, child_parent, policy, drawn, inherited_state);
        };
        visit(visit, root, waku->ButtonParent(slot, rect), presentation,
              visible, ButtonState::Normal);
      }
    };
    collect(window->GetTextboxWaku(), window->GetWindowRect(),
            window->IsTextboxWakuVisible(), 0);
    if (window->GetNameboxWaku())
      collect(window->GetNameboxWaku(), window->GetNameboxWakuRect(),
              window->IsNameboxWakuVisible(), 1);
  }
  return result;
}

void InteractionManager::ApplyMwndButton(const MwndButton& button,
                                         bool foreground) {
  const auto object = button.object.lock();
  if (!object)
    return;
  auto& param = object->Param();
  object->Param().button.visible = button.presentation.visible;
  const int action = param.GetButtonAction();
  if (!param.IsButton() || action < 0 ||
      static_cast<std::size_t>(action) >= button_actions_.GetCount()) {
    object->Param().button.hit_state = object->Param().button.real_state =
        ButtonState::Normal;
    param.ClearButtonOverrides();
    return;
  }
  ButtonState hit = ButtonState::Normal;
  if (foreground && button.visible && hovered_.lock() == object)
    hit = captured_ && captured_->object.lock() == object ? ButtonState::Push
                                                          : ButtonState::Hit;
  object->Param().button.hit_state = hit;
  ButtonState state = ButtonState::Normal;
  if (!button.presentation.enabled || param.GetButtonState() == kButtonDisable)
    state = ButtonState::Disable;
  else if (param.GetButtonState() == kButtonSelect)
    state = ButtonState::Select;
  else if (button.inherited_state != ButtonState::Normal)
    state = button.inherited_state;
  else if (button.presentation.touch_enabled)
    state = hit != ButtonState::Normal
                ? hit
                : (button.presentation.activated ? ButtonState::Select
                                                 : ButtonState::Normal);
  object->Param().button.real_state = state;
  const auto drawing = button_actions_.GetEntry(action).GetState(state);
  param.SetButtonOverrides(
      param.pattern_number + param.button.cut_no + drawing.pattern,
      drawing.rep_pos.x(), drawing.rep_pos.y(), drawing.rep_tr,
      drawing.rep_bright, drawing.rep_dark);
}

void InteractionManager::EmitMwndButton(const MwndButton& button,
                                        MwndButtonEvent event) {
  if (auto object = button.object.lock(); object && mwnd_event_)
    mwnd_event_(std::move(object), button.slot, event);
}

std::optional<InteractionManager::MwndButton>
InteractionManager::RefreshMwndButtons() {
  auto buttons = CollectMwndButtons(kLayerFg);
  if (captured_) {
    const auto object = captured_->object.lock();
    const auto window = captured_->window.lock();
    auto found =
        std::find_if(buttons.begin(), buttons.end(), [&](const auto& b) {
          return b.object.lock() == object && b.window.lock() == window &&
                 b.waku == captured_->waku &&
                 b.window_generation == captured_->window_generation &&
                 b.visible && object && object->HasDrawer() &&
                 object->Param().IsButton();
        });
    if (found == buttons.end()) {
      captured_.reset();
      last_repeat_.reset();
    } else {
      captured_ = *found;
      const auto& p = object->Param();
      captured_enabled_ = captured_enabled_ && found->presentation.enabled &&
                          found->presentation.touch_enabled &&
                          !p.click_disable &&
                          found->inherited_state == ButtonState::Normal &&
                          p.GetButtonState() != kButtonSelect &&
                          p.GetButtonState() != kButtonDisable;
    }
  }
  // Resolve drawing before hit testing, so script changes to pat_no, action,
  // scale and clipping affect both the displayed image and the hit geometry.
  for (const auto& button : buttons)
    ApplyMwndButton(button, true);
  std::optional<MwndButton> hit;
  for (const auto& button : buttons) {
    const auto object = button.object.lock();
    if (!object || !button.visible)
      continue;
    const auto& param = object->Param();
    const int action = param.GetButtonAction();
    if (param.IsButton() && action >= 0 &&
        static_cast<std::size_t>(action) < button_actions_.GetCount() &&
        !param.click_disable && object->HasDrawer() &&
        object->GetDrawer().HitTest(*object, input_.mouse_pos, button.parent) &&
        (!hit || hit->sorter < button.sorter))
      hit = button;
  }
  const auto old_hover = hovered_.lock();
  const auto object = hit ? hit->object.lock() : nullptr;
  hovered_ = object;
  for (const auto& button : buttons)
    ApplyMwndButton(button, true);
  if (object != old_hover && hit && hit->presentation.enabled &&
      hit->presentation.touch_enabled &&
      hit->inherited_state == ButtonState::Normal &&
      object->Param().GetButtonState() == 0)
    EmitMwndButton(*hit, MwndButtonEvent::Hit);
  return hit;
}

bool InteractionManager::HandleMwndButtonEvent(const Event& event) {
  if (std::holds_alternative<MouseMotion>(event)) {
    RefreshMwndButtons();
    return false;
  }
  const bool decide_event = std::visit(
      [](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::same_as<T, MouseDown> || std::same_as<T, MouseUp>)
          return item.button == MouseButton::LEFT;
        else if constexpr (std::same_as<T, KeyDown> || std::same_as<T, KeyUp>)
          return IsDecideKey(item.code);
        else
          return false;
      },
      event);
  if (!decide_event)
    return false;
  auto hit = RefreshMwndButtons();
  const bool down = input_.decide.down;
  const bool edge = down != last_decide_down_;
  last_decide_down_ = down;
  bool handled = swallow_decide_ || hit.has_value();
  if (edge && down && hit) {
    captured_ = hit;
    const auto object = hit->object.lock();
    captured_enabled_ = hit->presentation.enabled &&
                        hit->presentation.touch_enabled &&
                        hit->inherited_state == ButtonState::Normal &&
                        object->Param().GetButtonState() == 0;
    swallow_decide_ = true;
    if (captured_enabled_) {
      if (clock_)
        last_repeat_ = clock_->GetTime();
      EmitMwndButton(*hit, MwndButtonEvent::Push);
    }
  } else if (edge && !down) {
    const auto captured = captured_;
    captured_.reset();
    swallow_decide_ = false;
    last_repeat_.reset();
    if (captured && captured_enabled_ && hit &&
        captured->object.lock() == hit->object.lock())
      EmitMwndButton(*captured, MwndButtonEvent::Decide);
    captured_enabled_ = false;
  }
  consumed_decide_ = consumed_decide_ || handled;
  RefreshMwndButtons();
  return handled;
}
