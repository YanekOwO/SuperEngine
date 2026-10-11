// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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

#include "libsiglus/bindings/wipe.hpp"
#include "libsiglus/bindings/registry.hpp"

#include "core/frame_action.hpp"
#include "core/frame_counter.hpp"
#include "core/stage.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/bindings/wait_helpers.hpp"
#include "srbind/module.hpp"
#include "srbind/srbind.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"
#include "vm/future.hpp"
#include "vm/list.hpp"
#include "vm/promise.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace libsiglus::binding {
namespace sr = serilang;
namespace sb = srbind;

void WipeCompletion::RequestFinish(int requested_result,
                                   int minimum_total_boundaries) {
  if (finish_after_boundary) {
    // Input interruption takes precedence over natural completion.
    if (requested_result != 0) {
      result = requested_result;
      finish_after_boundary =
          std::min(*finish_after_boundary, regular_boundaries + 1);
    }
    return;
  }
  result = requested_result;
  finish_after_boundary =
      std::max(std::max(1, minimum_total_boundaries), regular_boundaries + 1);
}

bool WipeCompletion::AfterFrameActions() {
  ++regular_boundaries;
  return finish_after_boundary && regular_boundaries >= *finish_after_boundary;
}

constexpr int kWipeOptionMax = 8;

struct WipeParams {
  int wipe_type = 0;
  int wipe_time = 500;
  int speed_mode = 0;
  int start_time = 0;
  std::array<int, kWipeOptionMax> option{};
  int begin_order = 0;
  int end_order = 0;
  int begin_layer = std::numeric_limits<int>::min();
  int end_layer = std::numeric_limits<int>::max();
  bool wait_flag = true;
  int key_wait_mode = -1;
  int with_low_order = 0;
  std::string mask_file;

  static WipeParams DecodeFrom(std::vector<sr::Value> raw_args,
                               bool masked,
                               bool all) {
    WipeParams params;
    if (all)
      params.end_order = std::numeric_limits<int>::max();

    CallPacket packet = CallPacket::DecodeFrom(std::move(raw_args));
    const auto& args = packet.args;
    if (masked) {
      if (args.size() > 0)
        params.mask_file = AsString(args[0]);
      if (args.size() > 1)
        params.wipe_type = AsInt(args[1]).value_or(0);
      if (args.size() > 2)
        params.wipe_time = AsInt(args[2]).value_or(0);
      if (args.size() > 3)
        params.speed_mode = AsInt(args[3]).value_or(0);
      if (args.size() > 4)
        CopyOptions(args[4], params.option);
    } else {
      if (args.size() > 0)
        params.wipe_type = AsInt(args[0]).value_or(0);
      if (args.size() > 1)
        params.wipe_time = AsInt(args[1]).value_or(0);
      if (args.size() > 2)
        params.speed_mode = AsInt(args[2]).value_or(0);
      if (args.size() > 3)
        CopyOptions(args[3], params.option);
    }

    const auto& kwargs = packet.kwargs;
    ForEachKeywordId(kwargs, [&](int id, const sr::Value& value) {
      switch (id) {
        case 0:
          params.wipe_type = AsInt(value).value_or(0);
          break;
        case 1:
          params.wipe_time = AsInt(value).value_or(0);
          break;
        case 2:
          params.speed_mode = AsInt(value).value_or(0);
          break;
        case 3:
          CopyOptions(value, params.option);
          break;
        case 4:
          params.begin_order = AsInt(value).value_or(0);
          break;
        case 5:
          params.end_order = AsInt(value).value_or(0);
          break;
        case 6:
          params.begin_layer = AsInt(value).value_or(0);
          break;
        case 7:
          params.end_layer = AsInt(value).value_or(0);
          break;
        case 8:
          params.wait_flag = AsInt(value).value_or(0) != 0;
          break;
        case 9:
          params.key_wait_mode = AsInt(value).value_or(0);
          break;
        case 10:
          params.with_low_order = AsInt(value).value_or(0);
          break;
        case 11:
          params.start_time = AsInt(value).value_or(0);
          break;
        default:
          break;
      }
    });

    return params;
  }

  static void CopyOptions(const sr::Value& value,
                          std::array<int, kWipeOptionMax>& options) {
    const sr::List* list = value.Get_if<sr::List>();
    if (!list) {
      options[0] = AsInt(value).value_or(0);
      return;
    }

    const std::size_t count =
        std::min(list->items.size(), static_cast<std::size_t>(kWipeOptionMax));
    for (std::size_t i = 0; i < count; ++i)
      options[i] = AsInt(list->items[i]).value_or(0);
  }
};

struct SiglusWipe {
  struct Control {
    SiglusWipe* owner = nullptr;
  };

