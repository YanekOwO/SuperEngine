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

#include "libsiglus/siglus_machine.hpp"
#include "libsiglus/bindings/flow.hpp"

#include "core/input.hpp"
#include "core/kidoku_table.hpp"
#include "libsiglus/bindings/frame_action.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "systems/sound_system.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"
#include "vm/exception.hpp"
#include "vm/future.hpp"
#include "vm/promise.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <memory>
#include <stdexcept>
#include <thread>

namespace libsiglus {
namespace sr = serilang;

SiglusMachine::~SiglusMachine() {
  // Native VM instances and pending coroutine frames keep non-owning pointers
  // to runtime services. Finalize them before member destruction tears those
  // services down.
  if (auto s = selection_state) {
    s->pending_.reset();
    if (s->active_)
      CancelSelection(s->active_);
    s->prepared_.reset();
  }

  ResetCaptures();
  vm.reset();
}

sr::Value SiglusMachine::RequestCapture(CaptureRequest request) {
  if (!vm || !captures)
    throw std::logic_error(
        "SiglusMachine::RequestCapture requires a VM and capture manager");

  auto* future = vm->gc_->Allocate<sr::Future>();
  auto prepared = captures->PrepareRequest(std::move(request));
  if (!prepared) {
    future->promise->Resolve(sr::nil);
    return future;
  }
  vm->TrackPendingPromise(future->promise);
  pending_captures_.push_back({std::move(*prepared), future->promise});
  return future;
}

void SiglusMachine::ProcessCaptureFrame() {
  if (pending_captures_.empty())
    return;

  auto job = std::move(pending_captures_.front());
  pending_captures_.pop_front();
  if (job.promise->HasResult())
    return;

  try {
    const auto result = captures->Capture(job.request);
    job.promise->Resolve(result ? sr::Value(*result) : sr::nil);
  } catch (const std::exception& e) {
    job.promise->Reject(e.what());
  }
}

void SiglusMachine::ResetCaptures() {
  auto pending = std::move(pending_captures_);
  pending_captures_.clear();
  if (captures)
    captures->Reset();
  for (auto& job : pending)
    if (!job.promise->HasResult())
      job.promise->Reject("Capture cancelled by runtime reset");
}

void SiglusMachine::Start(std::optional<int> start_scene) {
  if (!vm || !loader || !gameexe)
    throw std::logic_error(
        "SiglusMachine::Start requires a VM, loader and Gameexe");

  sr::Function* script = nullptr;
  if (start_scene.has_value()) {
    // An explicit --scene ID starts at the scene's main script.
    sr::Module* mod = loader->Load(*start_scene);
    const auto it = mod->globals->find("%%script");
    if (it != mod->globals->end())
      script = it->second.Get_if<sr::Function>();
    if (!script)
      throw sr::RuntimeError(
          std::format("Scene {}: %%script not found", *start_scene));
  } else {
    auto start = (*gameexe)("START_SCENE");
    script = binding::ResolveSceneEntry(
        *loader, start.StrAt(0).value_or("_start"), start.IntAt(1).value_or(0));
  }
  vm->ScheduleExecute(script);
}

void SiglusMachine::Update(std::chrono::milliseconds real_delta,
                           std::chrono::milliseconds game_delta) {
  using ms = std::chrono::milliseconds;
  real_delta = std::max(ms::zero(), real_delta);
  game_delta = std::max(ms::zero(), game_delta);
  if (selection_state && !local_config.time_stop)
    UpdateSelection(game_delta.count());

  const bool time_stopped = local_config.time_stop;
  if (!time_stopped && !local_config.counter_time_stop && counter_list)
    counter_list->Advance(game_delta, real_delta);

  // Advance before callbacks can create new timers. Such timers must not
  // receive the elapsed time from before their creation.
  std::erase_if(counters_,
                [real_delta, game_delta, time_stopped](const auto& ptr) {
                  if (auto counter = ptr.lock()) {
                    if (!time_stopped)
                      counter->Advance(game_delta, real_delta);
                    return false;
                  }
                  return true;
                });

  bool should_refresh = (screen_quakes && screen_quakes->IsActive()) ||
                        (stage && stage->HasActiveQuakes());
  if (!time_stopped && screen_quakes)
    screen_quakes->Advance(game_delta.count());

  if (!time_stopped && !local_config.stage_time_stop && stage)
    stage->AdvanceQuakes(game_delta.count());
  should_refresh |= (screen_quakes && screen_quakes->IsActive()) ||
                    (stage && stage->HasActiveQuakes());

  if (should_refresh)
    system->graphics().ForceRefresh();

  if (frame_action_manager)
    frame_action_manager->AdvanceAndDispatch(real_delta, game_delta);
  if (update_wipe_after_frame_actions)
    update_wipe_after_frame_actions();
}

void SiglusMachine::Run() {
  if (!vm || !system)
    throw std::logic_error("SiglusMachine::Run requires a VM and system");

  using FrameClock = std::chrono::steady_clock;
  constexpr auto frame_period =
      std::chrono::duration_cast<FrameClock::duration>(
          std::chrono::seconds(1)) /
      60;
  constexpr auto minimum_vm_budget =
      std::chrono::duration_cast<FrameClock::duration>(
          std::chrono::milliseconds(1));
  const auto frame_clock = system->event().GetClock();
  auto last_frame_tick = frame_clock->GetTicks();

  while (!vm->IsStopRequested() && !system->IsQuitRequested() &&
         vm->HasPendingWork()) {
    const auto next_frame = FrameClock::now() + frame_period;
    const bool capture_frame = HasPendingCaptures();
    input_event_listener->ResetState();
    {
      auto& graphics = system->graphics();
      struct RestorePresentation {
        GraphicsSystem& graphics;
        bool previous;
        ~RestorePresentation() {
          graphics.set_is_responsible_for_update(previous);
        }
      } restore{graphics, graphics.is_responsible_for_update()};
      if (capture_frame && !CaptureDisplayEnabled())
        graphics.set_is_responsible_for_update(false);
      system->Run([this, &last_frame_tick, &frame_clock] {
        interaction_manager->Update();

        const auto now = frame_clock->GetTicks();
        const auto real_delta =
            std::max(now - last_frame_tick, Clock::duration_t::zero());
        last_frame_tick = now;

        const bool fast_forward = system->ShouldFastForward();
        const auto fast_forward_delta =
            real_delta > Clock::duration_t::max() / 32
                ? Clock::duration_t::max()
                : real_delta * 32;
        const auto game_delta = fast_forward ? fast_forward_delta : real_delta;

        Update(real_delta, game_delta);
      });
    }
    if (vm->IsStopRequested() || system->IsQuitRequested())
      break;
    if (capture_frame)
      ProcessCaptureFrame();

    vm->Run(std::max(next_frame - FrameClock::now(), minimum_vm_budget));
    if (vm->IsStopRequested() || system->IsQuitRequested() ||
        !vm->HasPendingWork())
      break;

    std::this_thread::sleep_until(next_frame);
  }

  if (system->IsQuitRequested())
    vm->RequestStop();
}

std::shared_ptr<Counter> SiglusMachine::CreateCounter() {
  auto ret = std::make_shared<Counter>();
  counters_.emplace_back(ret);
  return ret;
}

// ------------------------------------------------------------------------------
void SiglusMachine::RegisterDebugSelectionChoice(int scene,
                                                 int kidoku,
                                                 int choice) {
  if (choice < -1)
    throw sr::RuntimeError(
        "Selection choice must be -1 or a nonnegative index");
  debug_selection_choices_.insert_or_assign({scene, kidoku}, choice);
}

void SiglusMachine::QueueDebugSelectionChoice(int choice) {
  if (choice < -1)
    throw sr::RuntimeError(
        "Selection choice must be -1 or a nonnegative index");
  debug_selection_choice_queue_.push_back(choice);
}

void SiglusMachine::OnSelectionChosen(const Selection& s) {
  const int index = s.result.value();
  if (system) {
    const int number = index < 0 ? 2 : 1;
    if (system->sound().HasSe(number))
      system->sound().PlaySe(number);
  }
  if (kidoku_table && s.request.scene >= 0 && s.request.kidoku >= 0)
    kidoku_table->RecordKidoku(s.request.scene, s.request.kidoku);
}

void SiglusMachine::StartSelection(std::shared_ptr<Selection> s) {
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

  for (const auto& w : stage->mwnd[kLayerFg]) {
    if (!w)
      continue;
    w->ClearMouseCapture();
  }
  stage->selection[kLayerFg] = s;

  system->text().set_in_selection_mode(true);

  s->Start();
  UpdateSelection(0);
  sel->pending_->PruneDone();
}

void SiglusMachine::FinishSelection(std::shared_ptr<Selection> s) {
  if (s->window) {
    TextSystem& text = system->text();
    s->window->SetSelectionSize(std::nullopt);
    if (s->window->action_on_pause())
      s->window->message_state()->MarkNovelClear(
          text.GetPageOnWindow(s->window));
    else
      s->window->message_state()->MarkMessageClearReady(
          text.GetPageOnWindow(s->window));
    if (s->request.close_window)
      s->window->SetVisible(false);
  }

  system->text().set_in_selection_mode(false);
  system->graphics().ForceRefresh();
}

void SiglusMachine::CancelSelection(std::shared_ptr<Selection> s) {
  if (selection_state->active_ != s)
    return;
  FinishSelection(s);

  if (stage->selection[kLayerFg] == s)
    stage->selection[kLayerFg].reset();
  selection_state->active_.reset();
}

void SiglusMachine::UpdateSelection(int ms) {
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
    return stage && stage->selection[kLayerFg] != s && !s->result;
  };
  if (displaced()) {
    CancelSelection(s);
    return;
  }

  const bool was_visible = s->Visible();
  s->Advance(ms);
  if (s->phase() == Selection::Phase::Waiting && !s->open_hook_done) {
    if (s->request.capture)
      s->capture = system->graphics().RenderToSurface();

    // Mark before entering script code so a reentrant update cannot run the
    // capture/start hook twice.
    s->open_hook_done = true;

    if (s->request.capture && !s->request.start_scene.empty() &&
        s->request.start_entry >= 0) {
      if (!loader)
        throw sr::RuntimeError(
            "Selection start callback requires a scene loader");
      auto* entry = binding::ResolveSceneEntry(*loader, s->request.start_scene,
                                               s->request.start_entry);
      vm->InvokeUntilBlocked(*entry, {});
    }
    for (std::size_t i = 0; i < s->items.size(); ++i)
      if (s->request.options[i].type == 1) {
        s->focused = static_cast<int>(i);
        break;
      }
  }

  if (was_visible || s->Visible())
    system->graphics().ForceRefresh();

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
  if (system)
    system->graphics().ForceRefresh();
}

}  // namespace libsiglus
