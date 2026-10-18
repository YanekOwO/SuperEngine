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

#include "core/selection.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "vm/value.hpp"

namespace libsiglus::binding {

SelectionRequest DecodeSelectionRequest(std::vector<serilang::Value> raw,
                                        bool buttons,
                                        bool cancel,
                                        bool context);

// Resolves resources, window styling, name substitutions, and hit geometry.
std::shared_ptr<Selection> Build(SiglusMachine& machine,
                                 SelectionRequest request,
                                 std::shared_ptr<TextWindow> window);

}  // namespace libsiglus::binding
