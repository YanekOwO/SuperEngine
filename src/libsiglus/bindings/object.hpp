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

#include "core/object.hpp"
#include "core/object_internal/object_parameter.hpp"

#include <functional>
#include <memory>
#include <string>

namespace libsiglus::binding {

struct ObjectHandle {
  std::shared_ptr<GraphicsObject> object;
  int owner = 0;
};

struct ObjectListHandle {
  int layer;
  int owner = 0;
};

struct ObjectChildListHandle {
  std::shared_ptr<GraphicsObject> object;
  int owner = 0;
};

struct ObjectEventHandle {
  std::shared_ptr<GraphicsObject> object;
  std::string name;
  std::function<int(const ObjectParameter&)> getter;
  std::function<void(ObjectParameter&, int)> setter;
  int owner = 0;
};

struct ObjectRepnoEventHandle {
  using Getter = std::function<int(const ObjectParameter&, int)>;
  using Setter = std::function<void(ObjectParameter&, int, int)>;

  std::shared_ptr<GraphicsObject> object;
  int repno;
  std::string name;
  Getter getter;
  Setter setter;
  int owner = 0;
};

struct ObjectRepnoEventListHandle {
  std::shared_ptr<GraphicsObject> object;
  std::string name;
  ObjectRepnoEventHandle::Getter getter;
  ObjectRepnoEventHandle::Setter setter;
  int owner = 0;
};

// Rebuild native property accessors from their script-visible identity.
ObjectEventHandle RestoreObjectEvent(std::shared_ptr<GraphicsObject> object,
                                     const std::string& name,
                                     int owner);
ObjectRepnoEventListHandle RestoreObjectRepnoEvent(
    std::shared_ptr<GraphicsObject> object,
    const std::string& name,
    int owner);

}  // namespace libsiglus::binding
