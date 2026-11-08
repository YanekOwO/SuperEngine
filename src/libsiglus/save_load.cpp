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

#include "libsiglus/save_load.hpp"

#include "core/bgm_table.hpp"
#include "core/frame_action_manager.hpp"
#include "core/input.hpp"
#include "core/kidoku_table.hpp"
#include "core/message_back.hpp"
#include "core/object_internal/objdrawer.hpp"
#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/memory.hpp"
#include "libsiglus/execution_serialization.hpp"
#include "libsiglus/native_wait_serialization.hpp"
#include "libsiglus/runtime_checkpoint.hpp"
#include "libsiglus/save_manager.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "log/domain_logger.hpp"
#include "platforms/implementor.hpp"
#include "systems/event_system.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/sound_system.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"
#include "utilities/checkpoint_clock.hpp"
#include "vm/future.hpp"
#include "vm/promise.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/serialization/vector.hpp>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace libsiglus {
namespace sr = serilang;
namespace fs = std::filesystem;
namespace {
DomainLogger logger("SiglusSaveLoad");
constexpr int kSaveSound = 4;
constexpr int kLoadSound = 5;
constexpr int kPreviousSelectionSound = 7;

struct MachineState {
  // Gameplay comes first, followed by the active EX call stack.
  int caller = -1;
  std::uint32_t entry = 0, completion = 0;
  std::shared_ptr<TextWindow> window;
  std::vector<NativeWaitState> waits;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) {
    ar & caller & entry & completion & window;
  }
};
struct ExRequest {
  ScriptCall call;
  int caller = -1;
  std::uint32_t completion = 0;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) { ar & call & caller & completion; }
};
struct CaptureState {
  int owner = -1;
  std::uint32_t completion = 0;
  CaptureRequest request;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) { ar & owner & completion & request; }
};
struct RuntimeState {
  LocalMemory memory;
  PropertyStorage properties;
  LocalConfig config;
  std::string title, full_message;
  SiglusRuntime::CheckpointState resources;
  std::vector<MachineState> machines;
  std::vector<ExRequest> excalls;
  std::vector<CaptureState> captures;
  std::uint64_t game_fingerprint = 0;
  bool ex_allocated = false;
  std::vector<int> ex_flags;
  LocalConfig ex_config;
  std::vector<CheckpointId> backlog_ids;
  CheckpointId last_backlog_checkpoint = 0;
  std::uint64_t backlog_counter = 0;
  bool syscom_enabled = true, interface_hidden = false;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) {
    ar & memory & properties & config & title & full_message & resources &
        machines & excalls & captures & game_fingerprint & ex_allocated &
        ex_flags & ex_config & backlog_ids & last_backlog_checkpoint &
        backlog_counter & syscom_enabled & interface_hidden;
  }
};
struct GlobalState {
  GlobalMemory memory;
  std::int64_t play_time = 0;
  std::vector<bool> bgm;

 private:
  friend class boost::serialization::access;
  void serialize(auto& ar, unsigned int) { ar & memory & play_time & bgm; }
};

template <class T>
std::string Encode(const T& data,
                   std::string_view signature,
                   SerializationContext context = {}) {
  std::ostringstream stream(std::ios::binary);
  boost::archive::binary_oarchive archive(stream);
  SerializationContext::Init(archive, std::move(context));
  archive << std::string(signature) << data;
  return stream.str();
}
template <class T>
T Decode(const std::string& bytes,
         std::string_view signature,
         SerializationContext context = {}) {
  std::istringstream stream(bytes, std::ios::binary);
  boost::archive::binary_iarchive archive(stream);
  SerializationContext::Init(archive, std::move(context));
  std::string actual;
  archive >> actual;
  if (actual != signature)
    throw std::runtime_error("unsupported Siglus archive format");
  T result;
  archive >> result;
  return result;
}
std::string ReadFile(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    throw std::runtime_error("could not open " + path.string());
  return {std::istreambuf_iterator<char>(stream), {}};
}
void WriteFile(const fs::path& path, const std::string& bytes) {
  fs::create_directories(path.parent_path());
  auto temporary = path;
  temporary += ".tmp";
  try {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), bytes.size());
    stream.close();
    if (!stream || ReadFile(temporary) != bytes)
      throw std::runtime_error("could not verify " + path.string());
    fs::rename(temporary, path);
  } catch (...) {
    std::error_code error;
    fs::remove(temporary, error);
    throw;
  }
}
std::uint64_t FingerprintGame(const Archive* archive) {
  std::uint64_t hash = 1469598103934665603ull;
  auto bytes = [&](std::string_view data) {
    for (unsigned char byte : data) {
      hash ^= byte;
      hash *= 1099511628211ull;
    }
  };
  auto number = [&](std::uint64_t value) {
    bytes({reinterpret_cast<const char*>(&value), sizeof(value)});
  };
  auto text = [&](std::string_view value) {
    number(value.size());
    bytes(value);
  };
  if (!archive)
    return hash;
  number(archive->raw_scene_data_.size());
  for (const auto& scene : archive->raw_scene_data_)
    text(scene);
  number(archive->scene_names_.size());
  for (const auto& name : archive->scene_names_)
    text(name);
  number(archive->prop_.size());
  for (const auto& property : archive->prop_) {
    number(static_cast<unsigned int>(property.form));
    number(property.size);
    text(property.name);
  }
  number(archive->cmd_.size());
  for (const auto& command : archive->cmd_) {
    number(command.scene_id);
    number(command.offset);
    text(command.name);
  }
  return hash;
}

