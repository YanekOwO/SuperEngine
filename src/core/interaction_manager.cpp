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
#include "core/object_internal/objdrawer.hpp"
#include "core/stage.hpp"

#include <cstddef>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace {

constexpr int kButtonSelect = 3;
constexpr int kButtonDisable = 4;

struct HitCandidate {
  int z_order;
  int z_layer;
  int z_depth;
  int object_id;
  std::size_t traversal_order;
  int button_no;
};

auto CandidateOrder(const HitCandidate& candidate) {
  return std::tie(candidate.z_order, candidate.z_layer, candidate.z_depth,
                  candidate.object_id, candidate.traversal_order);
}

void VisitButtonCandidates(
    GraphicsObject& object,
    std::optional<ParentObjState> parent,
    int object_id,
    const Point& mouse_pos,
    const StageElement<Group>::layer_type& groups,
    std::size_t& traversal_order,
    std::vector<std::optional<HitCandidate>>& candidates) {
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
      param.GetButtonAction() >= 0 && param.GetButtonState() != kButtonSelect &&
      param.GetButtonState() != kButtonDisable && !param.click_disable &&
      object.GetDrawer().HitTest(object, mouse_pos, parent)) {
    HitCandidate candidate{.z_order = param.z_order,
                           .z_layer = param.z_layer,
                           .z_depth = param.z_depth,
                           .object_id = object_id,
                           .traversal_order = current_traversal_order,
                           .button_no = param.GetButtonNumber()};
    std::optional<HitCandidate>& best =
        candidates[static_cast<std::size_t>(group_no)];
    if (!best || CandidateOrder(*best) < CandidateOrder(candidate))
      best = candidate;
  }

  if (!object.HasChildren())
    return;

  const ParentObjState child_parent = ParentObjState::BuildFrom(object);
  for (auto& child : object.GetChildren()) {
    if (child) {
      VisitButtonCandidates(*child, child_parent, object_id, mouse_pos, groups,
                            traversal_order, candidates);
    }
  }
}

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
  const int action = param.GetButtonAction();
  if (!param.IsButton() || action < 0 ||
      static_cast<std::size_t>(action) >= button_actions.GetCount()) {
    param.ClearButtonOverrides();
  } else {
    const ButtonActionTable::Entry entry = button_actions.GetEntry(action);
    const ButtonActionTable::State& action_state = entry.GetState(state);
    param.SetButtonOverrides(action_state.pattern, action_state.rep_pos.x(),
                             action_state.rep_pos.y(), action_state.rep_tr,
                             action_state.rep_bright, action_state.rep_dark);
  }

  if (!object.HasChildren())
    return;

  std::optional<ButtonState> child_parent_state;
  if (state == ButtonState::Select || state == ButtonState::Disable)
    child_parent_state = state;
  for (auto& child : object.GetChildren()) {
    if (child)
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
  for (int layer = kLayerFg; layer <= kLayerNext; ++layer)
    UpdateLayer(layer);
}

void InteractionManager::UpdateGroup(Group& group,
                                     std::optional<int> hit_button_no) {
  if (group.status == Group::Status::Disabled)
    return;

  group.hit_button_no = hit_button_no;

  const InputListener::State& decide = input_.decide;
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
  std::vector<std::optional<HitCandidate>> candidates(groups.size());

  auto& objects = stage_.ObjectsForLayer(layer);
  std::size_t traversal_order = 0;
  for (std::size_t i = 0; i < objects.size(); ++i)
    if (objects[i])
      VisitButtonCandidates(*objects[i], std::nullopt, static_cast<int>(i),
                            input_.mouse_pos, groups, traversal_order,
                            candidates);

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
}