  SiglusWipe(System* system,
             Stage* stage,
             std::shared_ptr<bool> no_wipe_animation,
             std::shared_ptr<bool> skip_wipe_animation,
             std::shared_ptr<Control> control)
      : system_(system),
        stage_(stage),
        no_wipe_animation_(std::move(no_wipe_animation)),
        skip_wipe_animation_(std::move(skip_wipe_animation)),
        control_(std::move(control)) {
    control_->owner = this;
  }

  // The VM (and therefore this native object) is destroyed after several
  // runtime services. Teardown must only invalidate the external control
  // hook; touching the borrowed System, Stage, or manager pointers here would
  // use already-destroyed objects.
  ~SiglusWipe() { control_->owner = nullptr; }

  struct ActiveWipe {
    std::shared_ptr<sr::Promise> promise;
    int key_wait_mode = -1;
    bool cancelled = false;
    WipeCompletion completion;
  };

  FrameCounter MakeWipeFrameCounter(const WipeParams& params,
                                    std::shared_ptr<Clock> clock) {
    InterpolationMode mode = InterpolationMode::Linear;
    switch (params.speed_mode) {
      case 1:
        mode = InterpolationMode::Accelerate;
        break;
      case 2:
        mode = InterpolationMode::Decelerate;
        break;
      case 0:
      default:
        break;
    }

    FrameCounter counter(
        std::move(clock),
        Interpolation(Range(0, 1), InterpolationType::OneShot, mode),
        params.wipe_time);
    counter.BeginTimer(std::chrono::milliseconds(0) -
                       std::chrono::milliseconds(params.start_time));
    return counter;
  }

  struct WipeTask final : CoroutineTask {
    WipeTask(sr::VM& vm,
             System* system,
             Stage* stage,
             WipeParams params,
             std::weak_ptr<ActiveWipe> active,
             FrameCounter progress_counter,
             std::shared_ptr<bool> skip_wipe_animation,
             std::function<void(int, int)> finish_stage)
        : CoroutineTask(vm, system ? system->event_ptr().get() : nullptr),
          system_(system),
          stage_(stage),
          params_(std::move(params)),
          active_(std::move(active)),
          start_ticks_(system ? system->event().GetTicks() : 0),
          progress_counter_(std::move(progress_counter)),
          skip_wipe_animation_(std::move(skip_wipe_animation)),
          finish_stage_(std::move(finish_stage)) {}

    TaskCoroutine Run() override {
      // Polling starts after the priming interval. Start handles
      // already-terminal wipes separately so their exact two regular
      // frame-action boundaries do not depend on timer phase.
      const WaitOutcome primed =
          co_await WaitFor(kPollInterval, ShouldInterruptOnInput());
      if (primed == WaitOutcome::InterruptedByInput) {
        FinishStage(1);
        co_return 1;
      }

      while (true) {
        if (IsCancelled())
          co_return CurrentResult();

        if (!system_ || !stage_) {
          FinishStage(0);
          co_return 0;
        }

        const bool fast_forward = system_->ShouldFastForward();
        if (fast_forward && params_.wait_flag) {
          // Legacy's wipe-wait process explicitly ends a skipped wipe before
          // its next regular traversal. StartPrepared pre-schedules this case
          // when skipping is already active; this also handles it becoming
          // active later.
          FinishStage(0, 1);
          co_return 0;
        }

        // A non-waiting wipe is not force-ended by skipping. Its game-time
        // clock advances at Siglus' 32x rate instead.
        const int elapsed = ElapsedTime(fast_forward);
        if (elapsed >= params_.wipe_time) {
          FinishStage(0, 3);
          co_return 0;
        }

        float progress =
            fast_forward ? ProgressAt(elapsed) : progress_counter_.ReadFrame();
        stage_->SetTransitionRenderAlpha(progress, 1.0 - progress);

        const WaitOutcome outcome =
            co_await WaitFor(kPollInterval, ShouldInterruptOnInput());
        if (outcome == WaitOutcome::InterruptedByInput) {
          FinishStage(1);
          co_return 1;
        }
      }
    }

   private:
    static constexpr std::chrono::milliseconds kPollInterval =
        std::chrono::milliseconds(5);

    bool IsCancelled() const {
      std::shared_ptr<ActiveWipe> active = active_.lock();
      if (!active)
        return true;
      if (active->cancelled)
        return true;
      return active->promise && active->promise->HasResult();
    }

    int CurrentResult() const {
      if (std::shared_ptr<ActiveWipe> active = active_.lock())
        return active->completion.result;
      return 0;
    }

    bool ShouldInterruptOnInput() const {
      std::shared_ptr<ActiveWipe> active = active_.lock();
      if (!active)
        return false;

      const int key_wait_mode = active->key_wait_mode;
      if (key_wait_mode == 0)
        return false;
      if (key_wait_mode == 1)
        return true;

      return !skip_wipe_animation_ || *skip_wipe_animation_;
    }