SaveCounts Counts(SiglusRuntime& runtime) {
  SaveCounts ret =
      runtime.gameexe ? SaveCounts::Parse(*runtime.gameexe) : SaveCounts{};
  return ret.Clamped();
}
}  // namespace

struct SiglusSaveLoad::PreparedLoad {
  SlotPayload payload;
  RuntimeState state;
  ExecutionReader execution;
  std::shared_ptr<CheckpointClock> clock;
  std::vector<std::unique_ptr<SiglusMachine>> machines;
  std::vector<SiglusRuntime::WipeHook> wipe_hooks;
};

SiglusSaveLoad::SiglusSaveLoad(SiglusRuntime& runtime)
    : runtime_(runtime), manager_(runtime.save_pth, Counts(runtime)) {
  game_fingerprint_ = FingerprintGame(runtime.archive.get());
  if (runtime.gameexe) {
    const auto config = CaptureConfig::Parse(*runtime.gameexe, {});
    if (config.save_thumbnail)
      manager_.SetThumbnailExtension(config.png_thumbnail ? "png" : "bmp");
  }
}
SiglusSaveLoad::~SiglusSaveLoad() = default;

SaveLocalExtra SiglusSaveLoad::Extra() const {
  return {runtime_.local_config.syscommenu.local_extra_switch,
          runtime_.local_config.syscommenu.local_extra_mode};
}
void SiglusSaveLoad::RequestCheckpoint(SiglusMachine& machine,
                                       sr::Fiber& fiber,
                                       bool explicit_result) {
  // Callback requests capture after the invocation and its host operation
  // finish. They must not retain a fiber that can die before capture.
  if (!checkpoint_) {
    if (runtime_.IsCallbackExecution(machine))
      checkpoint_ = CheckpointRequest{nullptr, nullptr,
                                      runtime_.gameplay_->lifetime, false};
    else
      checkpoint_ = CheckpointRequest{&machine, &fiber, machine.lifetime,
                                      explicit_result};
  }
  runtime_.YieldScripts(false);
}
void SiglusSaveLoad::BeginMessageBlock(SiglusMachine& machine,
                                       sr::Fiber& fiber) {
  if (runtime_.message_back)
    runtime_.message_back->GoNextMsg();
  ClearCheckpoint();
  if (!runtime_.local_config.dont_set_save_point)
    RequestCheckpoint(machine, fiber, false);
}
void SiglusSaveLoad::ClearCheckpoint() {
  checkpoint_.reset();
  manager_.ClearCheckpoint();
}
void SiglusSaveLoad::AppendMessage(std::string_view message) {
  runtime_.current_full_message.append(message);
  if (!runtime_.local_config.msg_back_off && runtime_.message_back)
    runtime_.message_back->AddMessage(std::string(message));
  AppendCheckpointMessage(message, !runtime_.local_config.msg_back_off);
}
void SiglusSaveLoad::AppendBacklogMessage(std::string_view message) {
  if (runtime_.message_back)
    runtime_.message_back->AddMessage(std::string(message));
  AppendCheckpointMessage(message);
}
void SiglusSaveLoad::AssociateBacklogCheckpoint() {
  AppendCheckpointMessage("");
}
void SiglusSaveLoad::AppendName(std::string_view name) {
  if (!runtime_.local_config.msg_back_off && runtime_.message_back)
    runtime_.message_back->AddNamae(std::string(name));
  AppendCheckpointMessage(name, !runtime_.local_config.msg_back_off);
}
void SiglusSaveLoad::AppendCheckpointMessage(std::string_view message,
                                             bool associate_backlog) {
  manager_.AppendMessage(message);
  if (!manager_.HasCheckpoint())
    return;
  const auto id = manager_.active().id;
  const int count =
      runtime_.gameexe
          ? std::clamp((*runtime_.gameexe)("MESSAGE_BACK_SAVE.CNT").ReadInt(0),
                       0, 10000)
          : 0;
  if (!count)
    return;
  if (id != last_backlog_checkpoint_) {
    last_backlog_checkpoint_ = id;
    const int interval =
        runtime_.gameexe
            ? std::max(
                  1,
                  (*runtime_.gameexe)("MESSAGE_BACK_SAVE.INTERVAL").ReadInt(1))
            : 1;
    if (backlog_counter_++ % interval == 0) {
      backlog_[id] = manager_.MakePayload(Extra());
      while (backlog_.size() > static_cast<std::size_t>(count))
        backlog_.erase(backlog_.begin());
    }
  }
  if (auto found = backlog_.find(id); found != backlog_.end()) {
    found->second.active = manager_.active();
    if (associate_backlog && runtime_.message_back)
      runtime_.message_back->SetCheckpointId(id);
    if (runtime_.system)
      runtime_.system->text().GetCurrentMwnd()->SetCheckpointId(id);
  }
}
void SiglusSaveLoad::Reset(bool preserve_backlog) {
  checkpoint_.reset();
  manager_.Reset();
  if (!preserve_backlog) {
    backlog_.clear();
    selected_backlog_ = 0;
    last_backlog_checkpoint_ = 0;
    backlog_counter_ = 0;
  }
  runtime_.scene_title.clear();
  runtime_.current_full_message.clear();
  if (!preserve_backlog && runtime_.message_back)
    runtime_.message_back->Reset();
}
bool SiglusSaveLoad::pending() const {
  return checkpoint_ || load_ || after_load_ || end_capture_ ||
         quit_deadline_ || fade_in_deadline_;
}

