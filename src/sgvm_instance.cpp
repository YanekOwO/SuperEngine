// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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
//
// -----------------------------------------------------------------------

#include "sgvm_instance.hpp"

#include "libsiglus/sgvm_factory.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "systems/graphics_system.hpp"
#include "vm/function.hpp"
#include "vm/instruction.hpp"
#include "vm/object.hpp"
#include "vm/vm.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace libsiglus;
namespace sr = serilang;

int SgvmInstance::Main(const std::filesystem::path& game_root) {
  try {
    SGVMFactory factory(game_root);
    factory.debug_ = debug_;
    factory.fast_forward_ = fast_forward_;
    factory.platform_implementor_ = platform_implementor_;
    std::unique_ptr<SiglusMachine> machine = factory.Create();
    SiglusMachine& rt = *machine;

    rt.system->graphics().SetDebugFrameDumpConfig(debug_frame_dump_config_);
    sr::VM& vm = *rt.vm;

    sr::Module* mod = rt.loader->Load(start_scene_);
    if (!mod) {
      std::string errmsg =
          std::format("Failed to parse start scene: {}", start_scene_);
      throw std::runtime_error(std::move(errmsg));
    }

    sr::Function* script = (*mod->globals)["%%script"].Get_if<sr::Function>();
    if (!script) {
      std::string errmsg =
          std::format("Scene {}: %%script not found", start_scene_);
      throw std::runtime_error(std::move(errmsg));
    }

    vm.ScheduleExecute(script);
    vm.Run();

    return 0;

  } catch (const sr::UnhandledError& e) {
    std::cerr << "Fatal Siglus script error: " << e.what() << std::endl;
  } catch (std::exception& e) {
    std::cerr << "Fatal Siglus runtime error: " << e.what() << std::endl;
  }

  return 1;
}
