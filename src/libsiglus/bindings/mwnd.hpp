// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
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

#include "systems/text_waku.hpp"

#include "core/stage.hpp"
#include "systems/text_window.hpp"
#include "vm/value.hpp"

#include <functional>
#include <memory>
#include <string>

namespace libsiglus::binding {

struct MwndHandle {
  std::shared_ptr<TextWindow> mwnd;
  int owner = 0;
};

struct MwndArrayHandle {
  int layer = kLayerFg;
  int owner = 0;
};

struct MwndObjectList {
  enum class Kind { Object, Button, Face };

  std::shared_ptr<TextWindow> window;
  std::function<serilang::Value(std::shared_ptr<GraphicsObject>)> make_object;
  Kind kind = Kind::Object;

  std::string Name() const;

  TextWaku& Resolve() const;

  serilang::Value Get(int index) const;

  std::size_t Size() const;
};

}  // namespace libsiglus::binding
