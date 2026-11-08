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

#include "core/script_call.hpp"
#include "libsiglus/types.hpp"
#include "vm/object.hpp"
#include "vm/vm.hpp"

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace libsiglus {
class Archive;
namespace binding {

struct UserCommand {
  serilang::Value function;
  std::vector<Type> args;
  CmdTarget target;
};

serilang::Value ResolveZlabelEntry(const serilang::Module& mod, int zlabel);

class Loader {
 public:
  explicit Loader(Archive& ar,
                  serilang::VM& v,
                  bool debug = true,
                  bool trace = false)
      : archive_(ar), vm_(v), is_debug_(debug), trace_enabled_(trace) {}

  serilang::Module& LoadScene(std::string scene_name);
  const UserCommand& ResolveSceneUserCommand(std::string scene_name,
                                             const std::string& command_name);
  serilang::Value ResolveSceneCommand(std::string scene_name,
                                      const std::string& command_name);
  serilang::Value ResolveSceneEntry(std::string scene_name, int zlabel);
  serilang::Value ResolveScriptCall(const ScriptCall& call);

  serilang::Module* Load(int scene);
  serilang::Module* Load(std::string const& scnname);
  serilang::Value ResolveUserCommandTarget(const CmdTarget& target);
  std::span<const Type> GetUserCommandArgs(const CmdTarget& target);

  const UserCommand& ResolveUserCommand(serilang::Fiber& fiber,
                                        std::string_view command_name);
  const UserCommand& ResolveUserCommand(const serilang::Module& mod,
                                        std::string_view command_name);

 private:
  struct SceneCommandTable {
    int scene_id = -1;
    std::string scene_name;
    std::unordered_map<std::string, UserCommand> commands;
  };

  using Globals = std::unordered_map<std::string, serilang::Value>;

  const UserCommand& ResolveUserCommand(const SceneCommandTable& scene,
                                        std::string_view command_name);
  const UserCommand& FindUserCommand(const SceneCommandTable& scene,
                                     std::string_view command_name) const;

  Archive& archive_;
  serilang::VM& vm_;
  bool is_debug_;
  bool trace_enabled_;
  std::unordered_map<std::string, serilang::Module*> scene_module_cache_;
  std::unordered_map<const Globals*, SceneCommandTable> commands_by_globals_;
};

}  // namespace binding
}  // namespace libsiglus