std::string SiglusSaveLoad::Capture(const CheckpointRequest& request) {
  RuntimeState state;
  state.game_fingerprint = game_fingerprint_;
  if (runtime_.memory)
    state.memory = runtime_.memory->GetLocalMemory();
  state.properties = runtime_.properties;
  state.config = runtime_.local_config;
  state.title = runtime_.scene_title;
  state.full_message = runtime_.current_full_message;
  for (const auto& [id, payload] : backlog_)
    state.backlog_ids.push_back(id);
  state.last_backlog_checkpoint = last_backlog_checkpoint_;
  state.backlog_counter = backlog_counter_;
  state.syscom_enabled = runtime_.syscom_enabled;
  state.interface_hidden = runtime_.interface_hidden_;
  if (runtime_.excall) {
    state.ex_allocated = runtime_.excall->allocated;
    state.ex_flags = runtime_.excall->flags;
    state.ex_config = runtime_.excall->config;
  }
  state.resources = runtime_.CaptureCheckpoint();
  ExecutionWriter execution;
  std::vector<SiglusMachine*> machines{runtime_.gameplay_.get()};
  for (const auto& machine : runtime_.excalls_)
    machines.push_back(machine.get());
  const auto index = [&](SiglusMachine* machine) -> int {
    if (!machine)
      return -1;
    const auto found = std::find(machines.begin(), machines.end(), machine);
    if (found == machines.end())
      throw std::runtime_error(
          "checkpoint refers to an unknown script machine");
    return found - machines.begin();
  };
  for (const auto& pending : runtime_.pending_excalls_)
    state.excalls.push_back({pending.request, index(pending.caller),
                             execution.PromiseId(pending.completion)});
  for (const auto& pending : runtime_.pending_captures_)
    state.captures.push_back({index(pending.owner),
                              execution.PromiseId(pending.promise),
                              pending.request});
  for (auto* machine : machines) {
    MachineState saved;
    saved.caller = index(machine->caller);
    saved.entry = execution.PromiseId(machine->entry_completion);
    saved.completion = execution.PromiseId(machine->caller_completion);
    saved.window = machine->saved_window;
    state.machines.push_back(std::move(saved));
  }
  std::ostringstream stream(std::ios::binary);
  boost::archive::binary_oarchive archive(stream);
  SerializationContext::Init(archive, MakeSerializationContext(runtime_));
  archive << std::string("RLVM-SIGLUS-CHECKPOINT") << state;
  for (auto* machine : machines)
    execution.WriteMachine(archive, *machine,
                           request.explicit_result && machine == request.machine
                               ? request.fiber
                               : nullptr);
  return stream.str();
}

