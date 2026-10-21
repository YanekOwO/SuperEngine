// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2011 Elliot Glaysher
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

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class Gameexe;
class Platform;
class RLMachine;
class System;
class Debugger;
class IPlatformImplementor;
namespace libreallive {
class Archive;
}

// The main, cross platform emulator class. Has template methods for
// implementing platform specific GUI.
class RLVMInstance {
 public:
  RLVMInstance();
  ~RLVMInstance();

  int Main(const std::filesystem::path& gamepath);

  void SetStartScene(int scene_id) { start_scene_ = scene_id; }
  void SetCustomFont(std::string font) { custom_font_ = std::move(font); }
  void SetFastForward(bool fast_forward) { fast_forward_ = fast_forward; }

  void SetPlatformImplementor(std::shared_ptr<IPlatformImplementor> impl);

 private:
  // Control the machine to execute the next operation
  void Step();

  // Returns a formatted string to descirbe current location (scene number,
  // line)
  std::string DescribeCurrentIP() const;

  // Should bring up a platform native dialog box to report the message.
  void ReportFatalError(const std::string& message_text,
                        const std::string& informative_text);

  // Ask the user if we should take an action.
  bool AskUserPrompt(const std::string& message_text,
                     const std::string& informative_text,
                     const std::string& true_button,
                     const std::string& false_button);

  std::shared_ptr<RLMachine> machine_;

  std::shared_ptr<Debugger> debugger_;

  // Whether we should set a custom font.
  std::string custom_font_;

  // Which SEEN# we should start execution from
  std::optional<int> start_scene_;

  bool fast_forward_ = false;

  // The bridge to the class that implements platform-specific code
  std::shared_ptr<IPlatformImplementor> platform_implementor_;
};
