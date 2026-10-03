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

#include "libsiglus/bindings/registry.hpp"

#include "core/frame_counter.hpp"
#include "core/stage.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/bindings/wait_helpers.hpp"
#include "srbind/module.hpp"
#include "srbind/srbind.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"
#include "vm/dict.hpp"
#include "vm/future.hpp"
#include "vm/list.hpp"
#include "vm/promise.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace libsiglus::binding {
namespace sr = serilang;
namespace sb = srbind;

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
  SiglusWipe(System* system, Stage* stage) : system_(system), stage_(stage) {}
  ~SiglusWipe() { EndCurrent(0); }

  struct ActiveWipe {
    std::shared_ptr<sr::Promise> promise;
    int key_wait_mode = -1;
    int result = 0;
    bool cancelled = false;
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
             FrameCounter progress_counter)
        : CoroutineTask(vm, system ? system->event_ptr().get() : nullptr),
          system_(system),
          stage_(stage),
          params_(std::move(params)),
          active_(std::move(active)),
          start_ticks_(system ? system->event().GetTicks() : 0),
          progress_counter_(std::move(progress_counter)) {}

    TaskCoroutine Run() override {
      while (true) {
        if (IsCancelled())
          co_return CurrentResult();

        if (!system_ || !stage_) {
          FinishStage();
          co_return 0;
        }

        if (system_->ShouldFastForward() ||
            system_->graphics().should_skip_animations()) {
          FinishStage();
          co_return 0;
        }

        const int elapsed = ElapsedTime();
        if (elapsed >= params_.wipe_time) {
          FinishStage();
          co_return 0;
        }

        float progress = progress_counter_.ReadFrame();
        stage_->SetTransitionRenderAlpha(progress, 1.0 - progress);

        const WaitOutcome outcome =
            co_await WaitFor(kPollInterval, ShouldInterruptOnInput());
        if (outcome == WaitOutcome::InterruptedByInput) {
          FinishStage();
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
        return active->result;
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

      return system_ && system_->graphics().should_skip_animations() != 0;
    }

    int ElapsedTime() const {
      const unsigned int now = system_->event().GetTicks();
      return params_.start_time + static_cast<int>(now - start_ticks_);
    }

    void FinishStage() {
      if (!stage_)
        return;

      stage_->ClearTransitionRenderState();
      stage_->next_objects.Clear();
    }

    System* system_ = nullptr;
    Stage* stage_ = nullptr;
    WipeParams params_;
    std::weak_ptr<ActiveWipe> active_;
    unsigned int start_ticks_ = 0;
    FrameCounter progress_counter_;
  };

  sr::Value Start(sr::VM& vm, WipeParams params) {
    EndCurrent(0);

    if (!stage_)
      return MakeResolvedFuture(*vm.gc_);

    stage_->Wipe(params.begin_order, params.end_order, params.begin_layer,
                 params.end_layer);

    if (!system_) {
      ClearWipeState();
      return MakeResolvedFuture(*vm.gc_);
    }

    if (ShouldCompleteImmediately(params)) {
      ClearWipeState();
      return MakeResolvedFuture(*vm.gc_);
    }

    auto active = std::make_shared<ActiveWipe>();
    active->key_wait_mode = params.wait_flag ? params.key_wait_mode : -1;
    active_ = active;

    auto progress_counter =
        MakeWipeFrameCounter(params, system_->event().GetClock());
    const double initial_progress = progress_counter.ReadFrame();
    stage_->SetTransitionRenderAlpha(initial_progress, 1.0 - initial_progress);

    const bool wait_flag = params.wait_flag;
    auto task =
        std::make_unique<WipeTask>(vm, system_, stage_, std::move(params),
                                   active, std::move(progress_counter));
    FutureBackedCoroutineTask pending(std::move(task));
    pending.Start();  // eager start is required
    sr::Future* future = pending_.MakeFuture(*vm.gc_, std::move(pending));
    active->promise = future->promise;
    vm.TrackPendingPromise(future->promise);

    if (!wait_flag)
      return MakeResolvedFuture(*vm.gc_);

    return sr::Value(future);
  }

  sr::Value Wait(sr::VM& vm, int key_wait_mode) {
    if (!IsActive())
      return MakeResolvedFuture(*vm.gc_);

    if (active_)
      active_->key_wait_mode = key_wait_mode;

    return MakeFutureForActive(vm);
  }

  void EndCurrent(int result) {
    if (!active_)
      return;

    active_->result = result;
    active_->cancelled = true;
    if (active_->promise)
      active_->promise->Resolve(sr::Value(result));
    active_ = nullptr;
    ClearWipeState();
  }

  void ClearWipeState() {
    if (stage_) {
      stage_->ClearTransitionRenderState();
      stage_->next_objects.Clear();
    }
  }

  bool ShouldCompleteImmediately(const WipeParams& params) const {
    return params.wipe_time <= 0 || params.start_time >= params.wipe_time ||
           system_->graphics().should_skip_animations() ||
           system_->ShouldFastForward();
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
  std::shared_ptr<ActiveWipe> active_;
  PendingCoroutineTasks pending_;
};

void BindWipe(SiglusRuntime& runtime) {
  sr::VM& vm = *runtime.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  sb::class_<SiglusWipe> wipe_cls(m, "Wipe", false);
  auto wipe = wipe_cls.inst("wipe", runtime.system.get(), runtime.stage.get());

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
