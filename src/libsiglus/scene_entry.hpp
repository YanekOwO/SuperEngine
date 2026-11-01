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
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
// -----------------------------------------------------------------------

#pragma once

#include "vm/function.hpp"
#include "vm/gc.hpp"
#include "vm/list.hpp"
#include "vm/object.hpp"

namespace libsiglus {

// Dispatcher slots: function, entry descriptor, variadic arguments, bank owner.
inline constexpr uint16_t kSceneArgumentSlot = 4;
inline constexpr uint16_t kSceneOwnedBankSlot = 3;

enum class SceneCallKind { Scene, Command, Gosub, Farcall, Extended };

// Descriptors are immutable lists containing target, arity, and call kind.
inline serilang::Value MakeSceneEntry(serilang::GarbageCollector& gc,
                                      serilang::Function* dispatcher,
                                      int target,
                                      int arity,
                                      SceneCallKind kind) {
  auto* descriptor = gc.Allocate<serilang::List>(
      std::vector<serilang::Value>{target, arity, static_cast<int>(kind)});
  return gc.Allocate<serilang::BoundMethod>(
      serilang::Value(dispatcher),
      std::vector<serilang::Value>{serilang::Value(descriptor)});
}

}  // namespace libsiglus
