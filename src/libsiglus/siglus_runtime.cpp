// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
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

#include "libsiglus/siglus_runtime.hpp"
#include "core/property_storage.hpp"
#include "libsiglus/backlog.hpp"
#include "libsiglus/bindings/frame_action.hpp"
#include "libsiglus/bindings/object.hpp"

#include "core/frame_action_manager.hpp"
#include "core/input.hpp"
#include "core/kidoku_table.hpp"
#include "core/mask.hpp"
#include "core/message_back.hpp"
#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/loader.hpp"
#include "libsiglus/bindings/memory.hpp"
#include "libsiglus/bindings/trace.hpp"
#include "libsiglus/property.hpp"
#include "libsiglus/save_load.hpp"
#include "libsiglus/scene_entry.hpp"
#include "libsiglus/siglus_scene_renderer.hpp"
#include "long_operations/siglus_movie.hpp"
#include "systems/event_system.hpp"
#include "systems/sound_system.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"
#include "vm/exception.hpp"
#include "vm/future.hpp"
#include "vm/instruction.hpp"
#include "vm/list.hpp"
#include "vm/promise.hpp"
#include "vm/string.hpp"
#include "vm/transfer_value.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>

namespace libsiglus {
namespace sr = serilang;

SiglusRuntime::SiglusRuntime()
    : message_back(std::make_shared<MessageBack>()),
      gameplay_(std::make_unique<SiglusMachine>(*this)) {
  *finish_active_wipe = [this] {
    std::erase_if(wipe_hooks,
                  [](const auto& hook) { return hook.lifetime.expired(); });
    for (const auto& hook : wipe_hooks)
      hook.finish();
  };
  update_wipe_after_frame_actions = [this] {
    std::erase_if(wipe_hooks,
                  [](const auto& hook) { return hook.lifetime.expired(); });
    for (const auto& hook : wipe_hooks)
      hook.update();
  };
}
SiglusSaveLoad& SiglusRuntime::Saves() {
  if (!saves_)
    saves_ = std::make_unique<SiglusSaveLoad>(*this);
  return *saves_;
}

SiglusBacklog& SiglusRuntime::Backlog() {
  if (!backlog) {
    backlog = std::make_shared<SiglusBacklog>(*this);
    system->event().AddListener(35, backlog);
    system->text().SetBacklogView(
        [this] { return backlog && backlog->IsOpen(); },
        [this](int direction) {
          if (backlog->IsOpen())
            backlog->Scroll(direction * 120);
          else if (direction < 0)
            backlog->Open(IsExcall() && excall && excall->allocated ? 1 : 0);
        },
        [this] { backlog->Close(); });
  }
  return *backlog;
}

void SiglusRuntime::RegisterCounter(std::shared_ptr<Counter> counter,
                                    int owner,
                                    bool persistent) {
  counters_.push_back({std::move(counter), owner, persistent});
}
SiglusMachine& SiglusRuntime::GameplayMachine() { return *gameplay_; }
const SiglusMachine& SiglusRuntime::GameplayMachine() const {
  return *gameplay_;
}
SiglusMachine& SiglusRuntime::ActiveMachine() {
  return excalls_.empty() ? *gameplay_ : *excalls_.back();
}

const SiglusMachine& SiglusRuntime::ActiveMachine() const {
  return excalls_.empty() ? *gameplay_ : *excalls_.back();
}

SiglusRuntime::~SiglusRuntime() {
  pending_excalls_.clear();
  excalls_.clear();
  callbacks_[0].reset();
  callbacks_[1].reset();
  if (excall)
    excall->Reset();
  // Native VM instances and pending coroutine frames keep non-owning pointers
  // to runtime services. Finalize them before member destruction tears those
  // services down.
  if (auto s = selection_state) {
    s->pending_.reset();
    if (s->active_)
      CancelSelection(s->active_);
    s->prepared_.reset();
  }

  if (gameplay_->frame_requests)
    gameplay_->frame_requests->Reset();
  ResetCaptures();
  GameplayMachine().vm.reset();
  if (system) {
    system->text().BindGraphics({});
    if (stage)
      stage->Reset();
    system->text().Reset();
  }
  captures.reset();
  renderer.reset();
}

void SiglusRuntime::InitializeExcall() {
  if (!excall)
    excall = std::make_unique<Excall>();
}

Stage& SiglusRuntime::GetStage(int owner) {
  if (owner == 0 && stage)
    return *stage;
  if (owner != 1)
    throw sr::RuntimeError("invalid stage owner");
  InitializeExcall();
  excall->RequireAllocated();
  return excall->stage;
}

FrameActionManager& SiglusRuntime::GetFrameActions(int owner) {
  if (owner == 0 && frame_action_manager)
    return *frame_action_manager;
  if (owner != 1)
    throw sr::RuntimeError("invalid frame-action owner");
  InitializeExcall();
  excall->RequireAllocated();
  return *excall->actions;
}

void SiglusRuntime::Activate(SiglusMachine& machine) {
  selection_state = machine.selection_state;
  if (system) {
    system->text().set_in_selection_mode(
        selection_state && selection_state->active_ &&
        !selection_state->active_->CanResume());
    if (machine.saved_window)
      system->text().SetCurrentMwnd(machine.saved_window);
  }
  if (script_input)
    script_input->Clear();
  if (input_event_listener)
    input_event_listener->ClearState();
}

void SiglusRuntime::InitializeProperties() {
  auto MakePropertyTable =
      [](std::span<const Property> properties) -> PropertyStorage::Table {
    PropertyStorage::Table table;
    table.reserve(properties.size());
    for (const auto& property : properties) {
      auto entry = std::make_shared<PropertyStorage::Entry>();
      switch (property.form) {
        case Type::Int:
          entry->value = 0;
          break;
        case Type::String:
          entry->value = std::string{};
          break;
        case Type::IntList:
        case Type::StrList:
          if (property.size < 0)
            throw std::invalid_argument("negative property array size");
          entry->initial_size = property.size;
          if (property.form == Type::IntList)
            entry->value = PropertyStorage::Integers(property.size);
          else
            entry->value = PropertyStorage::Strings(property.size);
          break;
        default:
          break;
      }
      table.push_back(std::move(entry));
    }
    return table;
  };

  PropertyStorage::Tables tables;
  tables.emplace(-1, MakePropertyTable(archive->prop_));
  for (std::size_t i = 0; i < archive->GetScenarioCount(); ++i)
    tables.emplace(
        static_cast<int>(i),
        MakePropertyTable(archive->ParseScene(static_cast<int>(i)).property));
  properties.Initialize(std::move(tables));
}

bool SiglusRuntime::ScriptScopeActive(int owner) const {
  return owner == 1 || !IsExcall();
}

void SiglusRuntime::FreeExcall(bool finish_actions) {
  if (backlog && backlog->IsOpen() && backlog->Capture()->owner == 1)
    backlog->Close();
  if (!excall || (excall->releasing && finish_actions))
    return;
  if (excall->allocated && finish_active_wipe && *finish_active_wipe)
    (*finish_active_wipe)();
  excall->Free(finish_actions);
  if (callbacks_[1]) {
    if (IsDriving(*callbacks_[1])) {
      callbacks_[1]->lifetime.reset();
      callbacks_[1]->vm->RequestHostYield();
    } else
      callbacks_[1].reset();
  }
  std::erase_if(counters_, [](const auto& counter) {
    if (counter.owner != 1)
      return false;
    if (auto value = counter.counter.lock())
      value->Stop();
    return true;
  });
}

bool SiglusRuntime::IsCallbackExecution(const SiglusMachine& machine) const {
  for (auto* current = &machine; current; current = current->caller) {
    if (current->callback_owner >= 0)
      return true;
    if (current->caller_lifetime.expired())
      break;
  }
  return false;
}

bool SiglusRuntime::HasPendingCallbacks() const {
  for (const auto& callback : callbacks_)
    if (callback && callback->lifetime && callback->vm->HasPendingWork())
      return true;
  // A callback may have launched an EX call without awaiting its result.
  for (const auto& machine : excalls_)
    if (IsCallbackExecution(*machine))
      return true;
  return false;
}

void SiglusRuntime::DispatchFrameAction(int owner,
                                        const ActionReference& reference,
                                        ScriptCall call) {
  if (restart_)
    return;
  auto& slot = callbacks_[owner];
  if (slot && !slot->lifetime) {
    if (IsDriving(*slot))
      return;
    slot.reset();
  }
  if (!slot) {
    slot = std::make_unique<SiglusMachine>(*this, owner);
    slot->Initialize();
  }
  auto& machine = *slot;
  auto& vm = *machine.vm;
  const auto* target = std::get_if<CmdTarget>(&call.target);
  if (!target)
    throw sr::RuntimeError("frame-action callback requires a command target");
  const auto types = machine.loader->GetUserCommandArgs(*target);
  const std::size_t prefix = reference.owner ? 2 : 1;
  if (types.size() < prefix || types[0] != Type::FrameAction ||
      (reference.owner && types[1] != Type::Object)) {
    throw sr::RuntimeError("frame-action command has incompatible signature");
  }
  if (!reference.action)
    throw sr::RuntimeError("frame-action handle is null");
  std::vector<sr::Value> arguments;
  arguments.emplace_back(
      machine.class_registry.MakeInst<binding::FrameActionHandle>(
          reference, &GetFrameActions(owner)));
  if (reference.owner)
    arguments.emplace_back(
        machine.class_registry.MakeInst<binding::ObjectHandle>(reference.owner,
                                                               owner));

  std::size_t int_it = 0, str_it = 0;
  for (const auto type : types.subspan(prefix)) {
    if (type == Type::Int && int_it < call.integers.size()) {
      arguments.emplace_back(call.integers[int_it++]);
    } else if (type == Type::String && str_it < call.strings.size()) {
      arguments.emplace_back(
          vm.gc_->Allocate<sr::String>(call.strings[str_it++]));
    } else {
      throw sr::RuntimeError(
          "frame-action arguments do not match command signature");
    }
  }
  if (int_it != call.integers.size() || str_it != call.strings.size())
    throw sr::RuntimeError(
        "frame-action arguments do not match command signature");

  DriveMachine(machine, [&] {
    binding::InvokeFrameAction(
        vm, machine.loader->ResolveUserCommandTarget(*target),
        std::move(arguments), binding::MakeCallMemory(*memory));
  });
}

void SiglusRuntime::ResetExcall() {
  resetting_ = true;
  for (auto& request : pending_excalls_)
    if (request.completion && !request.completion->HasResult())
      request.completion->Reject("EX call cancelled by runtime reset");
  pending_excalls_.clear();
  for (auto& machine : excalls_) {
    if (machine->caller_completion && !machine->caller_completion->HasResult())
      machine->caller_completion->Reject("EX call cancelled by runtime reset");
    if (IsDriving(*machine)) {
      machine->lifetime.reset();
      machine->vm->RequestHostYield();
    } else
      machine->Cancel();
  }
  YieldScripts();
  excall_depth_ = 0;
  FreeExcall(false);
  if (excall)
    excall->Reset();
  syscom_enabled = true;
}

void SiglusRuntime::ResetLocalMemory(bool preserve_backlog) {
  if (backlog)
    backlog->Close();
  if (saves_)
    saves_->Reset(preserve_backlog);
  if (memory) {
    auto global = memory->GetGlobalMemory();
    memory->PartialReset(LocalMemory{});
    if (gameexe)
      memory->LoadFrom(*gameexe);
    memory->PartialReset(std::move(global));
  }
  for (auto& cb : gameplay_->reset_local_memory)
    cb();
  for (auto& cb : reset_local_memory)
    cb();
}

bool SiglusRuntime::OpenExcall(CmdTarget target) {
  return OpenExcall(ScriptCall{.target = target});
}

bool SiglusRuntime::OpenExcall(ScriptCall request) {
  pending_excalls_.push_back({std::move(request)});
  YieldScripts();
  return true;
}

std::shared_ptr<sr::Promise> SiglusRuntime::CallExcall(SiglusMachine& caller,
                                                       ScriptCall request) {
  auto completion = std::make_shared<sr::Promise>();
  caller.vm->TrackPendingPromise(completion);
  pending_excalls_.push_back(
      {std::move(request), &caller, caller.lifetime, completion});
  caller.vm->RequestHostYield();
  YieldScripts();
  return completion;
}

void SiglusRuntime::ApplyExcallRequests() {
  while (!pending_excalls_.empty()) {
    auto request = std::move(pending_excalls_.front());
    pending_excalls_.pop_front();
    if (request.caller && request.lifetime.expired())
      continue;

    auto machine = std::make_unique<SiglusMachine>(*this);
    machine->caller = request.caller;
    machine->caller_lifetime = request.lifetime;
    machine->caller_completion = request.completion;
    if (system)
      ActiveMachine().saved_window = system->text().GetCurrentMwnd();
    try {
      machine->Initialize();
      auto entry = machine->loader->ResolveScriptCall(request.request);
      auto* fiber = machine->vm->ScheduleExecute(entry);
      if (request.request.farcall) {
        // fresh, empty L/K banks for the excall
        fiber->usrdata = binding::MakeCallMemory(
            *memory, request.request.integers, request.request.strings);
      } else if (std::holds_alternative<CmdTarget>(request.request.target)) {
        fiber->usrdata = binding::MakeCallMemory(*memory);
      }
      machine->entry_completion = fiber->completion_promise;
      machine->entry_completion->AddWaker(
          [vm = machine->vm.get()](const auto& result) {
            if (result.has_value())
              (*vm->globals_)["%%entry_result"] = result.value();
          });

      // enter excall
      excalls_.push_back(std::move(machine));
      ++excall_depth_;
      Activate(ActiveMachine());
    } catch (const std::exception& e) {
      if (request.completion)
        request.completion->Reject(e.what());
      else
        throw;
    }
  }
}

void SiglusRuntime::CollectCompletedExcalls() {
  if (resetting_) {
    excalls_.clear();
    callbacks_[1].reset();
    resetting_ = false;
    Activate(*gameplay_);
  }

  while (!excalls_.empty() && excalls_.back()->entry_completion->HasResult()) {
    auto& machine = *excalls_.back();
    const auto& result = *machine.entry_completion->result;
    auto completion = machine.caller_completion;
    const bool caller_alive =
        machine.caller && !machine.caller_lifetime.expired();
    if (completion && caller_alive) {
      if (result.has_value()) {
        try {
          const auto copied = sr::TransferValue::Capture(result.value());
          completion->Resolve(copied.Restore(*machine.caller->vm->gc_));
        } catch (const std::exception& e) {
          completion->Reject(e.what());
        }
      } else
        completion->Reject(result.error());
    }
    const auto error = !result.has_value() && !completion
                           ? std::optional<std::string>(result.error())
                           : std::nullopt;
    // leave excall
    excalls_.pop_back();
    if (excall_depth_ > 0)
      --excall_depth_;
    Activate(ActiveMachine());
    if (error)
      throw sr::UnhandledError(*error);
  }
}

void SiglusRuntime::QueueReturnToMenu(std::string scene,
                                      int zlabel,
                                      bool preserve,
                                      int fade_millis) {
  if (finish_active_wipe && *finish_active_wipe)
    (*finish_active_wipe)();
  restart_ =
      RestartRequest{std::move(scene), zlabel, preserve,
                     std::chrono::steady_clock::now() +
                         std::chrono::milliseconds(std::max(0, fade_millis))};
  YieldScripts();
}

bool SiglusRuntime::ProcessRestart() {
  if (!restart_)
    return false;
  if (std::chrono::steady_clock::now() < restart_->deadline)
    return true;
  const auto request = std::exchange(restart_, std::nullopt).value();
  pending_excalls_.clear();
  for (auto& machine : excalls_)
    machine->Cancel();
  for (auto& machine : callbacks_)
    if (machine)
      machine->Cancel();
  gameplay_->Cancel();
  if (frame_action_manager)
    frame_action_manager->CancelAll();
  if (excall && excall->actions)
    excall->actions->CancelAll();
  if (finish_active_wipe && *finish_active_wipe)
    (*finish_active_wipe)();
  ResetCaptures();
  ResetLocalMemory(request.preserve);
  excalls_.clear();
  callbacks_[0].reset();
  callbacks_[1].reset();
  resetting_ = false;
  excall_depth_ = 0;
  if (stage)
    stage->Reset();
  if (mask_list)
    mask_list->Reset();
  ResetGraphics();
  if (system) {
    if (request.preserve) {
      system->sound().Reset();
      system->EnableSyscom();
    } else
      system->Reset();
  }
  gameplay_ = std::make_unique<SiglusMachine>(*this);
  gameplay_->Initialize();
  Activate(*gameplay_);
  gameplay_->vm->ScheduleExecute(
      gameplay_->loader->ResolveSceneEntry(request.scene, request.zlabel));
  return false;
}

void SiglusRuntime::DriveScripts() {
  for (;;) {
    CollectCompletedExcalls();
    ApplyExcallRequests();
    if (saves_ && saves_->Process())
      return;
    if (ProcessRestart())
      return;
    CollectCompletedExcalls();
    ApplyExcallRequests();
    if (backlog && backlog->IsOpen())
      return;
    auto& machine = ActiveMachine();
    DriveMachine(machine, [&] { machine.vm->RunReadyUntilBlocked(); });
    const bool checkpoint = saves_ && saves_->CheckpointPending();
    if (saves_ && saves_->Process())
      return;
    CollectCompletedExcalls();
    ApplyExcallRequests();
    // Capturing a checkpoint yields to the host without requesting a display
    // boundary. Resume immediately so a cleared message window is not drawn
    // before the next text submission. Native/frame waits still end this drive.
    if (!checkpoint || (saves_ && saves_->CheckpointPending()))
      return;
  }
}

bool SiglusRuntime::OpenExcallScene(const std::string& key) {
  if (!gameexe || IsExcall() || !pending_excalls_.empty())
    return false;
  const auto target = (*gameexe)(key);
  const auto name = target.StrAt(0).value_or("");
  if (name.empty())
    return false;
  return OpenExcall(
      ScriptCall{.target = SceneTarget{name, target.IntAt(1).value_or(0)}});
}

sr::Value SiglusRuntime::RequestCapture(SiglusMachine& machine,
                                        CaptureRequest request) {
  if (!captures)
    throw std::logic_error("Capture manager is unavailable");
  auto* future = machine.vm->gc_->Allocate<sr::Future>();
  auto prepared = captures->PrepareRequest(std::move(request));
  if (!prepared) {
    future->promise->Resolve(sr::nil);
    return future;
  }
  machine.vm->TrackPendingPromise(future->promise);
  pending_captures_.push_back(
      {std::move(*prepared), &machine, machine.lifetime, future->promise});
  return future;
}

bool SiglusRuntime::CanProcessCapture(const PendingCapture& job) const {
  if (job.lifetime.expired())
    return false;
  if (job.owner == &ActiveMachine())
    return true;
  for (int owner : {0, 1})
    if (job.owner == callbacks_[owner].get() && callbacks_[owner]->lifetime &&
        (owner == 0 || (excall && excall->allocated)))
      return true;
  return false;
}

bool SiglusRuntime::HasPendingCaptures() const {
  return std::ranges::any_of(pending_captures_, [this](const auto& job) {
    return CanProcessCapture(job);
  });
}

bool SiglusRuntime::CaptureDisplayEnabled() const {
  const auto job = std::ranges::find_if(
      pending_captures_,
      [this](const auto& request) { return CanProcessCapture(request); });
  return job == pending_captures_.end() || job->request.display;
}

void SiglusRuntime::CancelExpiredCaptures() {
  std::erase_if(pending_captures_, [](const auto& job) {
    if (!job.lifetime.expired())
      return false;
    if (!job.promise->HasResult())
      job.promise->Reject("Capture cancelled by execution reset");
    return true;
  });
}

void SiglusRuntime::ProcessCaptureFrame() {
  CancelExpiredCaptures();
  const auto ready = std::ranges::find_if(
      pending_captures_,
      [this](const auto& job) { return CanProcessCapture(job); });
  if (ready == pending_captures_.end())
    return;
  auto job = std::move(*ready);
  pending_captures_.erase(ready);
  if (job.promise->HasResult())
    return;

  try {
    const auto result = captures->Capture(job.request);
    job.promise->Resolve(result ? sr::Value(*result) : sr::nil);
  } catch (const std::exception& e) {
    job.promise->Reject(e.what());
  }
}

void SiglusRuntime::ResetCaptures() {
  auto pending = std::move(pending_captures_);
  pending_captures_.clear();
  if (captures)
    captures->Reset();
  for (auto& job : pending)
    if (!job.promise->HasResult())
      job.promise->Reject("Capture cancelled by runtime reset");
}

void SiglusRuntime::Start(std::optional<int> start_scene) {
  if (!GameplayMachine().vm || !GameplayMachine().loader || !gameexe)
    throw std::logic_error(
        "SiglusRuntime::Start requires a VM, loader and Gameexe");

  sr::Value script;
  if (start_scene.has_value()) {
    // An explicit --scene ID starts at the scene's main script.
    sr::Module* mod = GameplayMachine().loader->Load(*start_scene);
    const auto it = mod->globals->find("%%script");
    if (it != mod->globals->end())
      script = it->second;
    if (script.Type() == sr::ObjType::Nil)
      throw sr::RuntimeError(
          std::format("Scene {}: %%script not found", *start_scene));
  } else {
    auto start = (*gameexe)("START_SCENE");
    script = ActiveMachine().loader->ResolveSceneEntry(
        start.StrAt(0).value_or("_start"), start.IntAt(1).value_or(0));
  }
  GameplayMachine().vm->ScheduleExecute(script);
}

void SiglusRuntime::Update(std::chrono::milliseconds real_delta,
                           std::chrono::milliseconds game_delta) {
  CancelExpiredCaptures();
  if ((backlog && backlog->IsOpen()) || (saves_ && saves_->PauseGameTime()))
    return;
  const bool dispatch = !saves_ || !saves_->CheckpointPending();
  SiglusMachine* callback_frames[] = {callbacks_[0].get(), callbacks_[1].get()};
  for (int owner : {0, 1}) {
    auto* callback = callback_frames[owner];
    if (callback && callback->lifetime)
      callback->frame_requests->BeginFrameRequests();
  }
  using ms = std::chrono::milliseconds;
  real_delta = std::max(ms::zero(), real_delta);
  game_delta = std::max(ms::zero(), game_delta);
  total_play_time += real_delta.count();
  if (selection_state && (IsExcall() || !local_config.time_stop))
    UpdateSelection(game_delta.count());

  const bool time_stopped = local_config.time_stop;
  if (system) {
    system->text().AdvanceMessageText(time_stopped ? ms::zero() : game_delta);
    system->text().AdvanceExcallText(game_delta);
  }
  if (!time_stopped && !local_config.counter_time_stop && counter_list)
    counter_list->Advance(game_delta, real_delta);

  // Advance before callbacks can create new timers. Such timers must not
  // receive the elapsed time from before their creation.
  std::erase_if(counters_,
                [this, real_delta, game_delta, time_stopped](const auto& ptr) {
                  if (auto counter = ptr.counter.lock()) {
                    if ((!time_stopped && ptr.owner == 0) ||
                        (ptr.owner == 1 && excall && excall->allocated))
                      counter->Advance(game_delta, real_delta);
                    return false;
                  }
                  return true;
                });

  if (!time_stopped && screen_quakes)
    screen_quakes->Advance(game_delta.count());

  if (!time_stopped && !local_config.stage_time_stop && stage)
    stage->AdvanceQuakes(game_delta.count());
  if (frame_action_manager) {
    frame_action_manager->Advance(real_delta, game_delta);
    if (dispatch)
      frame_action_manager->Dispatch();
  }
  if (excall && excall->allocated) {
    excall->counters->Advance(game_delta, real_delta);
    excall->stage.AdvanceQuakes(game_delta.count());
    excall->actions->Advance(real_delta, game_delta);
    if (dispatch)
      excall->actions->Dispatch();
  }
  for (int owner : {0, 1}) {
    auto& callback = callbacks_[owner];
    if (callback && !callback->lifetime)
      callback.reset();
    if (callback && !restart_ &&
        (owner == 0 || (excall && excall->allocated))) {
      if (callback.get() == callback_frames[owner])
        callback->frame_requests->CompleteFrameRequests(nullptr);
      DriveMachine(*callback, [&] { callback->vm->RunReadyUntilBlocked(); });
    }
  }
  if (update_wipe_after_frame_actions)
    update_wipe_after_frame_actions();
}

void SiglusRuntime::PumpHostEvents() {
  system->event().PumpHostEvents();
  if (system->IsQuitRequested()) {
    GameplayMachine().vm->RequestStop();
    for (auto* machine : drive_stack_)
      machine->vm->RequestStop();
  }
  if (!pending_excalls_.empty())
    YieldScripts();
}

void SiglusRuntime::DriveMachine(SiglusMachine& machine,
                                 std::function<void()> run) {
  drive_stack_.push_back(&machine);
  try {
    run();
  } catch (...) {
    drive_stack_.pop_back();
    throw;
  }
  drive_stack_.pop_back();
}

bool SiglusRuntime::IsDriving(const SiglusMachine& machine) const {
  return std::ranges::find(drive_stack_, &machine) != drive_stack_.end();
}

void SiglusRuntime::YieldScripts(bool include_callbacks) {
  if (include_callbacks) {
    for (auto* machine : drive_stack_)
      machine->vm->RequestHostYield();
    return;
  }
  // Checkpoints leave callbacks running until their current invocation ends.
  gameplay_->vm->RequestHostYield();
  for (auto& machine : excalls_)
    if (!IsCallbackExecution(*machine))
      machine->vm->RequestHostYield();
}

bool SiglusRuntime::HasPendingScriptWork() const {
  if (saves_ && saves_->pending())
    return true;
  if (ActiveMachine().vm->HasPendingWork() || !pending_excalls_.empty() ||
      !excalls_.empty() || restart_)
    return true;
  for (int owner : {0, 1}) {
    const auto& callback = callbacks_[owner];
    if (callback && callback->lifetime &&
        (owner == 0 || (excall && excall->allocated)) &&
        callback->vm->HasPendingWork())
      return true;
  }
  return false;
}

void SiglusRuntime::Run() {
  if (!GameplayMachine().vm || !system || !gameplay_->frame_requests)
    throw std::logic_error(
        "SiglusRuntime::Run requires a VM, system and frame request manager");

  using FrameClock = std::chrono::steady_clock;
  // assuming max 144 fps
  constexpr auto frame_period =
      std::chrono::duration_cast<FrameClock::duration>(
          std::chrono::seconds(1)) /
      144;
  auto last_frame_tick = GameplayMachine().vm->scheduler_.GetTime();

  while ((!GameplayMachine().vm->IsStopRequested() || restart_) &&
         !system->IsQuitRequested() && HasPendingScriptWork()) {
    const auto next_frame = FrameClock::now() + frame_period;
    CollectCompletedExcalls();
    ApplyExcallRequests();
    auto* frame_machine = &ActiveMachine();
    frame_machine->frame_requests->BeginFrameRequests();
    const bool capture_frame = HasPendingCaptures();
    input_event_listener->ResetState();

    // Poll events
    system->event().ExecuteEventSystem();

    // Advance clock
    if (!backlog || !backlog->IsOpen()) {
      if (IsExcall() && excall && excall->allocated &&
          excall_interaction_manager)
        excall_interaction_manager->Update();
      else if (interaction_manager)
        interaction_manager->Update();
    }
    const auto now = GameplayMachine().vm->scheduler_.GetTime();
    const auto real_delta = std::max(
        std::chrono::duration_cast<Clock::duration_t>(now - last_frame_tick),
        Clock::duration_t::zero());
    last_frame_tick = now;

    const bool fast_forward = system->ShouldFastForward();
    const auto fast_forward_delta = real_delta > Clock::duration_t::max() / 32
                                        ? Clock::duration_t::max()
                                        : real_delta * 32;
    const auto game_delta = fast_forward ? fast_forward_delta : real_delta;
    Update(real_delta, game_delta);

    // Update Text and Sound subsystems
    system->text().ExecuteTextSystem();
    system->sound().ExecuteSoundSystem();

    if (renderer && (!backlog || !backlog->IsOpen()))
      renderer->ExecuteFrame();

    if (GameplayMachine().vm->IsStopRequested() || system->IsQuitRequested())
      break;

    if (!capture_frame || CaptureDisplayEnabled())
      RenderFrame();
    if (capture_frame)
      ProcessCaptureFrame();
    frame_machine->frame_requests->CompleteFrameRequests(
        frame_machine == &ActiveMachine() ? script_input.get() : nullptr);

    DriveScripts();
    if (GameplayMachine().vm->IsStopRequested() || system->IsQuitRequested() ||
        !HasPendingScriptWork()) {
      break;
    }

    std::this_thread::sleep_until(next_frame);
  }

  if (system->IsQuitRequested())
    GameplayMachine().vm->RequestStop();
  if (GameplayMachine().vm->IsStopRequested()) {
    gameplay_->frame_requests->Reset();
    system->event().DiscardDeferredInput();
  }
}

void SiglusRuntime::BindGraphics() {
  window_title->LoadSiglus(*gameexe);
  ResetGraphics();
  system->text().BindGraphics(
      {.images = images,
       .interface_hidden = [this] { return is_interface_hidden(); },
       .toggle_interface = [this] { ToggleInterfaceHidden(); },
       .renderer = renderer});
}

void SiglusRuntime::ResetGraphics() {
  interface_hidden_ = false;
  if (window_title) {
    window_title->base_title_.clear();
    window_title->subtitle_.clear();
    window_title->subtitle_visible_ = false;
    window_title->Apply();
  }
  if (presenter)
    presenter->backend()->ShowSystemCursor(true);
}

void SiglusRuntime::RenderFrame() {
  presenter->RenderFrame({}, [this] {
    if (const auto custom = GetFrameRenderer())
      custom();
    else if (renderer)
      renderer->RenderScene();
    if (backlog)
      backlog->Render();
    if (saves_)
      saves_->RenderOverlay();
  });
}

std::shared_ptr<SDLSurface> SiglusRuntime::RenderToSurface() {
  return presenter->RenderToSurface([this] { renderer->RenderScene(); });
}

std::function<void()> SiglusRuntime::GetFrameRenderer() const {
  if (const auto frame = movie_frame.lock())
    return frame->draw;
  return {};
}

std::shared_ptr<Counter> SiglusRuntime::CreateCounter(int owner,
                                                      bool persistent) {
  auto ret = std::make_shared<Counter>();
  counters_.push_back({ret, owner, persistent});
  return ret;
}

// ------------------------------------------------------------------------------
void SiglusRuntime::RegisterDebugSelectionChoice(int scene,
                                                 int kidoku,
                                                 int choice) {
  if (choice < -1)
    throw sr::RuntimeError(
        "Selection choice must be -1 or a nonnegative index");
  debug_selection_choices_.insert_or_assign({scene, kidoku}, choice);
}

void SiglusRuntime::QueueDebugSelectionChoice(int choice) {
  if (choice < -1)
    throw sr::RuntimeError(
        "Selection choice must be -1 or a nonnegative index");
  debug_selection_choice_queue_.push_back(choice);
}

void SiglusRuntime::OnSelectionChosen(const Selection& s) {
  const int index = s.result.value();
  if (system) {
    const int number = index < 0 ? 2 : 1;
    if (system->sound().HasSe(number))
      system->sound().PlaySe(number);
  }
  if (kidoku_table && s.request.scene >= 0 && s.request.kidoku >= 0)
    kidoku_table->RecordKidoku(s.request.scene, s.request.kidoku);
  // Selection history copies the established checkpoint, including cancels.
  if (saves_ && saves_->manager().HasCheckpoint())
    saves_->manager().selection_points().Mark();
}

void SiglusRuntime::StartSelection(std::shared_ptr<Selection> s) {
  std::shared_ptr<SelState>& sel = selection_state;
  if (sel->active_ && !sel->active_->CanResume())
    throw sr::RuntimeError("An active selection is already waiting");
  sel->active_ = s;
  sel->held_keys_.clear();
  sel->cancel_pressed_ = false;

  // Opening a menu transfers input ownership from message windows. Discard
  // stale presses so the input that invoked it cannot also decide a choice.
  sel->mouse_ = input_event_listener->mouse_pos;
  input_event_listener->ReleaseAll();
  if (script_input)
    script_input->ReleaseAll();

  auto& selection_stage =
      GetStage(IsExcall() && excall && excall->allocated ? 1 : 0);
  for (const auto& w : selection_stage.mwnd[kLayerFg]) {
    if (!w)
      continue;
    w->ClearMouseCapture();
  }
  selection_stage.selection[kLayerFg] = s;

  system->text().set_in_selection_mode(true);

  s->Start();
  UpdateSelection(0);
  sel->pending_->PruneDone();
}

void SiglusRuntime::FinishSelection(std::shared_ptr<Selection> s) {
  if (s->window) {
    s->window->SetSelectionSize(std::nullopt);
    if (s->window->action_on_pause())
      s->window->message_state()->MarkNovelClear(*s->window);
    else
      s->window->message_state()->MarkMessageClearReady(*s->window,
                                                        local_config);
    if (s->request.close_window)
      s->window->SetVisible(false);
  }

  system->text().set_in_selection_mode(false);
}

void SiglusRuntime::CancelSelection(std::shared_ptr<Selection> s) {
  if (selection_state->active_ != s)
    return;
  FinishSelection(s);

  auto& selection_stage =
      GetStage(IsExcall() && excall && excall->allocated ? 1 : 0);
  if (selection_stage.selection[kLayerFg] == s)
    selection_stage.selection[kLayerFg].reset();
  selection_state->active_.reset();
}

void SiglusRuntime::UpdateSelection(int ms) {
  if (!selection_state || updating_selection_)
    return;
  auto s = selection_state->active_;
  if (!s)
    return;

  // Opening callbacks can enter script code and reenter the runtime. Defer
  // nested updates until the outer hook has returned, including on failure.
  updating_selection_ = true;
  struct UpdateGuard {
    bool& updating;
    ~UpdateGuard() { updating = false; }
  } guard{updating_selection_};

  const auto displaced = [&] {
    return GetStage(IsExcall() && excall && excall->allocated ? 1 : 0)
                   .selection[kLayerFg] != s &&
           !s->result;
  };
  if (displaced()) {
    CancelSelection(s);
    return;
  }

  s->Advance(ms);
  if (s->phase() == Selection::Phase::Waiting && !s->open_hook_done) {
    if (s->request.capture)
      s->capture = RenderToSurface();

    // Mark before entering script code so a reentrant update cannot run the
    // capture/start hook twice.
    s->open_hook_done = true;

    if (s->request.capture && !s->request.start_scene.empty() &&
        s->request.start_entry >= 0) {
      if (!ActiveMachine().loader)
        throw sr::RuntimeError(
            "Selection start callback requires a scene loader");
      auto entry = ActiveMachine().loader->ResolveSceneEntry(
          s->request.start_scene, s->request.start_entry);
      ActiveMachine().vm->InvokeUntilBlocked(entry, {});
    }
    for (std::size_t i = 0; i < s->items.size(); ++i)
      if (s->request.options[i].type == 1) {
        s->focused = static_cast<int>(i);
        break;
      }
  }

  // A wipe can replace the stage's reference while its snapshot remains
  // visible. The displaced, undecided session must stop owning input.
  if (displaced()) {
    CancelSelection(s);
    return;
  }

  if (selection_state->active_ != s || !s->CanInput())
    return;

  // injected choice
  std::optional<int> choice;
  if (!debug_selection_choice_queue_.empty()) {
    choice = debug_selection_choice_queue_.front();
    debug_selection_choice_queue_.pop_front();
  } else if (const auto it = debug_selection_choices_.find(
                 {s->request.scene, s->request.kidoku});
             it != debug_selection_choices_.end()) {
    choice = it->second;
  }

  if (!choice)
    return;
  if (!selection_state->Choose(*choice))
    throw sr::RuntimeError(std::format(
        "Invalid registered selection choice {} for scene {}, kidoku {}",
        *choice, s->request.scene, s->request.kidoku));
  OnSelectionChosen(*s);
}

}  // namespace libsiglus
