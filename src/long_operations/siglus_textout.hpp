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

#include "long_operations/long_operation.hpp"

#include <string>

class System;
struct LocalConfig;

namespace lop {

class SiglusTextoutOperation final : public LongOperation {
 public:
  SiglusTextoutOperation(System* system,
                         LocalConfig& local_config,
                         std::string text);
  Task<int> Run(WaitContext& waits) override;

 private:
  bool ShouldFlushText() const;

  System* system_;
  LocalConfig& local_config_;
  std::string text_;
  bool flush_ = false;
};

}  // namespace lop
