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

#include "libsiglus/native_operation_serialization.hpp"

#include "core/avdec/ffmpeg.hpp"
#include "libsiglus/execution_serialization.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "long_operations/siglus_movie.hpp"
#include "long_operations/siglus_mwnd_wait.hpp"
#include "long_operations/siglus_textout.hpp"
#include "long_operations/siglus_wipe.hpp"
#include "long_operations/wait_for_selection.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"
#include "vm/promise.hpp"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace lop {
namespace {
class CompletedOperation final : public LongOperation {
 public:
  explicit CompletedOperation(int result) : result_(result) {}
  Task<int> Run(WaitContext&) override { co_return result_; }

 private:
  int result_;
};
}  // namespace

libsiglus::TextoutOperationState SiglusTextoutOperation::CaptureCheckpoint(
    libsiglus::ExecutionWriter&) const {
  return {.window = window_, .text = text_, .phase = static_cast<int>(phase_)};
}
std::unique_ptr<LongOperation> SiglusTextoutOperation::RestoreCheckpoint(
    libsiglus::SiglusMachine& machine,
    libsiglus::ExecutionReader&,
    const libsiglus::TextoutOperationState& state) {
  if (state.phase < 0 || state.phase > 3)
    throw std::runtime_error("Invalid saved text operation phase");
  auto value = std::make_unique<SiglusTextoutOperation>(
      machine.host().system.get(), state.window, state.text);
  value->phase_ = static_cast<Phase>(state.phase);
  return value;
}

libsiglus::MessageWaitOperationState SiglusMwndWaitOperation::CaptureCheckpoint(
    libsiglus::ExecutionWriter& execution) const {
  return {.window = window_,
          .pace_frame = execution.PromiseId(pace_frame_),
          .phase = static_cast<int>(phase_),
          .after_wait = static_cast<int>(after_wait_),
          .result = result_};
}
std::unique_ptr<LongOperation> SiglusMwndWaitOperation::RestoreCheckpoint(
    libsiglus::SiglusMachine& machine,
    libsiglus::ExecutionReader& execution,
    const libsiglus::MessageWaitOperationState& state) {
  if (state.phase < 0 || state.phase > 6 || state.after_wait < 0 ||
      state.after_wait > 3)
    throw std::runtime_error("Invalid saved message operation phase");
  if (!state.window)
    throw std::runtime_error("Saved message wait has no window");
  auto value = std::make_unique<SiglusMwndWaitOperation>(
      machine.host().system.get(), *machine.frame_requests, state.window,
      state.window->message_state(), static_cast<AfterWait>(state.after_wait));
  value->phase_ = static_cast<Phase>(state.phase);
  if (value->phase_ == Phase::Pause)
    value->phase_ = Phase::Advance;
  value->result_ = state.result;
  value->pace_frame_ = execution.Promise(state.pace_frame);
  return value;
}

libsiglus::SelectionWaitOperationState WaitForSelection::CaptureCheckpoint(
    libsiglus::ExecutionWriter&) const {
  return {sel_};
}
std::unique_ptr<LongOperation> WaitForSelection::RestoreCheckpoint(
    libsiglus::SiglusMachine& machine,
    libsiglus::ExecutionReader&,
    const libsiglus::SelectionWaitOperationState& state) {
  if (!state.selection || !machine.selection_state)
    throw std::runtime_error("Saved selection wait has no session");
  return std::make_unique<WaitForSelection>(
      machine.selection_state, state.selection,
      [&machine](std::shared_ptr<Selection> selection) {
        machine.host().FinishSelection(std::move(selection));
      },
      [&machine](std::shared_ptr<Selection> selection) {
        machine.host().CancelSelection(std::move(selection));
      });
}

libsiglus::WipeOperationState SiglusWipeOperation::CaptureCheckpoint(
    libsiglus::ExecutionWriter& execution) const {
  libsiglus::WipeOperationState state;
  state.duration = duration_;
  state.interpolation = interpolation_;
  state.owner =
      stage_ && system_ && stage_ == system_->text().excall_stage_ ? 1 : 0;
  state.wait_flag = wait_flag_;
  state.primed = primed_;
  if (auto active = active_.lock()) {
    state.promise = execution.PromiseId(active->promise);
    state.key_mode = active->key_wait_mode;
    state.cancelled = active->cancelled || active->promise->HasResult();
    state.boundaries = active->completion.regular_boundaries;
    state.finish_boundary =
        active->completion.finish_after_boundary.value_or(-1);
    state.result = active->completion.result;
  }
  return state;
}
std::unique_ptr<LongOperation> SiglusWipeOperation::RestoreCheckpoint(
    libsiglus::SiglusMachine& machine,
    libsiglus::ExecutionReader& execution,
    const libsiglus::WipeOperationState& state) {
  if (state.cancelled)
    return std::make_unique<CompletedOperation>(state.result);
  if (state.owner < 0 || state.owner > 1 || !state.promise ||
      state.boundaries < 0 || state.duration < 0 ||
      state.interpolation < InterpolationMode::Linear ||
      state.interpolation > InterpolationMode::Decelerate)
    throw std::runtime_error("Invalid saved wipe state");
  auto active = std::make_shared<SiglusWipeState>();
  active->promise = execution.Promise(state.promise);
  active->key_wait_mode = state.key_mode;
  active->cancelled = state.cancelled;
  active->completion.regular_boundaries = state.boundaries;
  if (state.finish_boundary >= 0)
    active->completion.finish_after_boundary = state.finish_boundary;
  active->completion.result = state.result;
  libsiglus::binding::RestoreWipeOperation(machine, state.owner, active);
  auto* stage = state.owner == 0 ? machine.host().stage.get()
                                 : &machine.host().excall->stage;
  auto counter = std::make_shared<Counter>();
  counter->StartGame();
  FrameCounter progress(counter,
                        Interpolation(Range(0, 1), InterpolationType::OneShot,
                                      state.interpolation),
                        state.duration);
  progress.BeginTimer();
  if (state.finish_boundary >= 0)
    progress.EndTimer();
  auto value = std::make_unique<SiglusWipeOperation>(
      machine.host().system.get(), stage, state.wait_flag, active,
      std::move(progress), state.duration, state.interpolation,
      machine.host().global_config);
  value->primed_ = state.finish_boundary >= 0 && state.primed;
  value->on_start_ = [host = &machine.host(), counter, stage, state] {
    host->RegisterCounter(counter, state.owner, false);
    if (stage)
      stage->SetTransitionRenderAlpha(state.finish_boundary >= 0 ? 1.0 : 0.0,
                                      state.finish_boundary >= 0 ? 0.0 : 1.0);
  };
  return value;
}

