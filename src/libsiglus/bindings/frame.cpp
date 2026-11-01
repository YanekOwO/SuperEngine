// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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
//
// -----------------------------------------------------------------------

#include "libsiglus/bindings/registry.hpp"

#include "srbind/srbind.hpp"

namespace libsiglus::binding {

void BindFrame(SiglusMachine& runtime) {
  srbind::module_ m(runtime.vm->gc_.get(), runtime.vm->globals_.get());
  m.def("frame", [&runtime] { return runtime.RequestFrame(); });
  runtime.reset_local_memory.emplace_back(
      [frames = runtime.frame_requests.get()] { frames->Reset(); });
}

RLVM_REGISTER(SiglusBindingRegistry, "frame", BindFrame)

}  // namespace libsiglus::binding
