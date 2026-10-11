// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
// Copyright (C) 2026 Serina Sakurai
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

#include "core/stage.hpp"

#include "core/object_internal/objdrawer.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

Stage::Stage(int object_size, int effect_size, int group_size, int quake_size)
    : objects(object_size),
      groups(group_size),
      effects(effect_size),
      quakes(quake_size),
      saved_foreground_objects(object_size),
      saved_background_objects(object_size) {}

void Stage::Reset() {
  objects.Clear();
  groups.Clear();
  effects.Clear();
  quakes.Clear();
  ClearTransitionRenderState();
}

void Stage::Wipe() {
  Wipe(std::numeric_limits<int>::min(), std::numeric_limits<int>::max(),
       std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
}

void Stage::Wipe(int begin_order,
                 int end_order,
                 int begin_layer,
                 int end_layer) {
  // TODO(siglus): Promote mwnd, btnsel, and world stage state
  // when those core Siglus element implementations exist.
  auto& foreground_objects = objects[kLayerFg];
  auto& background_objects = objects[kLayerBg];
  auto& next_objects = objects[kLayerNext];
  for (auto& object : next_objects)
    object.reset();

  auto InWipeRange = [begin = std::make_pair(begin_order, begin_layer),
                      end = std::make_pair(end_order, end_layer)](
                         std::pair<int, int> now) -> bool {
    return begin <= now && now <= end;
  };
  auto ObjOrd = [](const ObjectParameter& param) {
    return std::make_pair(param.z_order, param.z_layer);
  };

  const size_t count =
      std::min({foreground_objects.size(), background_objects.size(),
                next_objects.size()});
  for (size_t i = 0; i < count; ++i) {
    const bool fg_exists = static_cast<bool>(foreground_objects[i]);
    const bool bg_exists = static_cast<bool>(background_objects[i]);

    const bool front_in_range =
        fg_exists &&
        (foreground_objects[i]->HasDrawer() ||
         foreground_objects[i]->HasChildren()) &&
        InWipeRange(ObjOrd(foreground_objects[i]->Param()));
    const bool back_has_content =
        bg_exists && (background_objects[i]->HasDrawer() ||
                      background_objects[i]->HasChildren());
    if (!front_in_range && !back_has_content)
      continue;

    if (fg_exists)
      objects.Assign(kLayerNext, i, foreground_objects[i]->Clone());

    const bool replace_front =
        back_has_content ||
        (bg_exists && background_objects[i]->Param().wipe_erase) ||
        (front_in_range && foreground_objects[i]->Param().wipe_copy == 0);
    if (!replace_front)
      continue;

    if (bg_exists) {
      objects.Move(kLayerFg, i, kLayerBg, i);
    } else {
      objects.Assign(kLayerFg, i, GraphicsObject());
    }
  }

  auto& foreground_groups = groups[kLayerFg];
  auto& background_groups = groups[kLayerBg];
  auto& next_groups = groups[kLayerNext];
  for (auto& group : next_groups)
    group.reset();
  const size_t group_count =
      std::max(foreground_groups.size(), background_groups.size());
  foreground_groups.resize(group_count);
  background_groups.resize(group_count);
  next_groups.resize(group_count);

  for (size_t i = 0; i < group_count; ++i) {
    const Group default_group;
    const Group& foreground =
        foreground_groups[i] ? *foreground_groups[i] : default_group;
    if (!InWipeRange(std::make_pair(foreground.order, foreground.layer)))
      continue;

    if (foreground_groups[i]) {
      groups.Assign(kLayerNext, i, foreground);
      next_groups[i]->ClearTransientInteraction();
    }

    if (background_groups[i]) {
      groups.Move(kLayerFg, i, kLayerBg, i);
      foreground_groups[i]->ClearTransientInteraction();
    } else {
      foreground_groups[i].reset();
    }
  }

  auto& foreground_effects = effects[kLayerFg];
  auto& background_effects = effects[kLayerBg];
  auto& next_effects = effects[kLayerNext];
  for (auto& effect : next_effects)
    effect.reset();
  const size_t effect_count =
      std::min({foreground_effects.size(), background_effects.size(),
                next_effects.size()});
  for (size_t i = 0; i < effect_count; ++i) {
    const bool foreground_exists = static_cast<bool>(foreground_effects[i]);
    const bool background_exists = static_cast<bool>(background_effects[i]);

    if (foreground_exists)
      effects.Assign(kLayerNext, i, *foreground_effects[i]);

    bool replace_foreground =
        !foreground_exists || foreground_effects[i]->wipe_copy == 0;
    if (background_exists && background_effects[i]->wipe_erase != 0) {
      replace_foreground = true;
    }
    if (!replace_foreground)
      continue;

    if (background_exists) {
      effects.Move(kLayerFg, i, kLayerBg, i);
    } else {
      foreground_effects[i].reset();
    }
  }

  // Quakes are stage-wide render state in legacy Siglus. Unlike objects and
  // groups, every slot participates even in a sorter-limited wipe.
  auto& foreground_quakes = quakes[kLayerFg];
  auto& background_quakes = quakes[kLayerBg];
  auto& next_quakes = quakes[kLayerNext];
  for (auto& quake : next_quakes)
    quake.reset();
  const size_t quake_count = std::min(
      {foreground_quakes.size(), background_quakes.size(), next_quakes.size()});
  for (size_t i = 0; i < quake_count; ++i) {
    if (foreground_quakes[i])
      quakes.Assign(kLayerNext, i, *foreground_quakes[i]);

    if (background_quakes[i]) {
      quakes.Move(kLayerFg, i, kLayerBg, i);
    } else {
      foreground_quakes[i].reset();
    }
  }
}

void Stage::SetTransitionRenderAlpha(double foreground_alpha,
                                     double next_alpha) {
  foreground_render_alpha_ = std::clamp(foreground_alpha, 0.0, 1.0);
  next_render_alpha_ = std::clamp(next_alpha, 0.0, 1.0);
}

void Stage::ClearTransitionRenderState() {
  foreground_render_alpha_ = 1.0;
  next_render_alpha_ = 0.0;
}

StageEffect& Stage::GetEffect(int layer, int effect_number) {
  if (effect_number < 0)
    throw std::out_of_range("Negative stage effect index");
  return effects.Get(layer, static_cast<size_t>(effect_number));
}

void Stage::ResizeEffects(int layer, int size) {
  if (size < 0)
    throw std::runtime_error("Negative stage effect count");
  EffectsForLayer(layer).resize(static_cast<size_t>(size));
}

std::weak_ptr<QuakeState> Stage::GetQuakeHandle(int layer, int quake_number) {
  if (quake_number < 0)
    throw std::out_of_range("Negative stage quake index");
  return quakes.GetHandle(layer, static_cast<size_t>(quake_number));
}

void Stage::AdvanceQuakes(std::int64_t elapsed_ms) {
  for (auto& layer : quakes.storage)
    for (auto& quake : layer)
      if (quake)
        quake->Advance(elapsed_ms);
}

bool Stage::HasActiveQuakes() const {
  for (const auto& layer : quakes.storage)
    for (const auto& quake : layer)
      if (quake && quake->IsActive())
        return true;
  return false;
}

GraphicsObject& Stage::GetObject(int layer, int obj_number) {
  if (obj_number < 0)
    throw std::out_of_range("Negative object index");
  return objects.Get(layer, static_cast<size_t>(obj_number));
}

size_t Stage::GetFreeObjectId(int layer) {
  auto& objects = ObjectsForLayer(layer);

  for (size_t i = 0, end = objects.size(); i < end; ++i) {
    if (!objects[i])
      return i;
  }

  throw std::runtime_error("No free object slots");
}

void Stage::SetObject(int layer, int obj_number, GraphicsObject&& object) {
  if (obj_number < 0)
    throw std::out_of_range("Negative object index");
  objects.Assign(layer, static_cast<size_t>(obj_number), std::move(object));
}

void Stage::RemoveObject(int layer, size_t obj_number) {
  ObjectsForLayer(layer).at(obj_number).reset();
}

void Stage::FreeObjectData(int obj_number) {
  objects.Get(kLayerFg, obj_number).FreeObjectData();
  objects.Get(kLayerBg, obj_number).FreeObjectData();
}

void Stage::FreeAllObjectData() {
  FreeLayerObjectData(kLayerFg);
  FreeLayerObjectData(kLayerBg);
  FreeLayerObjectData(kLayerNext);
}

void Stage::FreeLayerObjectData(int layer) {
  auto& objects = ObjectsForLayer(layer);
  for (auto& obj : objects) {
    if (obj)
      obj->FreeObjectData();
  }
}

void Stage::InitializeObjectParams(int obj_number) {
  objects.Get(kLayerFg, obj_number).InitializeParams();
  objects.Get(kLayerBg, obj_number).InitializeParams();
}

void Stage::InitializeAllObjectParams() {
  for (auto& object : objects[kLayerFg])
    if (object)
      object->InitializeParams();

  for (auto& object : objects[kLayerBg])
    if (object)
      object->InitializeParams();
}

void Stage::Execute() {
  for (auto& layer_objects : objects.storage) {
    for (auto& obj : layer_objects) {
      if (!obj)
        continue;
      obj->Execute();
      obj->ExecuteMutators();
    }
  }
}

void Stage::AddGraphicsStackCommand(std::string command) {
  graphics_stack.emplace_back(std::move(command));

  // RealLive only allows 127 commands to be on the stack so game programmers
  // can be lazy and not clear it.
  if (graphics_stack.size() > 127)
    graphics_stack.pop_front();
}

void Stage::TakeSavepointSnapshot() {
  auto& foreground = objects[kLayerFg];
  auto& background = objects[kLayerBg];

  saved_foreground_objects.Clear();
  for (size_t i = 0; i < foreground.size(); ++i)
    if (foreground[i])
      saved_foreground_objects[i] = foreground[i]->Clone();

  saved_background_objects.Clear();
  for (size_t i = 0; i < background.size(); ++i)
    if (background[i])
      saved_background_objects[i] = background[i]->Clone();

  saved_graphics_stack = graphics_stack;
}
