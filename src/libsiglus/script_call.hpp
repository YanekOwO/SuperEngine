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

#include "core/frame_action.hpp"
#include "vm/value.hpp"

#include <string>
#include <variant>
#include <vector>

namespace serilang {
class GarbageCollector;
}

namespace libsiglus {

struct SceneTarget {
  std::variant<int, std::string> scene = 0;
  int zlabel = 0;
  bool operator==(const SceneTarget&) const = default;
};

// Calls between execution instances contain engine data, never VM objects.
struct ScriptCall {
  std::variant<SceneTarget, FrameAction::Target> target = SceneTarget{};
  std::vector<int> integers;
  std::vector<std::string> strings;
  bool farcall = false;
};

// A result is copied out before its producing execution instance is released.
struct TransferValue {
  using List = std::vector<TransferValue>;
  using Storage =
      std::variant<std::monostate, bool, int, double, std::string, List>;
  Storage value;

  static TransferValue Capture(const serilang::Value& value);
  serilang::Value Restore(serilang::GarbageCollector& gc) const;
  bool operator==(const TransferValue&) const = default;
};

std::vector<serilang::Value> MakeScriptArguments(serilang::GarbageCollector& gc,
                                                 const ScriptCall& call);

}  // namespace libsiglus
