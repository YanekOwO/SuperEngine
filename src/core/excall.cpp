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

#include "core/excall.hpp"

#include "vm/exception.hpp"

Excall::Excall()
    : counters(std::make_shared<CounterList>(0)),
      actions(std::make_shared<FrameActionManager>(stage, config, 0)) {}

void Excall::RequireAllocated() const {
  if (!allocated)
    throw serilang::RuntimeError("EX-call resources are not allocated");
}

void Excall::Alloc(const AllocationSizes& sizes) {
  if (allocated || releasing)
    return;
  flags.resize(sizes.flag_count);
  counters->elements_.resize(sizes.counter_count);
  for (auto& counter : counters->elements_)
    if (!counter)
      counter = std::make_shared<Counter>();
  actions->global_channels().resize(sizes.frame_action_count);
  stage = Stage(sizes.object_count, sizes.effect_count, sizes.group_count,
                sizes.quake_count);
  for (int layer : {kLayerFg, kLayerBg, kLayerNext})
    stage.mwnd.Resize(layer, sizes.window_count);
  lifetime = std::make_shared<char>();
  allocated = true;
}

void Excall::Free(bool finish_actions) {
  if (!allocated || (releasing && finish_actions))
    return;
  releasing = true;
  try {
    if (finish_actions)
      actions->FinishAll();
  } catch (...) {
    actions->CancelAll();
    releasing = false;
    Free(false);
    throw;
  }
  actions->CancelAll();
  counters->Reset();
  counters->elements_.clear();
  actions->global_channels().clear();
  flags.clear();
  stage = Stage();
  config.font_name.clear();
  lifetime.reset();
  allocated = false;
  releasing = false;
}

void Excall::Reset() {
  Free(false);
  config = LocalConfig{};
}
