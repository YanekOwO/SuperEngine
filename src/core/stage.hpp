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

#pragma once

#include "core/effect.hpp"
#include "core/group.hpp"
#include "core/object.hpp"
#include "core/quake.hpp"
#include "utilities/lazy_array.hpp"

#include <array>
#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

[[maybe_unused]] constexpr int kLayerFg = 0;
[[maybe_unused]] constexpr int kLayerBg = 1;
[[maybe_unused]] constexpr int kLayerNext = 2;

template <class T>
struct StageElement {
  using pointer_type = std::shared_ptr<T>;
  using layer_type = std::vector<pointer_type>;
  using storage_type = std::array<layer_type, 3>;

  explicit StageElement(size_t size = 0)
      : storage{layer_type(size), layer_type(size), layer_type(size)} {}

  layer_type& operator[](int layer) {
    if (layer < kLayerFg || layer > kLayerNext)
      throw std::runtime_error("Invalid layer number");
    return storage[static_cast<size_t>(layer)];
  }

  const layer_type& operator[](int layer) const {
    if (layer < kLayerFg || layer > kLayerNext)
      throw std::runtime_error("Invalid layer number");
    return storage[static_cast<size_t>(layer)];
  }

  pointer_type Get(int layer, size_t pos) {
    return Get(layer, pos, [] { return std::make_shared<T>(); });
  }

  template <class Factory>
  pointer_type Get(int layer, size_t pos, Factory factory) {
    pointer_type& element = (*this)[layer].at(pos);
    if (!element)
      element = factory();
    return element;
  }

  void Assign(int layer, size_t pos, T value) {
    (*this)[layer].at(pos) = std::make_shared<T>(std::move(value));
  }

  void Move(int dst_layer, size_t dst, int src_layer, size_t src) {
    if (dst_layer == src_layer && dst == src)
      return;
    (*this)[dst_layer].at(dst) = std::move((*this)[src_layer].at(src));
  }

  bool Exists(int layer, size_t pos) const {
    return static_cast<bool>((*this)[layer].at(pos));
  }

  void Delete(int layer, size_t pos) { (*this)[layer].at(pos).reset(); }

  void Resize(int layer, size_t size) { (*this)[layer].resize(size); }

  size_t Size(int layer) const { return (*this)[layer].size(); }

  void Clear() {
    for (auto& layer : storage)
      for (auto& element : layer)
        element.reset();
  }

  storage_type storage;
};

class TextWindow;

class Stage {
 public:
  struct TransitionRenderRange {
    // Inclusive bounds in lexicographic (order, layer) sorter order.
    std::pair<int, int> begin;
    std::pair<int, int> end;
  };

  Stage(int object_size = 0,
        int effect_size = 0,
        int group_size = 0,
        int quake_size = 0);

  // RealLive uses foreground only; Siglus has three fixed-size window lists.
  StageElement<TextWindow> mwnd;
  int mwnd_order = 1;

  // Foreground, background and next objects
  StageElement<GraphicsObject> objects;

  // Foreground, background and next groups
  StageElement<Group> groups;

  // Foreground, background and next stage effects.
  StageElement<Effect> effects;

  // Foreground, background and next stage quakes.
  StageElement<QuakeState> quakes;

  // Foreground objects (at the time of the last save)
  LazyArray<GraphicsObject> saved_foreground_objects;

  // Background objects (at the time of the last save)
  LazyArray<GraphicsObject> saved_background_objects;

  // List of commands in RealLive bytecode to rebuild the graphics stack at the
  // current moment.
  std::deque<std::string> graphics_stack;

  // Commands to rebuild the graphics stack (at the time of the last savepoint)
  std::deque<std::string> saved_graphics_stack;

 public:
  void Reset();

  // A process where the front and back buffers swap, updating the display to
  // show objects prepared in the back buffer. Documented as "Wipe operation".
  void Wipe();
  void Wipe(int begin_order, int end_order, int begin_layer, int end_layer);

  double foreground_render_alpha() const { return foreground_render_alpha_; }
  double next_render_alpha() const { return next_render_alpha_; }
  const std::optional<TransitionRenderRange>& transition_render_range() const {
    return transition_render_range_;
  }
  void SetTransitionRenderRange(int begin_order,
                                int end_order,
                                int begin_layer,
                                int end_layer);
  void SetTransitionRenderAlpha(double foreground_alpha, double next_alpha);
  void ClearTransitionRenderState();

  // Object getters
  // layer == OBJ_FG for foreground, OBJ_BG for background, OBJ_NEXT for next.
  GraphicsObject& GetObject(int layer, int obj_number);
  size_t GetFreeObjectId(int layer);
  void SetObject(int layer, int obj_number, GraphicsObject&& object);

  // Remove the entire graphics object.
  void RemoveObject(int layer, size_t obj_number);

  // Frees the object data (but not the parameters).
  void FreeObjectData(int obj_number);
  void FreeLayerObjectData(int layer);
  void FreeAllObjectData();

  // Resets/reinitializes all the object parameters without deleting the loaded
  // graphics object data.
  void InitializeObjectParams(int obj_number);
  void InitializeAllObjectParams();

  void Execute();

  // Adds |command|, the serialized form of a bytecode used by calling the
  // BytecodeElement::data().
  void AddGraphicsStackCommand(std::string command);

  // Takes a snapshot of the current object state. This snapshot is saved
  // instead of the current state of the graphics, since RealLive is a savepoint
  // based system.
  //
  // (This operation isn't exceptionally expensive; internally GraphicsObject
  // has multiple copy-on-write data structs to make this and object promotion a
  // relativly cheap operation.)
  void TakeSavepointSnapshot();

  inline const StageElement<GraphicsObject>::layer_type& ObjectsForLayer(
      int layer) const {
    return objects[layer];
  }
  inline StageElement<GraphicsObject>::layer_type& ObjectsForLayer(int layer) {
    return objects[layer];
  }

  inline const StageElement<Effect>::layer_type& EffectsForLayer(
      int layer) const {
    return effects[layer];
  }
  inline StageElement<Effect>::layer_type& EffectsForLayer(int layer) {
    return effects[layer];
  }
  Effect& GetEffect(int layer, int effect_number);
  void ResizeEffects(int layer, int size);

  inline const StageElement<QuakeState>::layer_type& QuakesForLayer(
      int layer) const {
    return quakes[layer];
  }
  inline StageElement<QuakeState>::layer_type& QuakesForLayer(int layer) {
    return quakes[layer];
  }
  std::shared_ptr<QuakeState> GetQuake(int layer, int index);
  void AdvanceQuakes(std::int64_t elapsed_ms);
  bool HasActiveQuakes() const;

 private:
  std::optional<TransitionRenderRange> transition_render_range_;
  double foreground_render_alpha_ = 1.0;
  double next_render_alpha_ = 0.0;
};
