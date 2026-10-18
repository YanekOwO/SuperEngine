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
#include "m6/compiler_pipeline.hpp"
#include "systems/graphics_system.hpp"
#include "utilities/file.hpp"
#include "vm/vm.hpp"

#include <exception>
#include <filesystem>
#include <format>
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

    if (hook_path_) {  // inject custom script
      const std::filesystem::path& path = *hook_path_;
      try {
        auto source =
            m6::SourceBuffer::Create(LoadFileStr(path), path.string());
        m6::CompilerPipeline compiler(machine->vm->gc_);
        compiler.compile(std::move(source));
        if (!compiler.Ok())
          throw std::runtime_error(compiler.FormatErrors());

        machine->vm->AddFiber(compiler.Get());
        machine->Run();
      } catch (const std::exception& e) {
        throw std::runtime_error(
            std::format("Hook '{}': {}", path.string(), e.what()));
      }

      if (vm.IsStopRequested())
        return 0;
    }

    machine->Start(start_scene_);
    machine->Run();

    return 0;

  } catch (const sr::UnhandledError& e) {
    std::cerr << "Fatal Siglus script error: " << e.what() << std::endl;
  } catch (std::exception& e) {
    std::cerr << "Fatal Siglus runtime error: " << e.what() << std::endl;
  }

  return 1;
}