std::unique_ptr<SiglusSaveLoad::PreparedLoad> SiglusSaveLoad::Prepare(
    SlotPayload payload) {
  SaveManager::Validate(payload);
  // Candidate bindings own their hooks, but cannot enter the live frame pump
  // until their machines and resources have been committed.
  struct HookGuard {
    SiglusRuntime& runtime;
    std::size_t count;
    ~HookGuard() { runtime.wipe_hooks.resize(count); }
  } hooks{runtime_, runtime_.wipe_hooks.size()};
  auto load = std::make_unique<PreparedLoad>();
  auto context = MakeSerializationContext(runtime_);
  load->clock = std::make_shared<CheckpointClock>(context.GetClock());
  context.clock = load->clock;
  std::istringstream stream(payload.blobs.at(payload.active.id).runtime_state,
                            std::ios::binary);
  boost::archive::binary_iarchive archive(stream);
  SerializationContext::Init(archive, std::move(context));
  std::string signature;
  archive >> signature;
  if (signature != "RLVM-SIGLUS-CHECKPOINT")
    throw std::runtime_error("unsupported Siglus archive format");
  archive >> load->state;
  load->payload = std::move(payload);
  auto& state = load->state;
  if (state.game_fingerprint != game_fingerprint_)
    throw std::runtime_error("saved game assets do not match this game");
  if (state.machines.empty())
    throw std::runtime_error("checkpoint has no gameplay machine");
  state.resources.Prepare(runtime_);
  for (std::size_t i = 0; i < state.machines.size(); ++i) {
    const auto& saved = state.machines[i];
    if (saved.caller < -1 ||
        saved.caller >= static_cast<int>(state.machines.size()) ||
        (i == 0 && saved.caller != -1) || (i != 0 && !saved.entry) ||
        (saved.caller == -1 && saved.completion != 0) ||
        (saved.caller >= 0 && saved.completion == 0))
      throw std::runtime_error("invalid checkpoint machine relationship");
    std::set<int> ancestors{static_cast<int>(i)};
    for (int caller = saved.caller; caller >= 0;
         caller = state.machines[caller].caller) {
      if (caller >= state.machines.size() || !ancestors.insert(caller).second)
        throw std::runtime_error("invalid saved caller chain");
    }
    auto machine = std::make_unique<SiglusMachine>(runtime_);
    machine->Initialize();
    load->machines.push_back(std::move(machine));
  }
  for (std::size_t i = 0; i < state.machines.size(); ++i) {
    auto& saved = state.machines[i];
    auto& machine = load->machines[i];
    saved.waits = load->execution.ReadMachine(archive, *machine);
    if (machine->entry_completion != load->execution.Promise(saved.entry) ||
        machine->caller_completion != load->execution.Promise(saved.completion))
      throw std::runtime_error(
          "saved machine promise links do not match execution");
    if (i != 0) {
      machine->entry_completion->AddWaker(
          [vm = machine->vm.get()](const auto& result) {
            if (result.has_value())
              (*vm->globals_)["%%entry_result"] = result.value();
          });
    }
    machine->saved_window = saved.window;
  }
  for (std::size_t i = 0; i < load->machines.size(); ++i) {
    auto& machine = *load->machines[i];
    const int caller = state.machines[i].caller;
    if (caller >= 0) {
      machine.caller = load->machines[caller].get();
      machine.caller_lifetime = machine.caller->lifetime;
    }
  }
  for (const auto& pending : state.excalls) {
    if (pending.caller < -1 ||
        pending.caller >= static_cast<int>(load->machines.size()))
      throw std::runtime_error("invalid saved EX caller");
    load->execution.Promise(pending.completion);
  }
  for (const auto& pending : state.captures) {
    if (pending.owner < 0 ||
        pending.owner >= static_cast<int>(load->machines.size()) ||
        !pending.completion)
      throw std::runtime_error("invalid saved capture owner or promise");
    load->execution.Promise(pending.completion);
  }
  for (std::size_t i = 0; i < load->machines.size(); ++i) {
    const auto& waits = state.machines[i].waits;
    PrepareNativeWaits(*load->machines[i], load->execution, waits);
  }
  load->execution.FinishRestore();
  if (stream.peek() != std::char_traits<char>::eof())
    throw std::runtime_error("trailing checkpoint data");
  load->wipe_hooks.assign(
      std::make_move_iterator(runtime_.wipe_hooks.begin() + hooks.count),
      std::make_move_iterator(runtime_.wipe_hooks.end()));
  return load;
}