    int ElapsedTime(bool fast_forward) const {
      const unsigned int now = system_->event().GetTicks();
      const std::uint64_t real_elapsed = now - start_ticks_;
      const std::uint64_t multiplier = fast_forward ? 32 : 1;
      const std::uint64_t scaled = std::min<std::uint64_t>(
          real_elapsed * multiplier,
          static_cast<std::uint64_t>(std::numeric_limits<int>::max()));
      return params_.start_time + static_cast<int>(scaled);
    }

    float ProgressAt(int elapsed) const {
      if (params_.wipe_time <= 0)
        return 1.0f;
      InterpolationMode mode = InterpolationMode::Linear;
      if (params_.speed_mode == 1)
        mode = InterpolationMode::Accelerate;
      else if (params_.speed_mode == 2)
        mode = InterpolationMode::Decelerate;
      const double normalized = std::clamp(
          static_cast<double>(elapsed) / params_.wipe_time, 0.0, 1.0);
      return static_cast<float>(
          Interpolation(Range(0, 1), InterpolationType::OneShot, mode)
              .ValueAt(normalized));
    }

    void FinishStage(int result, int regular_boundaries = 1) {
      if (finish_stage_)
        finish_stage_(result, regular_boundaries);
    }

    System* system_ = nullptr;
    Stage* stage_ = nullptr;
    WipeParams params_;
    std::weak_ptr<ActiveWipe> active_;
    unsigned int start_ticks_ = 0;
    FrameCounter progress_counter_;
    std::shared_ptr<bool> skip_wipe_animation_;
    std::function<void(int, int)> finish_stage_;
  };

  static void CancelObjectActions(
      const std::shared_ptr<GraphicsObject>& object) {
    if (!object)
      return;
    object->frame_action_.Reset();
    object->frame_action_list_.clear();
    for (const auto& child : object->GetChildren())
      CancelObjectActions(child);
  }

  void ClearWipeState() {
    if (!stage_)
      return;
    for (auto& group : stage_->groups[kLayerNext])
      group.reset();
    for (auto& object : stage_->objects[kLayerNext]) {
      CancelObjectActions(object);
      object.reset();
    }
    for (auto& effect : stage_->effects[kLayerNext])
      effect.reset();
    stage_->ClearTransitionRenderState();
  }

  void UpdateAfterFrameActions() {
    if (active_ && active_->completion.AfterFrameActions())
      EndCurrent(active_->completion.result);
  }

  void ScheduleFinishActive(const std::shared_ptr<ActiveWipe>& active,
                            int result,
                            int regular_boundaries = 1) {
    if (!active || active_ != active || active->cancelled)
      return;
    active->completion.RequestFinish(result, regular_boundaries);
  }

  sr::Value Start(sr::VM& vm, WipeParams params) {
    EndCurrent(0);
    if (!stage_)
      return MakeResolvedFuture(*vm.gc_);

    // Wipe clones old foreground actions into Next. Cancel only allocations
    // displaced by the swap, after their transition copies have been made.
    auto previous_foreground = stage_->objects[kLayerFg];
    stage_->Wipe(params.begin_order, params.end_order, params.begin_layer,
                 params.end_layer);
    for (std::size_t i = 0; i < previous_foreground.size(); ++i) {
      if (i >= stage_->objects[kLayerFg].size() ||
          previous_foreground[i] != stage_->objects[kLayerFg][i]) {
        CancelObjectActions(previous_foreground[i]);
      }
    }

    if (!system_ || (no_wipe_animation_ && *no_wipe_animation_)) {
      ClearWipeState();
      return MakeResolvedFuture(*vm.gc_);
    }

    auto active = std::make_shared<ActiveWipe>();
    active->promise = std::make_shared<sr::Promise>();
    active->key_wait_mode = params.wait_flag ? params.key_wait_mode : -1;
    active_ = active;

    const bool wait_flag = params.wait_flag;
    const bool initially_terminal =
        params.wipe_time <= 0 || params.start_time >= params.wipe_time;
    auto progress_counter =
        MakeWipeFrameCounter(params, system_->event().GetClock());
    const double initial_progress =
        initially_terminal ? 0.0 : progress_counter.ReadFrame();
    stage_->SetTransitionRenderAlpha(initial_progress, 1.0 - initial_progress);

    if (initially_terminal) {
      // Keep Next through the command's priming frame and the next traversal.
      ScheduleFinishActive(active, 0, 2);
    } else if (system_->ShouldFastForward() && wait_flag) {
      ScheduleFinishActive(active, 0, 1);
    }

    std::weak_ptr<ActiveWipe> weak_active = active;
    auto task = std::make_unique<WipeTask>(
        vm, system_, stage_, std::move(params), active,
        std::move(progress_counter), skip_wipe_animation_,
        [control = control_, weak_active](int result, int regular_boundaries) {
          if (control->owner) {
            control->owner->ScheduleFinishActive(weak_active.lock(), result,
                                                 regular_boundaries);
          }
        });
    FutureBackedCoroutineTask pending(std::move(task));
    pending.Start();
    sr::Future* future = pending_.MakeFuture(*vm.gc_, std::move(pending));
    vm.TrackPendingPromise(future->promise);

    return wait_flag ? MakeFutureForActive(vm) : MakeResolvedFuture(*vm.gc_);
  }

