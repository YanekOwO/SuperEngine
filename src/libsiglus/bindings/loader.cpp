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

#include "libsiglus/bindings/loader.hpp"

#include "libsiglus/archive.hpp"
#include "libsiglus/intern_name.hpp"
#include "libsiglus/parser.hpp"
#include "libsiglus/recompiler.hpp"
#include "libsiglus/scene.hpp"
#include "vm/exception.hpp"
#include "vm/function.hpp"

#include <format>
#include <iostream>
#include <unordered_map>

namespace libsiglus::binding {
namespace sr = serilang;

sr::Module* Loader::Load(int scene) {
  if (scene < 0 ||
      static_cast<std::size_t>(scene) >= archive_.GetScenarioCount()) {
    throw sr::RuntimeError(std::format("scene {} not found", scene));
  }

  const std::string scene_str = std::format("SCENE_{:04}", scene);
  if (auto it = scene_module_cache_.find(scene_str);
      it != scene_module_cache_.cend()) {
    return it->second;
  }

  Scene scn = archive_.ParseScene(scene);
  Parser parser(scn.scene_, scn.str_, scn.label, scn.zlabel, scn.property,
                archive_.prop_, scn.cmd, archive_.cmd_, scn.id_, scn.scnname_);
  auto parsed = parser.ParseAll();
  if (!parsed.has_value()) {
    throw sr::RuntimeError(std::format("failed to parse scene {} ({}): {}",
                                       scene, scn.scnname_, parsed.error()));
  }

  std::vector<Parser::ParsedToken>& tokens = parsed->first;
  std::vector<std::string>& warnings = parsed->second;
  for (const auto& warning : warnings)
    std::cerr << warning << std::endl;

  Recompiler compiler(vm_.gc_);
  compiler.SetSceneProperties(scn.id_, scn.property);
  compiler.is_debug_ = is_debug_;
  compiler.Compile(tokens);
  tokens.clear();

  if (!compiler.Ok()) {
    std::ignore = compiler.GetErrors();
  }

  compiler.module_->name = scn.scnname_;
  sr::Module* mod = compiler.module_;

  std::unordered_map<std::string, sr::Value> bootstrap_builtins;
  bootstrap_builtins.reserve(vm_.globals_->size() + vm_.builtins_->size());
  bootstrap_builtins.insert(vm_.globals_->begin(), vm_.globals_->end());
  bootstrap_builtins.insert(vm_.builtins_->begin(), vm_.builtins_->end());

  sr::VM bootstrap_vm(vm_.gc_);
  bootstrap_vm.gc_threshold_ = 0;
  bootstrap_vm.globals_ = mod->globals;
  bootstrap_vm.builtins_ =
      std::make_shared<std::unordered_map<std::string, sr::Value>>(
          std::move(bootstrap_builtins));

  try {
    bootstrap_vm.Evaluate(compiler.GetCode());
  } catch (const std::exception& e) {
    throw sr::RuntimeError(std::format("failed to bootstrap scene {} ({}): {}",
                                       scene, scn.scnname_, e.what()));
  }

  SceneCommandTable command_table{.scene_id = scene,
                                  .scene_name = scn.scnname_};
  command_table.commands.reserve(compiler.GetUserCommands().size());
  for (const UserCommandMetadata& metadata : compiler.GetUserCommands()) {
    const std::string entry_name = GetUsercmdId(metadata.source_entry);
    auto fn_it = mod->globals->find(entry_name);
    if (fn_it == mod->globals->end()) {
      throw sr::RuntimeError(
          std::format("failed to bootstrap user command '{}' in scene {} ({})",
                      metadata.name, scene, scn.scnname_));
    }
    sr::Function* function = fn_it->second.Get_if<sr::Function>();
    if (!function) {
      throw sr::RuntimeError(
          std::format("user command '{}' in scene {} ({}) is not a function",
                      metadata.name, scene, scn.scnname_));
    }
    const bool inserted =
        command_table.commands
            .emplace(metadata.name,
                     UserCommand{.function = function, .args = metadata.args})
            .second;
    if (!inserted) {
      throw sr::RuntimeError(
          std::format("duplicate user command '{}' in scene {} ({})",
                      metadata.name, scene, scn.scnname_));
    }
  }

  scene_module_cache_[scene_str] = mod;
  (*vm_.builtins_)["%%siglus_module@" + scene_str] = sr::Value(mod);
  commands_by_globals_.emplace(mod->globals.get(), std::move(command_table));

  return mod;
}

serilang::Module* Loader::Load(std::string const& scnname) {
  const auto it = archive_.scn_map_.find(scnname);
  if (it == archive_.scn_map_.cend())
    throw sr::RuntimeError(std::format("scene '{}' not found", scnname));
  const int scnid = it->second;
  return Load(scnid);
}

const UserCommand& Loader::ResolveUserCommand(
    sr::Fiber& fiber,
    std::string_view command_name) const {
  for (auto frame_it = fiber.frames.rbegin(); frame_it != fiber.frames.rend();
       ++frame_it) {
    if (!frame_it->fn || !frame_it->fn->globals)
      continue;

    auto scene_it = commands_by_globals_.find(frame_it->fn->globals.get());
    if (scene_it == commands_by_globals_.end())
      continue;

    const SceneCommandTable& scene = scene_it->second;
    auto command_it = scene.commands.find(std::string(command_name));
    if (command_it == scene.commands.end()) {
      throw sr::RuntimeError(
          std::format("user command '{}' not found in scene {} ({})",
                      command_name, scene.scene_id, scene.scene_name));
    }
    return command_it->second;
  }

  throw sr::RuntimeError(std::format(
      "cannot resolve user command '{}': caller is not in a loaded scene",
      command_name));
}

}  // namespace libsiglus::binding
