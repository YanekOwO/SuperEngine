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

#include <filesystem>
#include <memory>
#include <optional>

class EventListener;
class System;
class RLMachine;

class Gameexe;
namespace libreallive {

class Archive;

class GameLoader {
 public:
  GameLoader(std::filesystem::path gameroot,
             std::optional<int> start_scene,
             bool fast_forward = false);

  std::shared_ptr<libreallive::Archive> archive_;

  std::shared_ptr<Gameexe> gameexe_;

  std::shared_ptr<RLMachine> machine_;

  std::shared_ptr<EventListener> longop_listener_adapter_, system_listener_;

 private:
  void Load();
};

}  // namespace libreallive
