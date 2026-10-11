// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Sakurai Serina
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

#include "systems/text_waku.hpp"

#include "core/object.hpp"
#include "core/rect.hpp"
#include "systems/scene_renderer.hpp"

#include <algorithm>
#include <stdexcept>
#include <tuple>
#include <utility>

TextWaku::TextWaku(bool should_center_w, bool should_center_h)
    : center_w_(should_center_w), center_h_(should_center_h) {}

TextWaku::TextWaku(const TextWaku& source)
    : center_w_(source.center_w_),
      center_h_(source.center_h_),
      objects_(source.objects_.size()),
      button_objects_(source.button_objects_.size()),
      button_config_(source.button_config_),
      button_visible_(source.button_visible_),
      renderer_(source.renderer_) {
  for (std::size_t i = 0; i < objects_.size(); ++i)
    if (source.objects_[i])
      objects_[i] =
          std::make_shared<GraphicsObject>(source.objects_[i]->Clone());
  for (std::size_t i = 0; i < button_objects_.size(); ++i)
    if (source.button_objects_[i])
      button_objects_[i] =
          std::make_shared<GraphicsObject>(source.button_objects_[i]->Clone());
}

void TextWaku::Execute() {
  for (auto* list : {&button_objects_, &objects_})
    for (auto& obj : *list) {
      if (!obj)
        continue;
      obj->Execute();

      const auto dfs = [](auto&& dfs, GraphicsObject& obj) -> void {
        obj.ExecuteMutators();
        for (const auto& child : obj.GetChildren()) {
          if (!child)
            continue;
          dfs(dfs, *child);
        }
      };
      dfs(dfs, *obj);
    }
}

void TextWaku::Render(Point box_location,
                      Size namebox_size,
                      RGBAColour colour,
                      bool is_filter) {
  const Rect window(box_location, GetSize(namebox_size));
  for (auto* list : {&button_objects_, &objects_}) {
    const bool buttons = list == &button_objects_;
    std::vector<std::pair<std::size_t, GraphicsObject*>> draw_order;
    for (std::size_t i = 0; i < list->size(); ++i) {
      if ((*list)[i] && (*list)[i]->Param().visible() &&
          (!buttons || ButtonVisible(i)))
        draw_order.emplace_back(i, (*list)[i].get());
    }
    if (draw_order.empty())
      continue;
    if (!renderer_)
      throw std::runtime_error("TextWaku::Render requires a scene renderer");
    std::sort(draw_order.begin(), draw_order.end(),
              [](const auto& a, const auto& b) {
                const auto& ap = a.second->Param();
                const auto& bp = b.second->Param();
                return std::tie(ap.z_order, ap.z_layer, ap.z_depth, a.first) <
                       std::tie(bp.z_order, bp.z_layer, bp.z_depth, b.first);
              });
    for (const auto& [index, object] : draw_order)
      renderer_->RenderObject(
          *object, nullptr,
          buttons ? ButtonParent(index, window) : ObjectParent(box_location));
  }
}

std::shared_ptr<GraphicsObject> TextWaku::TouchButton(std::size_t index) {
  auto& button = button_objects_.at(index);
  if (!button)
    button = std::make_shared<GraphicsObject>();
  return button;
}

bool TextWaku::ButtonVisible(std::size_t index) const {
  return index >= button_visible_.size() || button_visible_[index];
}

ParentObjState TextWaku::ButtonParent(std::size_t index,
                                      const Rect& window) const {
  const auto& config = button_config_.at(index);
  const bool right = config.position_base == 1 || config.position_base == 3;
  const bool bottom = config.position_base == 2 || config.position_base == 3;
  return ObjectParent(Point(right ? window.x2() - config.position.x()
                                  : window.x() + config.position.x(),
                            bottom ? window.y2() - config.position.y()
                                   : window.y() + config.position.y()));
}

void TextWaku::ResetButtonsFrom(TextWaku& previous) {
  const auto reset = [](auto&& self, GraphicsObject& object) -> void {
    for (const auto& child : object.GetChildren())
      if (child)
        self(self, *child);
    object.Reset();
  };
  for (std::size_t i = 0; i < previous.button_objects_.size(); ++i) {
    auto& old = previous.button_objects_[i];
    if (!old)
      continue;
    reset(reset, *old);
    const auto action = old->frame_action_;
    if (i < button_objects_.size()) {
      if (button_objects_[i])
        *old = std::move(*button_objects_[i]);
      old->frame_action_ = action;
      button_objects_[i] = std::move(old);
    }
  }
}

std::shared_ptr<GraphicsObject> TextWaku::TouchObject(std::size_t index) {
  auto& object = objects_.at(index);
  if (!object)
    object = std::make_shared<GraphicsObject>();
  return object;
}

void TextWaku::ResetObjectsFrom(TextWaku& previous) {
  const std::size_t count = objects_.size();
  objects_ = std::move(previous.objects_);
  const auto reset = [](auto&& self, GraphicsObject& object) -> void {
    for (const auto& child : object.GetChildren())
      if (child)
        self(self, *child);
    object.Reset();
  };
  for (const auto& object : objects_)
    if (object)
      reset(reset, *object);
  objects_.resize(count);
}

ParentObjState TextWaku::ObjectParent(const Point& window_origin) {
  ParentObjState parent;
  parent.render_state.pos_x = window_origin.x();
  parent.render_state.pos_y = window_origin.y();
  return parent;
}