void SiglusSaveLoad::Commit(PreparedLoad& load) {
  runtime_.pending_excalls_.clear();
  runtime_.ResetCaptures();
  runtime_.gameplay_->Cancel();
  for (auto& machine : runtime_.excalls_)
    machine->Cancel();
  for (auto& machine : runtime_.callbacks_)
    if (machine)
      machine->Cancel();
  runtime_.excalls_.clear();
  runtime_.callbacks_[0].reset();
  runtime_.callbacks_[1].reset();
  std::erase_if(runtime_.wipe_hooks,
                [](const auto& hook) { return hook.lifetime.expired(); });
  runtime_.counters_.clear();
  runtime_.restart_.reset();
  runtime_.resetting_ = false;
  if (runtime_.memory)
    runtime_.memory->PartialReset(std::move(load.state.memory));
  runtime_.properties = std::move(load.state.properties);
  runtime_.local_config = std::move(load.state.config);
  runtime_.local_config.syscommenu.local_extra_switch =
      load.payload.extra.switches;
  runtime_.local_config.syscommenu.local_extra_mode = load.payload.extra.modes;
  runtime_.scene_title = std::move(load.state.title);
  runtime_.current_full_message = std::move(load.state.full_message);
  runtime_.syscom_enabled = load.state.syscom_enabled;
  runtime_.interface_hidden_ = load.state.interface_hidden;
  runtime_.InitializeExcall();
  runtime_.excall->allocated = load.state.ex_allocated;
  runtime_.excall->releasing = false;
  runtime_.excall->lifetime =
      load.state.ex_allocated ? std::make_shared<char>() : nullptr;
  runtime_.excall->flags = std::move(load.state.ex_flags);
  runtime_.excall->config = std::move(load.state.ex_config);
  load.state.resources.Commit(runtime_);
  for (std::size_t i = 0; i < load.machines.size(); ++i) {
    auto& machine = load.machines[i];
    if (i == 0)
      runtime_.gameplay_ = std::move(machine);
    else
      runtime_.excalls_.push_back(std::move(machine));
  }
  runtime_.excall_depth_ = runtime_.excalls_.size();
  std::vector<SiglusMachine*> machines{runtime_.gameplay_.get()};
  for (auto& machine : runtime_.excalls_)
    machines.push_back(machine.get());
  for (const auto& pending : load.state.excalls) {
    auto* caller = pending.caller < 0 ? nullptr : machines.at(pending.caller);
    runtime_.pending_excalls_.push_back(
        {pending.call, caller,
         caller ? std::weak_ptr(caller->lifetime) : std::weak_ptr<char>{},
         load.execution.Promise(pending.completion)});
  }
  for (const auto& pending : load.state.captures) {
    auto* owner = machines.at(pending.owner);
    auto promise = load.execution.Promise(pending.completion);
    owner->vm->TrackPendingPromise(promise);
    runtime_.pending_captures_.push_back(
        {pending.request, owner, owner->lifetime, std::move(promise)});
  }
  manager_.RestorePayload(load.payload);
  manager_.ResetMessages(runtime_.current_full_message);
  const std::set<CheckpointId> backlog_ids(load.state.backlog_ids.begin(),
                                           load.state.backlog_ids.end());
  std::erase_if(backlog_, [&](const auto& entry) {
    return !backlog_ids.contains(entry.first);
  });
  last_backlog_checkpoint_ = load.state.last_backlog_checkpoint;
  backlog_counter_ = load.state.backlog_counter;
  selected_backlog_ = 0;
  if (runtime_.input_event_listener)
    runtime_.input_event_listener->ReleaseAll();
  if (runtime_.script_input)
    runtime_.script_input->ReleaseAll();
  if (runtime_.system)
    runtime_.system->event().DiscardDeferredInput();
  runtime_.wipe_hooks.insert(runtime_.wipe_hooks.end(),
                             std::make_move_iterator(load.wipe_hooks.begin()),
                             std::make_move_iterator(load.wipe_hooks.end()));
  for (auto* machine : machines)
    ActivateNativeWaits(*machine);
  if (runtime_.window_title) {
    runtime_.window_title->subtitle_ = runtime_.scene_title;
    runtime_.window_title->Apply();
  }
  runtime_.Activate(runtime_.ActiveMachine());
  if (runtime_.system)
    runtime_.system->ApplyConfiguration();
  activate_clock_ = [clock = load.clock] { clock->Activate(); };
  after_load_ = true;
}

