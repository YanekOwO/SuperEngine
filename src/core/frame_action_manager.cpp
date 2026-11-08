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

#include "core/frame_action_manager.hpp"

#include "core/game_config.hpp"
#include "core/stage.hpp"
#include "systems/text_waku.hpp"
#include "systems/text_window.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

using ms = std::chrono::milliseconds;

namespace {

// Snapshot channels and children: a callback can reset its enclosing waku.
template <typename Visitor>
void VisitActions(const std::shared_ptr<FrameAction>& global_action,
                  FrameActionList& global_channels,
                  Stage& stage,
                  Visitor visitor) {
  auto visit_action = [&](std::shared_ptr<FrameAction> action,
                          ActionOwner owner, bool background) {
    if (action)
      visitor(ActionReference{std::move(action), std::move(owner)}, background);
  };
  auto visit_object = [&](auto&& self, std::shared_ptr<GraphicsObject> object,
                          bool background) -> void {
    if (!object)
      return;
    visit_action(object->frame_action_, object, background);
    const auto channels = object->frame_action_list_;
    for (const auto& action : channels)
      visit_action(action, object, background);
    const auto children = object->GetChildren();
    for (const auto& child : children)
      self(self, child, background);
  };

  // Callbacks may replace a waku or promote stage lists. Retain every root
  // before invoking the first callback, and never iterate a destroyed waku.
  std::vector<std::pair<ActionOwner, bool>> roots;
  for (int layer : {kLayerBg, kLayerFg, kLayerNext})
    for (const auto& object : stage.objects[layer])
      roots.emplace_back(object, layer == kLayerBg);
  for (int layer : {kLayerBg, kLayerFg, kLayerNext}) {
    for (const auto& window : stage.mwnd[layer]) {
      if (!window)
        continue;
      for (auto* waku : {window->GetTextboxWaku(), window->GetNameboxWaku()}) {
        if (!waku)
          continue;
        for (const auto* list :
             {&waku->button_objects_, &waku->faces_, &waku->objects_})
          for (const auto& object : *list)
            roots.emplace_back(object, layer == kLayerBg);
      }
    }
  }
  const auto channels = global_channels;
  visit_action(global_action, {}, false);
  for (const auto& action : channels)
    visit_action(action, {}, false);
  for (const auto& [object, background] : roots)
    visit_object(visit_object, object, background);
}

FrameAction& ResolveAction(const ActionReference& reference) {
  if (!reference.action)
    throw std::invalid_argument("frame-action handle is null");
  return *reference.action;
}

}  // namespace

FrameActionManager::FrameActionManager(Stage& stage,
                                       LocalConfig& local_config,
                                       std::size_t global_channel_count)
    : stage_(stage),
      local_config_(local_config),
      global_channels_(global_channel_count) {}

void FrameActionManager::SetExec(Exec exec) { exec_ = std::move(exec); }

void FrameActionManager::RunFinal(const ActionReference& reference) {
  auto& action = ResolveAction(reference);
  const ScriptCall call = action.call;
  action.active = false;
  action.end_action = true;
  action.GetCounter()->Set(std::max(0, action.end_time));
  // Final callbacks do not start a replacement in this slot.
  try {
    exec_(reference, call);
  } catch (...) {
    action.Reset();
    throw;
  }
  action.Reset();
}

void FrameActionManager::Start(ActionReference reference,
                               int duration,
                               bool real_time,
                               ScriptCall call) {
  auto& action = ResolveAction(reference);
  End(reference);
  action.call = std::move(call);
  action.active = true;
  action.end_time = duration;
  std::shared_ptr<Counter> counter = action.GetCounter();
  if (real_time)
    counter->StartReal();
  else
    counter->StartGame();
}

void FrameActionManager::End(ActionReference reference) {
  auto& action = ResolveAction(reference);
  if (action.IsActive())
    RunFinal(reference);
  else
    action.Reset();
}

void FrameActionManager::AdvanceAndDispatch(ms real_delta, ms game_delta) {
  const auto real_time = std::max(ms::zero(), real_delta);
  const auto game_time = std::max(ms::zero(), game_delta);

  const bool globals_stopped =
      local_config_.time_stop || local_config_.frame_action_time_stop;
  const bool objects_stopped =
      local_config_.time_stop || local_config_.stage_time_stop;
  VisitActions(global_action_, global_channels_, stage_,
               [&](const ActionReference& reference, bool) {
                 auto& action = *reference.action;
                 if (action.counter &&
                     !(reference.owner ? objects_stopped : globals_stopped))
                   action.counter->Advance(game_time, real_time);
               });
  VisitActions(global_action_, global_channels_, stage_,
               [&](const ActionReference& reference, bool) {
                 auto& action = *reference.action;
                 if (action.IsActive())
                   exec_(reference, action.call);
               });
  VisitActions(global_action_, global_channels_, stage_,
               [&](const ActionReference& reference, bool background) {
                 auto& action = *reference.action;
                 if (!background && action.IsActive() && action.end_time >= 0 &&
                     action.counter && action.counter->Get() >= action.end_time)
                   RunFinal(reference);
               });
}

void FrameActionManager::CancelGlobals() {
  if (global_action_)
    global_action_->Reset();
  for (auto& action : global_channels_)
    if (action)
      action->Reset();
}

void FrameActionManager::FinishAll() {
  VisitActions(
      global_action_, global_channels_, stage_,
      [this](const ActionReference& reference, bool) { End(reference); });
}

void FrameActionManager::CancelAll() {
  VisitActions(global_action_, global_channels_, stage_,
               [](const ActionReference& reference, bool) {
                 reference.action->Reset();
               });
}
