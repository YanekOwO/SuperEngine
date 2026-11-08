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

#include "core/mask.hpp"
#include "systems/event_system.hpp"

#include <memory>

namespace libsiglus::binding {

struct MaskHandle {
  std::shared_ptr<MaskList> list_;
  int index_;

  inline Mask& Get() { return list_->At(index_); }
  inline const Mask& Get() const { return list_->At(index_); }
};

struct MaskEventHandle {
  MaskHandle mask_;
  Mask::Parameter parameter_;
  EventSystem* event_;

  inline Mask& Get() { return mask_.Get(); }
};

struct MaskListHandle {
  std::shared_ptr<MaskList> list;
};

}  // namespace libsiglus::binding
