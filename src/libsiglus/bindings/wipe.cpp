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

#include "libsiglus/bindings/registry.hpp"
#include "long_operations/siglus_wipe.hpp"

#include "core/frame_action.hpp"
#include "core/frame_counter.hpp"
#include "core/stage.hpp"
#include "libsiglus/bindings/util.hpp"
#include "long_operations/siglus_wait.hpp"
#include "srbind/module.hpp"
#include "srbind/srbind.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"
#include "systems/text_waku.hpp"
#include "systems/text_window.hpp"
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
  struct Control {
    SiglusWipe* owner = nullptr;
  };

  SiglusWipe(SiglusMachine& runtime, std::shared_ptr<Control> control)
      : runtime_(runtime),
        system_(runtime.system.get()),
        stage_(runtime.stage.get()),
        global_config_(runtime.global_config),
        control_(std::move(control)) {
    control_->owner = this;
  }

  // Teardown invalidates the external hook before releasing coroutine state.
  ~SiglusWipe() { control_->owner = nullptr; }

  FrameCounter MakeWipeFrameCounter(const WipeParams& params) {
    auto source = runtime_.CreateCounter();
    source->StartGame();
    FrameCounter counter(std::move(source),
                         Interpolation(Range(0, 1), InterpolationType::OneShot,
                                       GetInterpolationMode(params.speed_mode)),
                         params.wipe_time);
    counter.BeginTimer(std::chrono::milliseconds(0) -
                       std::chrono::milliseconds(params.start_time));
    return counter;
  }

  static void CancelObjectActions(
      const std::shared_ptr<GraphicsObject>& object) {
    if (!object)
      return;
    object->ResetFrameActions();
    for (const auto& child : object->GetChildren())
      CancelObjectActions(child);
  }

  static void CancelWindowActions(const std::shared_ptr<TextWindow>& window) {
    if (!window)
      return;
    for (auto* waku : {window->GetTextboxWaku(), window->GetNameboxWaku()}) {
      if (!waku)
        continue;
      for (const auto* list :
           {&waku->button_objects_, &waku->faces_, &waku->objects_}) {
        for (const auto& object : *list)
          CancelObjectActions(object);
      }
    }
    window->ClearMouseCapture();
  }

  void ClearWipeState() {
    if (!stage_)
      return;
    stage_->selection[kLayerNext].reset();
    for (auto& group : stage_->groups[kLayerNext])
      group.reset();
    for (auto& object : stage_->objects[kLayerNext]) {
      CancelObjectActions(object);
      object.reset();
    }
    for (auto& effect : stage_->effects[kLayerNext])
      effect.reset();
    for (auto& window : stage_->mwnd[kLayerNext]) {
      CancelWindowActions(window);
      window.reset();
    }
    stage_->ClearTransitionRenderState();
  }

  void UpdateAfterFrameActions() {
    if (active_ && active_->completion.AfterFrameActions())
      EndCurrent(active_->completion.result);
  }

  void ScheduleFinishActive(const std::shared_ptr<lop::SiglusWipeState>& active,
                            int result,
                            int regular_boundaries = 1) {
    if (!active || active_ != active || active->cancelled)
      return;
    active->completion.RequestFinish(result, regular_boundaries);
  }

  sr::Value Start(sr::VM& vm, WipeParams params) {
    EndCurrent(0);
    if (!stage_)
      return lop::MakeResolvedFuture(*vm.gc_);

    // Wipe clones old foreground actions into Next. Cancel only allocations
    // displaced by the swap, after their transition copies have been made.
    auto previous_foreground = stage_->objects[kLayerFg];
    auto previous_windows = stage_->mwnd[kLayerFg];
    stage_->Wipe(params.begin_order, params.end_order, params.begin_layer,
                 params.end_layer);
    for (std::size_t i = 0; i < previous_foreground.size(); ++i) {
      if (i >= stage_->objects[kLayerFg].size() ||
          previous_foreground[i] != stage_->objects[kLayerFg][i]) {
        CancelObjectActions(previous_foreground[i]);
      }
    }

    for (std::size_t i = 0; i < previous_windows.size(); ++i)
      if (previous_windows[i] != stage_->mwnd[kLayerFg][i])
        CancelWindowActions(previous_windows[i]);

    if (!system_ || global_config_.system.disable_wipe_anime) {
      ClearWipeState();
      return lop::MakeResolvedFuture(*vm.gc_);
    }

    auto active = std::make_shared<lop::SiglusWipeState>();
    active->promise = std::make_shared<sr::Promise>();
    active->key_wait_mode = params.wait_flag ? params.key_wait_mode : -1;
    active_ = active;

    const bool wait_flag = params.wait_flag;
    const bool initially_terminal =
        params.wipe_time <= 0 || params.start_time >= params.wipe_time;
    auto progress_counter = MakeWipeFrameCounter(params);
    const double initial_progress =
        initially_terminal ? 0.0 : progress_counter.ReadFrame();
    stage_->SetTransitionRenderRange(params.begin_order, params.end_order,
                                     params.begin_layer, params.end_layer);
    stage_->SetTransitionRenderAlpha(initial_progress, 1.0 - initial_progress);

    if (initially_terminal) {
      // Keep Next through the command's priming frame and the next traversal.
      ScheduleFinishActive(active, 0, 2);
    } else if (system_->ShouldFastForward() && wait_flag) {
      ScheduleFinishActive(active, 0, 1);
    }

    auto task = std::make_unique<lop::SiglusWipeOperation>(
        system_, stage_, params.wait_flag, active, std::move(progress_counter),
        global_config_);
    lop::FutureBackedCoroutineTask pending(vm, std::move(task),
                                           system_->event_ptr().get());
    pending.Start();
    sr::Future* future = pending_.MakeFuture(*vm.gc_, std::move(pending));
    vm.TrackPendingPromise(future->promise);

    return wait_flag ? MakeFutureForActive(vm)
                     : lop::MakeResolvedFuture(*vm.gc_);
  }

  sr::Value Wait(sr::VM& vm, int key_wait_mode) {
    if (!IsActive())
      return lop::MakeResolvedFuture(*vm.gc_);
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

  SiglusMachine& runtime_;
  System* system_ = nullptr;
  Stage* stage_ = nullptr;
  const GlobalConfig& global_config_;
  std::shared_ptr<Control> control_;
  std::shared_ptr<lop::SiglusWipeState> active_;
  lop::PendingCoroutineTasks pending_;
};

void BindWipe(SiglusMachine& runtime) {
  sr::VM& vm = *runtime.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  sb::class_<SiglusWipe> wipe_cls(m, "Wipe", false);
  auto control = std::make_shared<SiglusWipe::Control>();
  auto wipe = wipe_cls.inst("wipe", runtime, control);

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