bool SiglusSaveLoad::Process() {
  if (!runtime_.drive_stack_.empty())
    return false;
  if (quit_deadline_) {
    if (std::chrono::steady_clock::now() >= *quit_deadline_) {
      quit_deadline_.reset();
      if (runtime_.system)
        runtime_.system->RequestQuit();
      runtime_.GameplayMachine().vm->RequestStop();
    }
    return true;
  }
  if (end_capture_ && !end_capture_->HasResult())
    return true;
  if (end_capture_ && end_capture_->HasResult()) {
    if (!end_capture_->result->has_value())
      throw std::runtime_error(end_capture_->result->error());
    end_capture_.reset();
    if (!FlushGlobal() ||
        !manager_.SaveSlot(*manager_.EndSlot(), Extra(),
                           runtime_.captures
                               ? runtime_.captures->EncodeSaveThumbnail()
                               : std::nullopt))
      throw std::runtime_error("end-save failed: " + manager_.last_error());
    if (end_sound_ && runtime_.system)
      runtime_.system->sound().PlaySe(kSaveSound);
  }
  if (load_) {
    if (std::chrono::steady_clock::now() < load_deadline_)
      return true;
    auto load = std::move(load_);
    Commit(*load);
    transition_start_ = std::chrono::steady_clock::now();
    const int duration =
        runtime_.gameexe && runtime_.system &&
                !runtime_.system->ShouldFastForward() &&
                !runtime_.global_config.system.disable_wipe_anime
            ? std::max(0,
                       (*runtime_.gameexe)("LOAD.WIPE").IntAt(1).value_or(1000))
            : 0;
    fade_in_deadline_ = transition_start_ + std::chrono::milliseconds(duration);
    return true;
  }
  if (fade_in_deadline_) {
    if (std::chrono::steady_clock::now() < *fade_in_deadline_)
      return true;
    fade_in_deadline_.reset();
  }
  if (activate_clock_)
    std::exchange(activate_clock_, {})();
  const auto capture_pending = [&] {
    if (!checkpoint_ || runtime_.HasPendingCallbacks())
      return;
    const auto request = *std::exchange(checkpoint_, std::nullopt);
    if (!request.lifetime.expired()) {
      manager_.Capture(Capture(request), runtime_.scene_title);
      manager_.ResetMessages(runtime_.current_full_message);
    }
  };
  if (after_load_) {
    after_load_ = false;
    if (runtime_.frame_action_manager)
      runtime_.frame_action_manager->Dispatch();
    if (runtime_.gameexe) {
      const auto target = (*runtime_.gameexe)("LOAD_AFTER_CALL");
      const auto name = target.StrAt(0).value_or("");
      if (!name.empty()) {
        auto& slot = runtime_.callbacks_[0];
        if (!slot) {
          slot = std::make_unique<SiglusMachine>(runtime_, 0);
          slot->Initialize();
        }
        auto& machine = *slot;
        auto entry = machine.loader->ResolveSceneEntry(
            name, target.IntAt(1).value_or(0));
        runtime_.DriveMachine(machine, [&] {
          machine.vm->InvokeUntilBlocked(
              entry, {}, binding::MakeCallMemory(*runtime_.memory));
        });
      }
    }
  }
  capture_pending();
  // Keep gameplay at its savepoint while callbacks finish. EX calls made by
  // callbacks must still run so their awaiting callback can return.
  return checkpoint_ && !runtime_.IsCallbackExecution(runtime_.ActiveMachine());
}

