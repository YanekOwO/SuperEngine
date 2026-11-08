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

#include "libsiglus/bindings/registry.hpp"

#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/flow.hpp"
#include "libsiglus/bindings/loader.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/intern_name.hpp"
#include "libsiglus/scene_entry.hpp"
#include "srbind/srbind.hpp"
#include "vm/exception.hpp"
#include "vm/function.hpp"
#include "vm/future.hpp"
#include "vm/instruction.hpp"
#include "vm/object.hpp"
#include "vm/string.hpp"
#include "vm/value.hpp"
#include "vm/vm.hpp"

#include <format>
#include <string>
#include <vector>

namespace libsiglus::binding {
namespace sr = serilang;
namespace sb = srbind;

static std::string NormalizeSceneName(std::string name) {
  for (char& c : name) {
    if ('A' <= c && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  }
  return name;
}

static sr::Value ResolveZlabelEntry(const sr::Module& mod, int zlabel) {
  auto it = mod.globals->find(GetZlabelId(zlabel));
  if (it == mod.globals->cend())
    it = mod.globals->find("%%script");

  if (it == mod.globals->cend()) {
    throw sr::RuntimeError(
        std::format("scene {} has no entry for z-label {}", mod.name, zlabel));
  }

  const auto* bound = it->second.Get_if<sr::BoundMethod>();
  if (!bound || !bound->method.Get_if<sr::Function>() ||
      bound->additional_args.size() != 1 ||
      !bound->additional_args.front().Get_if<sr::List>()) {
    throw sr::RuntimeError(
        std::format("scene {} entry is not a dispatcher adapter", mod.name));
  }
  return it->second;
}

static sr::Module& LoadScene(Loader& loader, std::string scene_name) {
  if (scene_name.empty())
    throw sr::RuntimeError("scene name is empty");

  sr::Module* mod = loader.Load(NormalizeSceneName(std::move(scene_name)));
  if (!mod)
    throw sr::RuntimeError("could not load destination scene");

  return *mod;
}

sr::Value ResolveSceneEntry(Loader& loader,
                            std::string scene_name,
                            int zlabel) {
  return ResolveZlabelEntry(LoadScene(loader, std::move(scene_name)), zlabel);
}

sr::Value ResolveSceneCommand(Loader& loader,
                              std::string scene_name,
                              const std::string& command_name) {
  return ResolveSceneUserCommand(loader, std::move(scene_name), command_name)
      .function;
}

const UserCommand& ResolveSceneUserCommand(Loader& loader,
                                           std::string scene_name,
                                           const std::string& command_name) {
  return loader.ResolveUserCommand(LoadScene(loader, std::move(scene_name)),
                                   command_name);
}

sr::Value ResolveScriptCall(Loader& loader,
                            sr::GarbageCollector& gc,
                            const ScriptCall& call) {
  if (const auto* command = std::get_if<FrameAction::Target>(&call.target))
    return loader.ResolveUserCommandTarget(*command);
  const auto& scene = std::get<SceneTarget>(call.target);
  sr::Module* module = nullptr;
  if (const auto* id = std::get_if<int>(&scene.scene)) {
    if (*id < 0)
      throw sr::RuntimeError("EX-call scene id must be non-negative");
    module = loader.Load(*id);
  } else {
    module = &LoadScene(loader, std::get<std::string>(scene.scene));
  }
  if (!module)
    throw sr::RuntimeError("EX-call could not load destination scene");
  const auto entry = ResolveZlabelEntry(*module, scene.zlabel);
  if (!call.farcall)
    return entry;
  const auto* bound = entry.Get_if<sr::BoundMethod>();
  const auto* descriptor = bound->additional_args.front().Get_if<sr::List>();
  return MakeSceneEntry(
      gc, module->globals->at("%%dispatch").Get_if<sr::Function>(),
      descriptor->items.front().Get<int>(), 2, SceneCallKind::Farcall);
}

void BindFlow(SiglusMachine& rt) {
  sr::VM& vm = *rt.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  m.def(
      "jump",
      [loader = rt.loader.get(), archive = rt.host().archive.get()](
          sr::VM& vm, sr::Fiber& fiber, std::vector<sr::Value> args) {
        if (!loader)
          throw sr::RuntimeError("jump requires a scene loader");
        if (args.empty() || args.size() > 2) {
          throw sr::RuntimeError(std::format(
              "jump expects 1 or 2 arguments, got {}", args.size()));
        }

        int zlabel = 0;
        if (args.size() == 2)
          zlabel = RequireInt(args[1], "jump z-label");

        sr::Module* mod = nullptr;
        if (const int* scene_id = args[0].Get_if<int>()) {
          if (*scene_id < 0) {
            throw sr::RuntimeError(std::format(
                "jump scene id must be non-negative: {}", *scene_id));
          }
          if (archive && static_cast<std::size_t>(*scene_id) >=
                             archive->GetScenarioCount()) {
            throw sr::RuntimeError(
                std::format("jump scene id {} is out of range for {} scenes",
                            *scene_id, archive->GetScenarioCount()));
          }
          mod = loader->Load(*scene_id);
        } else if (const sr::String* scene_name =
                       args[0].Get_if<sr::String>()) {
          if (scene_name->str_.empty())
            throw sr::RuntimeError("jump scene name is empty");
          mod = loader->Load(NormalizeSceneName(scene_name->str_));
        } else {
          throw sr::RuntimeError(std::format(
              "jump scene expects str or int, got {}", args[0].Desc()));
        }

        if (!mod)
          throw sr::RuntimeError("jump could not load destination scene");

        sr::Value fn = ResolveZlabelEntry(*mod, zlabel);
        // Scene jumps replace the current dispatcher but keep its caller and
        // bank ownership. The destination adopts cleanup instead of pushing
        // another bank frame.
        auto& old = fiber.frames.back();
        if (old.fast_locals.size() > kSceneOwnedBankSlot &&
            old.fast_locals[1] && old.fast_locals[1]->Get_if<sr::List>() &&
            old.fast_locals[kSceneOwnedBankSlot]) {
          const auto* old_entry = old.fast_locals[1]->Get_if<sr::List>();
          const auto* target = fn.Get_if<sr::BoundMethod>();
          const auto* entry =
              target->additional_args.front().Get_if<sr::List>();
          auto* transferred = vm.gc_->Allocate<sr::List>(
              std::vector<sr::Value>{entry->items[0], 0, old_entry->items[2],
                                     *old.fast_locals[kSceneOwnedBankSlot]});
          auto* adapter = vm.gc_->Allocate<sr::BoundMethod>(
              target->method, std::vector<sr::Value>{sr::Value(transferred)});
          vm.ReplaceCurrentCall(fiber, sr::Value(adapter));
        } else {
          vm.ScheduleExecute(fn);
          fiber.frames.clear();
        }
      },
      sb::vararg);

  m.def("__builtin_usrcmd",
        [loader = rt.loader.get()](int scn, int entry,
                                   std::string name) -> sr::Value {
          const std::string cmdname = GetUsercmdId(entry);
          const std::string dbgname = std::format("{}:{}@{}", scn, entry, name);

          sr::Module* mod = loader->Load(scn);
          if (!mod) {
            throw sr::RuntimeError(std::format(
                "User command {} could not load scene {}", dbgname, scn));
          }

          auto it = mod->globals->find(cmdname);
          if (it == mod->globals->cend()) {
            std::string errmsg =
                std::format("User command {} does not exist in {}:{}", dbgname,
                            scn, mod->name);
            throw sr::RuntimeError(std::move(errmsg));
          }
          return it->second;
        });

  m.def(
      "__builtin_farcall",
      [loader = rt.loader.get(), gc = vm.gc_.get()](std::string scene,
                                                    int zlabel) -> sr::Value {
        ScriptCall call;
        call.target = SceneTarget{std::move(scene), zlabel};
        call.farcall = true;
        return ResolveScriptCall(*loader, *gc, call);
      },
      sb::arg("scene"), sb::arg("zlabel"));

  m.def(
      "__builtin_excall",
      [&rt](sr::Value scene, int zlabel, sr::Value integers,
            sr::Value strings) -> sr::Value {
        ScriptCall call;
        SceneTarget target;
        if (const auto* id = scene.Get_if<int>())
          target.scene = *id;
        else if (const auto* name = scene.Get_if<sr::String>())
          target.scene = name->str_;
        else
          throw sr::RuntimeError("EX-call scene expects str or int");
        target.zlabel = zlabel;
        call.target = std::move(target);
        call.farcall = true;
        for (const auto& value :
             RequireList(integers, "EX-call L arguments")->items)
          call.integers.push_back(RequireInt(value, "EX-call L argument"));
        for (const auto& value :
             RequireList(strings, "EX-call K arguments")->items)
          call.strings.push_back(value.Str());
        auto* future = rt.vm->gc_->Allocate<sr::Future>();
        future->promise = rt.host().CallExcall(rt, std::move(call));
        return future;
      },
      sb::arg("scene"), sb::arg("zlabel"), sb::arg("integers"),
      sb::arg("strings"));

  m.def("__builtin_load_scn",
        [loader = rt.loader.get()](int scnid) -> sr::Value {
          sr::Module* mod = loader->Load(scnid);
          return sr::Value(mod);
        });
}

RLVM_REGISTER(SiglusBindingRegistry, "flow", BindFlow)

}  // namespace libsiglus::binding