  sr::Value Wait(sr::VM& vm, int key_wait_mode) {
    if (!IsActive())
      return MakeResolvedFuture(*vm.gc_);
    active_->key_wait_mode = key_wait_mode;
    return MakeFutureForActive(vm);
  }

  void EndCurrent(int result) {
    auto previous = std::exchange(active_, {});
    if (previous) {
      previous->completion.result = result;
      previous->cancelled = true;
    }
    ClearWipeState();
    if (previous && previous->promise)
      previous->promise->Resolve(sr::Value(result));
  }

  bool IsActive() const {
    return active_ && active_->promise && !active_->promise->HasResult();
  }

  sr::Value MakeFutureForActive(sr::VM& vm) {
    sr::Future* future = vm.gc_->Allocate<sr::Future>();
    future->promise = active_->promise;
    return sr::Value(future);
  }

  System* system_ = nullptr;
  Stage* stage_ = nullptr;
  std::shared_ptr<bool> no_wipe_animation_;
  std::shared_ptr<bool> skip_wipe_animation_;
  std::shared_ptr<Control> control_;
  std::shared_ptr<ActiveWipe> active_;
  PendingCoroutineTasks pending_;
};

void BindWipe(SiglusRuntime& runtime) {
  sr::VM& vm = *runtime.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  sb::class_<SiglusWipe> wipe_cls(m, "Wipe", false);
  auto control = std::make_shared<SiglusWipe::Control>();
  auto wipe = wipe_cls.inst("wipe", runtime.system.get(), runtime.stage.get(),
                            runtime.no_wipe_animation,
                            runtime.skip_wipe_animation, control);

  if (!runtime.finish_active_wipe)
    runtime.finish_active_wipe = std::make_shared<SiglusWipeEndHook>();
  *runtime.finish_active_wipe = [control] {
    if (control->owner)
      control->owner->EndCurrent(0);
  };
  runtime.update_wipe_after_frame_actions = [control] {
    if (control->owner)
      control->owner->UpdateAfterFrameActions();
  };

  wipe.def(
      "wipe",
      [](SiglusWipe* wipe, sr::VM& vm, std::vector<sr::Value> args) {
        return wipe->Start(
            vm, WipeParams::DecodeFrom(std::move(args), false, false));
      },
      sb::vararg);
  wipe.def(
      "wipe_all",
      [](SiglusWipe* wipe, sr::VM& vm, std::vector<sr::Value> args) {
        return wipe->Start(
            vm, WipeParams::DecodeFrom(std::move(args), false, true));
      },
      sb::vararg);
  wipe.def(
      "wipe_mask",
      [](SiglusWipe* wipe, sr::VM& vm, std::vector<sr::Value> args) {
        return wipe->Start(
            vm, WipeParams::DecodeFrom(std::move(args), true, false));
      },
      sb::vararg);
  wipe.def(
      "wipe_mask_all",
      [](SiglusWipe* wipe, sr::VM& vm, std::vector<sr::Value> args) {
        return wipe->Start(vm,
                           WipeParams::DecodeFrom(std::move(args), true, true));
      },
      sb::vararg);
  wipe.def(
      "end",
      [](SiglusWipe* wipe, std::vector<sr::Value> args) {
        wipe->EndCurrent(0);
      },
      sb::vararg);
  wipe.def(
      "wait",
      [](SiglusWipe* wipe, sr::VM& vm, std::vector<sr::Value> args) {
        int key_wait_mode = -1;
        CallPacket packet = CallPacket::DecodeFrom(std::move(args));
        if (!packet.args.empty())
          key_wait_mode = AsInt(packet.args.front()).value_or(-1);
        ForEachKeywordId(packet.kwargs, [&](int id, const sr::Value& value) {
          if (id == 0)
            key_wait_mode = AsInt(value).value_or(-1);
        });
        return wipe->Wait(vm, key_wait_mode);
      },
      sb::vararg);
  wipe.def(
      "check",
      [](SiglusWipe* wipe, std::vector<sr::Value> args) {
        const int ret = wipe->IsActive() ? 1 : 0;
        return sr::Value(ret);
      },
      sb::vararg);
}

RLVM_REGISTER(SiglusBindingRegistry, "wipe", BindWipe)

}  // namespace libsiglus::binding