bool SiglusSaveLoad::Confirm(bool save, bool quick, bool selection) const {
  if (!runtime_.platform_implementor)
    return true;
  std::string prompt = selection ? "Return to the previous selection?"
                       : save    ? "Overwrite this save?"
                                 : "Load this save?";
  if (runtime_.gameexe) {
    const std::string key =
        selection ? "WARNINGINFO.RETURNSEL_WARNING_STR"
                  : std::string(save ? "SAVEINFO." : "LOADINFO.") +
                        (quick ? "QUICK_WARNING_STR" : "WARNING_STR");
    prompt = (*runtime_.gameexe)(key).Str().value_or(prompt);
  }
  return runtime_.platform_implementor->AskUserPrompt(prompt, "", "Yes", "No");
}
bool SiglusSaveLoad::Save(int slot, bool warning, bool sound, Source source) {
  if (!manager_.HasCheckpoint())
    return false;
  const auto translated = source == Source::Quick ? manager_.QuickSlot(slot)
                          : source == Source::End ? manager_.EndSlot()
                                                  : manager_.NormalSlot(slot);
  if (!translated)
    return false;
  slot = *translated;
  if (warning && runtime_.global_config.system.alert_saveload &&
      fs::exists(manager_.SlotPath(slot)) &&
      !Confirm(true, source == Source::Quick))
    return false;
  if (source == Source::End && runtime_.captures && runtime_.gameexe &&
      (*runtime_.gameexe)("SAVE_THUMB.USE").ReadInt(0) != 0) {
    auto future = runtime_.RequestCapture(
        runtime_.ActiveMachine(), {.kind = CaptureKind::Save, .priority = 2});
    end_capture_ = sr::get_promise(future);
    end_sound_ = sound;
    runtime_.YieldScripts();
    runtime_.ActiveMachine().vm->RequestHostYield();
    return true;
  }
  try {
    const auto thumbnail = runtime_.captures
                               ? runtime_.captures->EncodeSaveThumbnail()
                               : std::nullopt;
    if (!FlushGlobal() || !manager_.SaveSlot(slot, Extra(), thumbnail)) {
      logger(Severity::Error)
          << "Could not save slot " << slot << ": " << manager_.last_error();
      return false;
    }
    if (sound && runtime_.system)
      runtime_.system->sound().PlaySe(kSaveSound);
    return true;
  } catch (const std::exception& e) {
    logger(Severity::Error)
        << "Could not save slot " << slot << ": " << e.what();
    return false;
  }
}
bool SiglusSaveLoad::Load(SiglusMachine& machine,
                          int slot,
                          bool warning,
                          bool sound,
                          bool fade,
                          Source source) {
  if (load_)
    return false;
  std::optional<SlotPayload> payload;
  try {
    switch (source) {
      case Source::Disk:
        payload = manager_.LoadSlot(slot);
        break;
      case Source::Quick:
        if (auto translated = manager_.QuickSlot(slot))
          payload = manager_.LoadSlot(*translated);
        break;
      case Source::End:
        if (auto translated = manager_.EndSlot())
          payload = manager_.LoadSlot(*translated);
        break;
      case Source::Inner:
        if (manager_.HasInner(slot))
          payload = manager_.MakePayloadFor(manager_.GetInner(slot), Extra());
        break;
      case Source::Selection:
        if (auto ref = manager_.LatestSelection())
          payload = manager_.MakePayloadFor(*ref, Extra());
        break;
      case Source::Backlog:
        if (auto it = backlog_.find(selected_backlog_); it != backlog_.end())
          payload = it->second;
        break;
    }
    if (!payload)
      return false;
    if (warning &&
        (source == Source::Selection ||
         runtime_.global_config.system.alert_saveload) &&
        !Confirm(false, source == Source::Quick, source == Source::Selection))
      return false;
    auto prepared = Prepare(std::move(*payload));
    int duration = 0;
    if (fade && runtime_.gameexe && runtime_.system &&
        !runtime_.system->ShouldFastForward() &&
        !runtime_.global_config.system.disable_wipe_anime)
      duration =
          std::max(0, (*runtime_.gameexe)("LOAD.WIPE").IntAt(1).value_or(0));
    transition_start_ = std::chrono::steady_clock::now();
    load_deadline_ = transition_start_ + std::chrono::milliseconds(duration);
    load_ = std::move(prepared);
    checkpoint_.reset();
    if (sound && runtime_.system)
      runtime_.system->sound().PlaySe(
          source == Source::Selection ? kPreviousSelectionSound : kLoadSound);
    if (duration && runtime_.system)
      runtime_.system->sound().BgmFadeOut(duration);
    runtime_.YieldScripts();
    machine.vm->RequestHostYield();
    return true;
  } catch (const std::exception& e) {
    logger(Severity::Error) << "Could not load checkpoint: " << e.what();
    return false;
  }
}
void SiglusSaveLoad::OpenMenu(bool save) {
  if (save && !manager_.HasCheckpoint())
    return;
  const auto key = save ? "SAVE_SCENE" : "LOAD_SCENE";
  if (!runtime_.gameexe ||
      (*runtime_.gameexe)(key).StrAt(0).value_or("").empty())
    throw sr::RuntimeError(
        std::string(key) +
        " is not configured; built-in save/load menus are unavailable");
  const auto target = (*runtime_.gameexe)(key);
  runtime_.OpenExcall(
      ScriptCall{.target = SceneTarget{target.StrAt(0).value_or(""),
                                       target.IntAt(1).value_or(0)}});
}
void SiglusSaveLoad::SelectBacklog(CheckpointId id) { selected_backlog_ = id; }

void SiglusSaveLoad::RequestQuit(bool fade) {
  int duration = 0;
  if (fade && runtime_.gameexe && runtime_.system &&
      !runtime_.system->ShouldFastForward() &&
      !runtime_.global_config.system.disable_wipe_anime)
    duration =
        std::max(0, (*runtime_.gameexe)("LOAD.WIPE").IntAt(1).value_or(1000));
  transition_start_ = std::chrono::steady_clock::now();
  quit_deadline_ = transition_start_ + std::chrono::milliseconds(duration);
  runtime_.YieldScripts();
}

bool SiglusSaveLoad::PauseGameTime() const {
  return load_ || fade_in_deadline_ || quit_deadline_ || end_capture_;
}

