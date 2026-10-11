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
#include "srbind/srbind.hpp"
#include "vm/exception.hpp"
#include "vm/function.hpp"
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

static sr::Function* ResolveEntry(sr::Module& mod, const std::string& entry) {
  auto it = mod.globals->find(entry);
  if (it == mod.globals->cend())
    it = mod.globals->find("%%script");

  if (it == mod.globals->cend()) {
    throw sr::RuntimeError(
        std::format("scene {} has no entry for z-label {}", mod.name, entry));
  }

  sr::Function* fn = it->second.Get_if<sr::Function>();
  if (!fn) {
    throw sr::RuntimeError(
        std::format("scene {} entry is not a function", mod.name));
  }
  return fn;
}

sr::Function* ResolveSceneEntry(Loader& loader,
                                std::string scene_name,
                                const std::string& entry_name) {
  if (scene_name.empty())
    throw sr::RuntimeError("scene name is empty");

  sr::Module* mod = loader.Load(NormalizeSceneName(std::move(scene_name)));
  if (!mod)
    throw sr::RuntimeError("could not load destination scene");

  return ResolveEntry(*mod, entry_name);
}

void BindFlow(SiglusRuntime& rt) {
  sr::VM& vm = *rt.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  m.def(
      "jump",
      [loader = rt.loader.get(), archive = rt.archive.get()](
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

        sr::Function* fn = ResolveEntry(*mod, GetZlabelId(zlabel));
				vm.ScheduleExecute(fn);
        fiber.frames.clear();
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

  m.def("__builtin_farcall",
        [loader = rt.loader.get()](std::string scn, int zlabel) -> sr::Value {
          const std::string dbgname = std::format("SCENE{}@{}", scn, zlabel);
          sr::Module* mod = loader->Load(NormalizeSceneName(std::move(scn)));

          if (!mod)
            throw sr::RuntimeError(
                std::format("Farcall {} could not load scene", dbgname));

          return ResolveEntry(*mod, GetZlabelId(zlabel));
        });

  m.def("__builtin_load_scn",
        [loader = rt.loader.get()](int scnid) -> sr::Value {
          sr::Module* mod = loader->Load(scnid);
          return sr::Value(mod);
        });
}

RLVM_REGISTER(SiglusBindingRegistry, "flow", BindFlow)

}  // namespace libsiglus::binding
