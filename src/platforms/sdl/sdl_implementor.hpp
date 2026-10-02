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
// -----------------------------------------------------------------------

#pragma once

#include "platforms/implementor.hpp"

#include <filesystem>
#include <string>

class SdlImplementor : public IPlatformImplementor {
 public:
  std::filesystem::path SelectGameDirectory() override;

  void ReportFatalError(const std::string& message_text,
                        const std::string& informative_text) override;

  bool AskUserPrompt(const std::string& message_text,
                     const std::string& informative_text,
                     const std::string& true_button,
                     const std::string& false_button) override;
};