void SiglusSaveLoad::RenderOverlay() {
  if (!runtime_.presenter)
    return;
  const auto now = std::chrono::steady_clock::now();
  std::optional<std::chrono::steady_clock::time_point> deadline;
  bool incoming = false;
  if (load_)
    deadline = load_deadline_;
  else if (quit_deadline_)
    deadline = quit_deadline_;
  else if (fade_in_deadline_) {
    deadline = fade_in_deadline_;
    incoming = true;
  }
  if (!deadline)
    return;
  const auto total =
      std::chrono::duration<double>(*deadline - transition_start_).count();
  const auto elapsed =
      std::chrono::duration<double>(now - transition_start_).count();
  double progress = total > 0 ? std::clamp(elapsed / total, 0.0, 1.0) : 1.0;
  if (incoming)
    progress = 1.0 - progress;
  if (!fade_surface_) {
    fade_surface_ = std::make_shared<SDLSurface>(Size(1, 1));
    fade_surface_->Fill(RGBAColour(0, 0, 0, 255));
  }
  fade_surface_->RenderToScreen(fade_surface_->GetRect(),
                                runtime_.presenter->screen_rect(),
                                static_cast<int>(progress * 255));
}

void SiglusSaveLoad::OpenBacklogLoad(CheckpointId id) {
  if (!backlog_.contains(id))
    return;
  selected_backlog_ = id;
  if (runtime_.gameexe) {
    const auto target = (*runtime_.gameexe)("MSGBK.LOAD_CALL");
    const auto scene = target.StrAt(0).value_or("");
    if (!scene.empty()) {
      const auto command = target.StrAt(1).value_or("");
      if (command.empty())
        throw sr::RuntimeError("MSGBK.LOAD_CALL requires a command name");
      runtime_.OpenExcall(runtime_.ActiveMachine()
                              .loader->ResolveSceneUserCommand(scene, command)
                              .target);
      return;
    }
  }
  Load(runtime_.ActiveMachine(), 0, true, true, true, Source::Backlog);
}

void SiglusSaveLoad::LoadGlobal() {
  const auto global_path = runtime_.save_pth / "global.sav";
  const auto config_path = runtime_.save_pth / "config.sav";
  const auto read_path = runtime_.save_pth / "read.sav";
  std::optional<GlobalState> global;
  std::optional<GlobalConfig> config;
  std::optional<KidokuTable> kidoku;
  if (fs::exists(global_path))
    global = Decode<GlobalState>(ReadFile(global_path), "RLVM-SIGLUS-GLOBAL");
  if (fs::exists(config_path))
    config = Decode<GlobalConfig>(ReadFile(config_path), "RLVM-SIGLUS-CONFIG");
  if (fs::exists(read_path))
    kidoku = Decode<KidokuTable>(ReadFile(read_path), "RLVM-SIGLUS-READ");
  if (global && runtime_.bgm_table &&
      global->bgm.size() != runtime_.bgm_table->Count())
    throw std::runtime_error("saved BGM table does not match the game");
  if (global) {
    runtime_.memory->PartialReset(std::move(global->memory));
    runtime_.total_play_time = global->play_time;
    if (runtime_.bgm_table)
      runtime_.bgm_table->RestoreListenState(std::move(global->bgm));
  }
  if (config)
    runtime_.global_config = std::move(*config);
  if (kidoku && runtime_.kidoku_table)
    *runtime_.kidoku_table = std::move(*kidoku);
  if (runtime_.system)
    runtime_.system->ApplyConfiguration();
}
bool SiglusSaveLoad::FlushGlobal() {
  if (runtime_.save_pth.empty())
    return true;
  try {
    GlobalState global;
    if (runtime_.memory)
      global.memory = runtime_.memory->GetGlobalMemory();
    global.play_time = runtime_.total_play_time;
    if (runtime_.bgm_table)
      global.bgm = runtime_.bgm_table->ListenState();
    WriteFile(runtime_.save_pth / "global.sav",
              Encode(global, "RLVM-SIGLUS-GLOBAL"));
    WriteFile(runtime_.save_pth / "config.sav",
              Encode(runtime_.global_config, "RLVM-SIGLUS-CONFIG"));
    if (runtime_.kidoku_table)
      WriteFile(runtime_.save_pth / "read.sav",
                Encode(*runtime_.kidoku_table, "RLVM-SIGLUS-READ"));
    return true;
  } catch (const std::exception& e) {
    logger(Severity::Error) << "Could not persist global state: " << e.what();
    return false;
  }
}
void SiglusSaveLoad::Shutdown() {
  if (manager_.counts().end && manager_.HasCheckpoint()) {
    if (runtime_.captures) {
      auto request = runtime_.captures->PrepareRequest(
          {.kind = CaptureKind::Save, .priority = 2});
      if (request)
        runtime_.captures->Capture(*request);
    }
    if (!manager_.SaveSlot(*manager_.EndSlot(), Extra(),
                           runtime_.captures
                               ? runtime_.captures->EncodeSaveThumbnail()
                               : std::nullopt))
      throw std::runtime_error("Could not write end save: " +
                               manager_.last_error());
  }
  if (!FlushGlobal())
    throw std::runtime_error("Could not persist Siglus global state");
}
}  // namespace libsiglus