libsiglus::MovieOperationState SiglusMovieOperation::CaptureCheckpoint(
    libsiglus::ExecutionWriter& execution) const {
  libsiglus::MovieOperationState state;
  state.path = path_.string();
  if (auto base = sound_.system().gameexe()("__GAMEPATH").Str();
      base && !base->empty()) {
    auto local = path_.lexically_relative(*base);
    if (!local.empty() && *local.begin() != "..") {
      state.path = local.string();
      state.relative_path = true;
    }
  }
  state.rectangle = rect_;
  state.volume = volume_;
  state.wait_key = wait_key_;
  state.cancelled = stopped_;
  state.promise = execution.PromiseId(completion_);
  return state;
}
std::unique_ptr<LongOperation> SiglusMovieOperation::RestoreCheckpoint(
    libsiglus::SiglusMachine& machine,
    libsiglus::ExecutionReader& execution,
    const libsiglus::MovieOperationState& state) {
  if (state.cancelled)
    return std::make_unique<CompletedOperation>(0);
  const auto path = state.relative_path ? machine.host().base_pth / state.path
                                        : std::filesystem::path(state.path);
  auto* system = machine.host().system.get();
  if (!system || !machine.host().presenter || !std::filesystem::exists(path))
    throw std::runtime_error("Saved movie resource is unavailable");
  FfmpegVideoDecoder validate(path);
  auto value = std::make_unique<SiglusMovieOperation>(
      system, machine.host().presenter->backend(), path, state.rectangle,
      state.volume, state.wait_key);
  value->completion_ = execution.Promise(state.promise);
  libsiglus::binding::RestoreMovieOperation(machine, value.get(),
                                            value->completion_);
  value->on_start_ = [host = &machine.host(), frame = value->frame()] {
    host->movie_frame = frame;
  };
  return value;
}
}  // namespace lop

namespace libsiglus {
// Operation owners define logical checkpoint state; this layer dispatches
// supported types without inspecting their private coroutine implementation.
NativeOperationState CaptureNativeOperation(lop::LongOperation& operation,
                                            ExecutionWriter& execution) {
#define CAPTURE(Type)                                     \
  if (auto* value = dynamic_cast<lop::Type*>(&operation)) \
    return value->CaptureCheckpoint(execution);
  CAPTURE(SiglusTextoutOperation)
  CAPTURE(SiglusMwndWaitOperation)
  CAPTURE(WaitForSelection)
  CAPTURE(SiglusWipeOperation)
  CAPTURE(SiglusMovieOperation)
#undef CAPTURE
  throw std::runtime_error(
      "Unsupported native Siglus operation during checkpoint");
}
std::unique_ptr<lop::LongOperation> RestoreNativeOperation(
    SiglusMachine& machine,
    ExecutionReader& execution,
    const NativeOperationState& saved) {
  return std::visit(
      [&](const auto& state) -> std::unique_ptr<lop::LongOperation> {
        using T = std::decay_t<decltype(state)>;
        if constexpr (std::is_same_v<T, TextoutOperationState>)
          return lop::SiglusTextoutOperation::RestoreCheckpoint(
              machine, execution, state);
        else if constexpr (std::is_same_v<T, MessageWaitOperationState>)
          return lop::SiglusMwndWaitOperation::RestoreCheckpoint(
              machine, execution, state);
        else if constexpr (std::is_same_v<T, SelectionWaitOperationState>)
          return lop::WaitForSelection::RestoreCheckpoint(machine, execution,
                                                          state);
        else if constexpr (std::is_same_v<T, WipeOperationState>)
          return lop::SiglusWipeOperation::RestoreCheckpoint(machine, execution,
                                                             state);
        else
          return lop::SiglusMovieOperation::RestoreCheckpoint(machine,
                                                              execution, state);
      },
      saved);
}
}  // namespace libsiglus
