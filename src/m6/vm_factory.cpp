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
//
// -----------------------------------------------------------------------

#include "m6/vm_factory.hpp"

#include "m6/compiler_pipeline.hpp"
#include "srbind/srbind.hpp"
#include "utilities/file.hpp"
#include "vm/exception.hpp"
#include "vm/future.hpp"
#include "vm/string.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace m6 {
namespace sr = serilang;
namespace sb = srbind;

static double Time() {
  using namespace std::chrono;
  auto now = system_clock::now().time_since_epoch();
  auto secs = duration_cast<seconds>(now).count();
  return secs;
}

sr::VM VMFactory::Create(std::shared_ptr<sr::GarbageCollector> gc,
                         std::ostream& out_stream,
                         std::istream& in_stream,
                         std::ostream& err_stream) {
  if (!gc)
    gc = std::make_shared<sr::GarbageCollector>();
  sr::VM vm(gc);

  sb::module_ m(vm.gc_.get(), vm.builtins_.get());
  m.def("time", &Time);

  m.def("__repl_print__", [&out_stream](sr::Value result) {
    if (result.Type() != sr::ObjType::Nil)
      out_stream << result.Str() << std::endl;
  });
  m.def(
      "print",
      [&out_stream](std::string sep, std::string end, bool do_flush,
                    std::vector<sr::Value> args) {
        bool firstPrinted = false;
        for (auto const& v : args) {
          if (firstPrinted) {
            out_stream << sep;
          }
          out_stream << v.Str();
          firstPrinted = true;
        }

        out_stream << end;

        if (do_flush)
          out_stream.flush();
      },

      sb::kw_arg("sep") = " ", sb::kw_arg("end") = "\n",
      sb::kw_arg("flush") = false, sb::vararg);

  m.def(
      "input",
      [&in_stream, &out_stream](std::string prompt) {
        if (!prompt.empty())
          out_stream << std::move(prompt);
        std::string line;
        std::getline(in_stream, line);
        return line;
      },

      sb::kw_arg("prompt") = "");

  vm.builtins_->try_emplace(
      "import",
      sr::Value(gc->Allocate<sr::NativeFunction>(
          "import",
          [&in_stream, &out_stream, &err_stream](
              sr::VM& vm2, sr::Fiber& f, uint8_t nargs,
              uint8_t nkwargs) -> sr::TempValue {
            if (!(nargs == 1 && nkwargs == 0))
              throw std::runtime_error("import() expects module name");
            sr::Value argv = f.op_stack.back();
            f.op_stack.pop_back();
            std::string const& modstr = argv.Get_if<sr::String>()->str_;
            if (auto it = vm2.module_cache_.find(modstr);
                it != vm2.module_cache_.end())
              return sr::Value(it->second);

            sr::VM mvm = Create(vm2.gc_, out_stream, in_stream, err_stream);
            mvm.gc_threshold_ = 0;  // disable garbage collector
            auto import_builtins =
                std::make_shared<std::unordered_map<std::string, sr::Value>>();
            import_builtins->reserve(vm2.globals_->size() +
                                     vm2.builtins_->size());
            import_builtins->insert(vm2.globals_->begin(), vm2.globals_->end());
            import_builtins->insert(vm2.builtins_->begin(),
                                    vm2.builtins_->end());
            mvm.builtins_ = std::move(import_builtins);

            CompilerPipeline pipe(mvm.gc_, false);
            const std::filesystem::path modpth = modstr + ".sr";
            std::string modsrc;
            try {
              modsrc = LoadFileStr(modpth);
            } catch (std::runtime_error& re) {
              throw sr::RuntimeError("Failed to load module " + modstr + ": " +
                                     re.what());
            }

            auto sb = m6::SourceBuffer::Create(std::move(modsrc), modstr);
            pipe.compile(sb);
            if (!pipe.Ok())
              throw sr::RuntimeError(pipe.FormatErrors());
            sr::Code* chunk = pipe.Get();

            auto mod = std::make_unique<sr::Module>(modstr, mvm.globals_);
            {
              ScopedCurrentPath guard(modpth.parent_path());
              vm2.module_cache_[modstr] = mod.get();
              mvm.module_cache_ = vm2.module_cache_;
              mvm.Evaluate(chunk);
            }

            return std::move(mod);
          })));

  serilang::InstallAsyncBuiltins(vm);
  return vm;
}

}  // namespace m6
