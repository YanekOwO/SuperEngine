// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
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
//
// -----------------------------------------------------------------------

#pragma once

#include "vm/value.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace serilang {

struct Function;

struct ExceptionHandler {
  uint32_t handler_ip;
  size_t stack_top;
};

struct CallFrame {
  Function* fn = nullptr;
  uint32_t ip = 0;  // index into chunk->code
  size_t bp = 0;    // base pointer into fiber stack
  CallResultMode result_mode = CallResultMode::Keep;
  std::vector<ExceptionHandler> handlers;
  std::vector<std::optional<Value>> fast_locals;
};

}  // namespace serilang
