// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 RLVM contributors
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

#include "libsiglus/bindings/trace.hpp"

#include "libsiglus/bindings/registry.hpp"
#include "log/trace.hpp"
#include "srbind/srbind.hpp"
#include "utilities/string_utilities.hpp"
#include "vm/function.hpp"
#include "vm/instruction.hpp"
#include "vm/object.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace libsiglus::binding {
namespace sr = serilang;
namespace sb = srbind;

namespace {

constexpr char kFrameActionTraceTag[] = "%%frame_action_trace";

void BindTrace(SiglusMachine& runtime) {
  auto& vm = *runtime.vm;
  sb::module_ module(vm.gc_.get(), vm.globals_.get());
  module.def("__builtin_dbgvalue",
             [](sr::VM& vm, sr::Value value) -> sr::Value {
               std::string content;
               if (const auto* str = value.Get_if<sr::String>())
                 content = '"' + EncodeText(str->str_) + '"';
               else
                 content = value.Str();
               return vm.gc_->Allocate<sr::String>(std::move(content));
             });
  module.def(
      "__builtin_trace",
      [](sr::Fiber& fiber, int scene, int line, std::vector<sr::Value> terms) {
        const bool frame_action =
            std::ranges::any_of(fiber.frames, [](const sr::CallFrame& frame) {
              return frame.fn->globals &&
                     frame.fn->globals->contains(kFrameActionTraceTag);
            });
        WriteTrace(
            frame_action ? TraceCategory::FrameAction : TraceCategory::Script,
            {scene, line}, [&] {
              std::string content;
              for (const auto& term : terms)
                content += term.Str();
              return content;
            });
      },
      sb::arg("scene"), sb::arg("line"), sb::vararg);
}

}  // namespace

void InvokeFrameAction(sr::VM& vm,
                       sr::Value callback,
                       std::vector<sr::Value> arguments,
                       std::any initial_userdata) {
  if (!IsTraceEnabled(TraceCategory::Script) &&
      !IsTraceEnabled(TraceCategory::FrameAction)) {
    vm.InvokeUntilBlocked(std::move(callback), std::move(arguments),
                          std::move(initial_userdata));
    return;
  }
  if (arguments.size() > std::numeric_limits<uint8_t>::max())
    throw sr::RuntimeError(
        "callback supports at most 255 positional arguments");

  auto* code = vm.gc_->Allocate<sr::Code>();
  code->const_pool = {std::move(callback)};
  code->Append(sr::Push{0});
  for (auto& argument : arguments) {
    code->Append(sr::Push{static_cast<uint32_t>(code->const_pool.size())});
    code->const_pool.emplace_back(std::move(argument));
  }
  code->Append(sr::Call{.argcnt = static_cast<uint32_t>(arguments.size())});
  code->Append(sr::Return{});
  auto* wrapper = vm.gc_->Allocate<sr::Function>(code);
  // A private wrapper namespace identifies this fiber without touching its
  // call-memory data or the completion promise replaced by an explicit yield.
  wrapper->globals =
      std::make_shared<std::unordered_map<std::string, sr::Value>>();
  wrapper->globals->emplace(kFrameActionTraceTag, true);
  vm.InvokeUntilBlocked(*wrapper, {}, std::move(initial_userdata));
}

RLVM_REGISTER(SiglusBindingRegistry, "trace", BindTrace)

}  // namespace libsiglus::binding
