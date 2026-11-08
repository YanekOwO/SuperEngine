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

#include "libsiglus/execution_serialization.hpp"
#include "core/object_internal/objdrawer.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/text_window.hpp"
#include "vm/function.hpp"
#include "vm/vm.hpp"

namespace libsiglus {
namespace sr = serilang;
namespace {
sr::SerializationHooks Hooks(SiglusMachine& machine) {
  sr::SerializationHooks hooks;
  hooks.external_name =
      [&machine](const sr::IObject& object) -> std::optional<std::string> {
    if (const auto* fn = dynamic_cast<const sr::Function*>(&object))
      if (auto scene = machine.loader->SceneId(fn))
        return std::to_string(*scene) + ":" +
               std::to_string(sr::CodeFingerprint(*fn->chunk));
    return std::nullopt;
  };
  hooks.resolve_external = [&machine](std::string_view name) -> sr::Value {
    auto separator = name.find(':');
    if (separator == std::string_view::npos)
      throw std::runtime_error("invalid saved scene reference");
    int scene = std::stoi(std::string(name.substr(0, separator)));
    auto fingerprint = std::stoull(std::string(name.substr(separator + 1)));
    auto* fn = machine.loader->Dispatcher(scene);
    if (sr::CodeFingerprint(*fn->chunk) != fingerprint)
      throw std::runtime_error("saved scene code does not match this game");
    return fn;
  };
  hooks.save_userdata = [](sr::ObjectWriter& out, const std::any& data) {
    const auto* memory = std::any_cast<MemoryHandle>(&data);
    if (!memory)
      throw std::runtime_error(
          "unsupported Siglus fiber userdata in checkpoint");
    const auto stack = memory->GetStackMemory();
    out.Put(stack.L);
    out.Put(stack.K);
  };
  hooks.load_userdata = [&machine](sr::ObjectReader& in) -> std::any {
    StackMemory stack;
    in.Archive() >> stack.L >> stack.K;
    if (stack.L.size() != stack.K.size())
      throw std::runtime_error("mismatched saved L/K banks");
    auto memory = machine.host().memory->CreateHandle();
    memory.ResetStackMemory(std::move(stack));
    return memory;
  };
  return hooks;
}
}  // namespace
void RememberBindingSymbols(SiglusMachine& machine) {
  sr::RememberBindingSymbols(*machine.vm);
}
void ExecutionWriter::WriteMachine(boost::archive::binary_oarchive& archive,
                                   SiglusMachine& machine,
                                   sr::Fiber* restored_savepoint) {
  sr::ObjectWriter out(archive, *machine.vm, session_, Hooks(machine));
  if (restored_savepoint && !restored_savepoint->op_stack.empty())
    out.stack_override = {restored_savepoint,
                          restored_savepoint->op_stack.size() - 1,
                          sr::Value(1)};
  machine.vm->Save(out);
  auto frames = machine.frame_requests->SaveState();
  auto frame_request = [&](const auto& request) {
    out.WritePromise(request.promise);
    out.Put(request.advance_script_input);
  };
  out.Sequence(frames.pending, frame_request);
  out.Sequence(frames.active, frame_request);
  out.WritePromise(machine.entry_completion);
  out.WritePromise(machine.caller_completion);
  SelectionExecutionState selection;
  if (const auto& state = machine.selection_state)
    selection = {.present = true,
                 .prepared = state->prepared_,
                 .active = state->active_,
                 .last_message = state->last_message_,
                 .mouse_x = state->mouse_.x(),
                 .mouse_y = state->mouse_.y()};
  archive << selection;
  const auto waits = CaptureNativeWaits(machine, *this);
  archive << waits;
  out.Finish();
}
std::vector<NativeWaitState> ExecutionReader::ReadMachine(
    boost::archive::binary_iarchive& archive,
    SiglusMachine& machine) {
  sr::ObjectReader in(archive, *machine.vm, session_, Hooks(machine));
  machine.vm->Load(in);
  FrameRequestManager::SavedState frames;
  auto frame_request = [&] {
    auto promise = in.ReadPromise();
    auto advance = in.Get<bool>();
    if (!promise)
      throw std::runtime_error("saved frame request has no promise");
    return FrameRequestManager::FrameRequest{promise, advance};
  };
  for (auto n = in.Count(); n; --n)
    frames.pending.push_back(frame_request());
  for (auto n = in.Count(); n; --n)
    frames.active.push_back(frame_request());
  machine.frame_requests->RestoreState(std::move(frames));
  machine.entry_completion = in.ReadPromise();
  machine.caller_completion = in.ReadPromise();
  SelectionExecutionState selection;
  archive >> selection;
  if (selection.present) {
    if (!machine.selection_state)
      throw std::runtime_error("saved machine requires selection bindings");
    auto& state = *machine.selection_state;
    state.prepared_ = selection.prepared;
    state.active_ = selection.active;
    state.last_message_ = selection.last_message;
    state.mouse_ = Point(selection.mouse_x, selection.mouse_y);
    state.held_keys_.clear();
    state.cancel_pressed_ = false;
  }
  auto waits = in.Get<std::vector<NativeWaitState>>();
  in.Finish();
  return waits;
}
}  // namespace libsiglus
