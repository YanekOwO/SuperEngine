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

#include "libsiglus/bindings/loader.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/intern_name.hpp"
#include "srbind/srbind.hpp"
#include "vm/exception.hpp"
#include "vm/future.hpp"
#include "vm/list.hpp"
#include "vm/object.hpp"
#include "vm/string.hpp"
#include "vm/value.hpp"
#include "vm/vm.hpp"

#include <format>
#include <string>

namespace libsiglus::binding {
namespace sr = serilang;
namespace sb = srbind;

void BindFlow(SiglusMachine& rt) {
  sr::VM& vm = *rt.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
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
      "__builtin_scene_entry",
      [loader = rt.loader.get()](sr::Value scene, int zlabel) -> sr::Value {
        sr::Module* mod = nullptr;
        if (const auto* id = scene.Get_if<int>())
          mod = loader->Load(*id);
        else if (const auto* name = scene.Get_if<sr::String>())
          mod = &loader->LoadScene(name->str_);
        else
          throw sr::RuntimeError("scene entry expects str or int");
        if (!mod)
          throw sr::RuntimeError("could not load destination scene");
        return ResolveZlabelEntry(*mod, zlabel);
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
