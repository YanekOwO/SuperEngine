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

#include <optional>

namespace libsiglus::binding {

struct WipeCompletion {
  // Completion requests specify a minimum total number of traversals since
  // the wipe started. They always leave at least one traversal still to run.
  void RequestFinish(int result, int minimum_total_boundaries = 1);
  bool AfterFrameActions();

  int regular_boundaries = 0;
  std::optional<int> finish_after_boundary;
  int result = 0;
};

}  // namespace libsiglus::binding
