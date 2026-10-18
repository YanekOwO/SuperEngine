// -----------------------------------------------------------------------
//
// This file is part of RLVM
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
// -----------------------------------------------------------------------

#pragma once

#include "libsiglus/siglus_machine.hpp"

#include <filesystem>
#include <memory>

namespace libsiglus {

class SGVMFactory {
 public:
  std::unique_ptr<SiglusMachine> Create();

  static std::unique_ptr<Stage> CreateStage(Gameexe& gameexe);

  std::filesystem::path base_path_;
  bool debug_ = false;
  bool fast_forward_ = false;
  std::shared_ptr<IPlatformImplementor> platform_implementor_;
};

}  // namespace libsiglus
