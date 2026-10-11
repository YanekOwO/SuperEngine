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

Stage::Stage(int object_size, int effect_size, int group_size)
    : objects(object_size),
      groups(group_size),
      effects(effect_size),
      saved_foreground_objects(object_size),
      saved_background_objects(object_size) {}

void Stage::Reset() {
  objects.Clear();
  groups.Clear();
  effects.Clear();
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
  // TODO(siglus): Promote mwnd, btnsel, world, and quake stage state
  // when those core Siglus element implementations exist.
  LazyArray<GraphicsObject>& foreground_objects = objects[kLayerFg];
  LazyArray<GraphicsObject>& background_objects = objects[kLayerBg];
  LazyArray<GraphicsObject>& next_objects = objects[kLayerNext];
  next_objects.Clear();

  auto InWipeRange = [begin = std::make_pair(begin_order, begin_layer),
                      end = std::make_pair(end_order, end_layer)](
                         std::pair<int, int> now) -> bool {
    return begin <= now && now <= end;
  };
  auto ObjOrd = [](const ObjectParameter& param) {
    return std::make_pair(param.z_order, param.z_layer);
  };

  const size_t count =
      std::min({foreground_objects.Size(), background_objects.Size(),
                next_objects.Size()});
  for (size_t i = 0; i < count; ++i) {
    const bool fg_exists = foreground_objects.Exists(i);
    const bool bg_exists = background_objects.Exists(i);

    const bool front_in_range =
        fg_exists &&
        InWipeRange(ObjOrd(foreground_objects.At(i).value().Param()));
    const bool back_participates =
        bg_exists && (background_objects[i].HasDrawer() ||
                      background_objects[i].HasChildren() ||
                      background_objects[i].Param().wipe_erase != 0);
    if (!front_in_range && !back_participates)
      continue;

    if (fg_exists)
      next_objects[i] = foreground_objects[i].Clone();

    const bool replace_front =
        back_participates || (front_in_range && fg_exists &&
                              foreground_objects[i].Param().wipe_copy == 0);
    if (!replace_front)
      continue;

    if (back_participates) {
      foreground_objects[i] = std::move(background_objects[i]);
      background_objects.DeleteAt(i);
    } else {
      foreground_objects[i].FreeDataAndInitializeParams();
    }
  }

  LazyArray<Group>& foreground_groups = groups[kLayerFg];
  LazyArray<Group>& background_groups = groups[kLayerBg];
  LazyArray<Group>& next_groups = groups[kLayerNext];
  next_groups.Clear();
  const size_t group_count =
      std::max(foreground_groups.Size(), background_groups.Size());
  foreground_groups.Resize(group_count);
  background_groups.Resize(group_count);
  next_groups.Resize(group_count);

  for (size_t i = 0; i < group_count; ++i) {
    const Group default_group;
    const Group& foreground = foreground_groups.Exists(i)
                                  ? foreground_groups.At(i).value()
                                  : default_group;
    if (!InWipeRange(std::make_pair(foreground.order, foreground.layer)))
      continue;

    if (foreground_groups.Exists(i)) {
      next_groups[i] = foreground;
      next_groups[i].ClearTransientInteraction();
    }

    if (background_groups.Exists(i)) {
      foreground_groups[i] = std::move(background_groups[i]);
      foreground_groups[i].ClearTransientInteraction();
      background_groups.DeleteAt(i);
    } else {
      foreground_groups.DeleteAt(i);
    }
  }

  LazyArray<StageEffect>& foreground_effects = effects[kLayerFg];
  LazyArray<StageEffect>& background_effects = effects[kLayerBg];
  LazyArray<StageEffect>& next_effects = effects[kLayerNext];
  next_effects.Clear();
  const size_t effect_count =
      std::min({foreground_effects.Size(), background_effects.Size(),
                next_effects.Size()});
  for (size_t i = 0; i < effect_count; ++i) {
    const bool foreground_exists = foreground_effects.Exists(i);
    const bool background_exists = background_effects.Exists(i);

    if (foreground_exists)
      next_effects[i] = foreground_effects[i];

    bool replace_foreground =
        !foreground_exists || foreground_effects.At(i).value().wipe_copy == 0;
    if (background_exists && background_effects.At(i).value().wipe_erase != 0) {
      replace_foreground = true;
    }
    if (!replace_foreground)
      continue;

    if (background_exists) {
      foreground_effects[i] = std::move(background_effects[i]);
      background_effects.DeleteAt(i);
    } else {
      foreground_effects.DeleteAt(i);
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
  return EffectsForLayer(layer)[static_cast<size_t>(effect_number)];
}

void Stage::ResizeEffects(int layer, int size) {
  if (size < 0)
    throw std::runtime_error("Negative stage effect count");
  EffectsForLayer(layer).Resize(static_cast<size_t>(size));
}

GraphicsObject& Stage::GetObject(int layer, int obj_number) {
  return ObjectsForLayer(layer)[obj_number];
}

size_t Stage::GetFreeObjectId(int layer) {
  LazyArray<GraphicsObject>& objects = ObjectsForLayer(layer);

  for (size_t i = 0, end = objects.Size(); i < end; ++i) {
    if (!objects.Exists(i))
      return i;
  }

  throw std::runtime_error("No free object slots");
}

void Stage::SetObject(int layer, int obj_number, GraphicsObject&& object) {
  ObjectsForLayer(layer)[obj_number] = std::move(object);
}

void Stage::RemoveObject(int layer, size_t obj_number) {
  ObjectsForLayer(layer).DeleteAt(obj_number);
}

void Stage::FreeObjectData(int obj_number) {
  objects[kLayerFg][obj_number].FreeObjectData();
  objects[kLayerBg][obj_number].FreeObjectData();
}

void Stage::FreeAllObjectData() {
  FreeLayerObjectData(kLayerFg);
  FreeLayerObjectData(kLayerBg);
  FreeLayerObjectData(kLayerNext);
}

void Stage::FreeLayerObjectData(int layer) {
  LazyArray<GraphicsObject>& objects = ObjectsForLayer(layer);
  for (auto& obj : objects)
    obj.FreeObjectData();
}

void Stage::InitializeObjectParams(int obj_number) {
  objects[kLayerFg][obj_number].InitializeParams();
  objects[kLayerBg][obj_number].InitializeParams();
}

void Stage::InitializeAllObjectParams() {
  for (GraphicsObject& object : objects[kLayerFg])
    object.InitializeParams();

  for (GraphicsObject& object : objects[kLayerBg])
    object.InitializeParams();
}

void Stage::Execute() {
  for (auto& layer_objects : objects.storage) {
    for (auto& obj : layer_objects) {
      obj.Execute();
      obj.ExecuteMutators();
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
  for (auto it = foreground.begin(), end = foreground.end(); it != end; ++it) {
    saved_foreground_objects[it.pos()] = it->Clone();
  }

  saved_background_objects.Clear();
  for (auto it = background.begin(), end = background.end(); it != end; ++it) {
    saved_background_objects[it.pos()] = it->Clone();
  }

  saved_graphics_stack = graphics_stack;
}
