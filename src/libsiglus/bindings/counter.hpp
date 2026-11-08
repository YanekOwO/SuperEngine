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

namespace libsiglus::binding {

// A VM-facing owning reference to a Counter. Other Siglus elements use
// this handle to expose the same native Counter type as the global list.
struct CounterHandle {
  std::shared_ptr<Counter> counter_;
  int owner = -1;

  std::shared_ptr<Counter> Get() const;
};

struct CounterListHandle {
  std::shared_ptr<CounterList> list;
  int owner = 0;
};

}  // namespace libsiglus::binding
