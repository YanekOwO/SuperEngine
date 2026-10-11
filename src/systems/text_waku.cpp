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

void TextWaku::Execute() {
  for (auto& obj : objects_) {
    if (!obj)
      continue;
    obj->Execute();
    const auto mutate = [](auto&& self, GraphicsObject& object) -> void {
      object.ExecuteMutators();
      for (const auto& child : object.GetChildren())
        if (child)
          self(self, *child);
    };
    mutate(mutate, *obj);
  }
}

void TextWaku::Render(Point box_location,
                      Size namebox_size,
                      RGBAColour colour,
                      bool is_filter) {
  std::vector<std::pair<std::size_t, GraphicsObject*>> draw_order;
  for (std::size_t i = 0; i < objects_.size(); ++i) {
    if (objects_[i] && objects_[i]->Param().visible())
      draw_order.emplace_back(i, objects_[i].get());
  }
  if (draw_order.empty())
    return;
  if (!renderer_)
    throw std::runtime_error("TextWaku::Render requires a scene renderer");
  std::sort(draw_order.begin(), draw_order.end(),
            [](const auto& a, const auto& b) {
              const auto& ap = a.second->Param();
              const auto& bp = b.second->Param();
              return std::tie(ap.z_order, ap.z_layer, ap.z_depth, a.first) <
                     std::tie(bp.z_order, bp.z_layer, bp.z_depth, b.first);
            });
  const ParentObjState parent = ObjectParent(box_location);
  for (const auto& [index, object] : draw_order)
    renderer_->RenderObject(*object, nullptr, parent);
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
