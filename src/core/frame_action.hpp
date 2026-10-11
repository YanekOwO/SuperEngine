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

#pragma once

#include "core/counter.hpp"

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace serilang {
struct Function;
}

using FrameActionArgument = std::variant<int, std::string>;

struct FrameAction {
  FrameAction() = default;
  FrameAction(const FrameAction& source);
  FrameAction& operator=(const FrameAction& source);
  FrameAction(FrameAction&& source) noexcept;
  FrameAction& operator=(FrameAction&& source) noexcept;

  std::shared_ptr<Counter> GetCounter();
  inline bool IsActive() const { return function != nullptr; }
  void Reset();

  // Borrowed from a scene module kept alive by the VM.
  serilang::Function* function = nullptr;
  std::vector<FrameActionArgument> arguments;
  // Created on first use; moves transfer the allocation without replacing
  // counters referenced by scripts when a channel vector grows.
  std::shared_ptr<Counter> counter;
  int end_time = 0;
  bool end_action = false;
};

using FrameActionList = std::vector<FrameAction>;
