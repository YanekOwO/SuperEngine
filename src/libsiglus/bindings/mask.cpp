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

#include "core/interpolation.hpp"
#include "libsiglus/bindings/registry.hpp"

#include "core/mask.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/bindings/wait_helpers.hpp"
#include "srbind/module.hpp"
#include "srbind/srbind.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"
#include "vm/object.hpp"
#include "vm/value.hpp"
#include "vm/vm.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace libsiglus::binding {
namespace sb = srbind;
namespace sr = serilang;

struct MaskHandle {
  std::shared_ptr<MaskList> list_;
  int index_;

  Mask& Get() { return list_->At(index_); }
  const Mask& Get() const { return list_->At(index_); }
};

struct MaskEventHandle {
  MaskHandle mask_;
  Mask::Parameter parameter_;
  EventSystem* event_;

  EventSystem& GetEvent() {
    if (!event_)
      throw std::runtime_error("mask event requires an event system");
    return *event_;
  }
  inline Mask& Get() { return mask_.Get(); }
};

void BindMask(SiglusRuntime& runtime) {
  std::shared_ptr<MaskList> list = runtime.mask_list;
  if (!list)
    throw std::runtime_error("mask binding requires mask state");

  sr::VM& vm = *runtime.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  EventSystem* event = runtime.system ? &runtime.system->event() : nullptr;
  GraphicsSystem* graphics =
      runtime.system ? &runtime.system->graphics() : nullptr;

  // ------------------------------------------------------------------------------
  // MaskEvent
  sb::class_<MaskEventHandle> event_class(m, "MaskEvent", false);
  // starting oneshot event
  auto mask_eve_set = [](MaskEventHandle* h, int value, int duration, int delay,
                         int speed_type) {
    Mask& mask = h->Get();
    mask.EndMutator(h->parameter_);

    const int start = mask.Param().*(h->parameter_);
    auto fc = MakeRepeatingFrameCounter(InterpolationType::OneShot, duration,
                                        delay, start, value, speed_type,
                                        h->GetEvent().GetClock());
    mask.AddMutator(Mask::ParameterMutator(h->parameter_, fc));
  };
  event_class.def("set", mask_eve_set, sb::arg("value"), sb::arg("duration"),
                  sb::arg("delay"), sb::arg("speed_type"));
  event_class.def("set_real", mask_eve_set, sb::arg("value"),
                  sb::arg("duration"), sb::arg("delay"), sb::arg("speed_type"));
  // starting loop event
  auto mask_eve_repeat = [](InterpolationType type) {
    return [type](MaskEventHandle* h, int start, int end, int duration,
                  int delay, int speed_type) {
      Mask& mask = h->Get();
      mask.EndMutator(h->parameter_);
      auto fc = MakeRepeatingFrameCounter(type, duration, delay, start, end,
                                          speed_type, h->GetEvent().GetClock());
      mask.AddMutator(Mask::ParameterMutator(h->parameter_, fc));
    };
  };
  event_class.def("loop", mask_eve_repeat(InterpolationType::Loop),
                  sb::arg("start"), sb::arg("end"), sb::arg("duration"),
                  sb::arg("delay"), sb::arg("speed_type"));
  event_class.def("loop_real", mask_eve_repeat(InterpolationType::Loop),
                  sb::arg("start"), sb::arg("end"), sb::arg("duration"),
                  sb::arg("delay"), sb::arg("speed_type"));
  event_class.def("turn", mask_eve_repeat(InterpolationType::Turn),
                  sb::arg("start"), sb::arg("end"), sb::arg("duration"),
                  sb::arg("delay"), sb::arg("speed_type"));
  event_class.def("turn_real", mask_eve_repeat(InterpolationType::Turn),
                  sb::arg("start"), sb::arg("end"), sb::arg("duration"),
                  sb::arg("delay"), sb::arg("speed_type"));
  // end event
  event_class.def("end", [](MaskEventHandle* h) {
    Mask& mask = h->Get();
    mask.EndMutator(h->parameter_);
  });
  // check if the event is still running
  event_class.def("check", [](MaskEventHandle* h) {
    const Mask& mask = h->Get();
    const bool is_running = mask.IsMutatorRunning(h->parameter_);
    return is_running ? 1 : 0;
  });
  // wait until event finishes
  event_class.def(
      "wait",
      [](MaskEventHandle* h, sr::VM& vm, std::vector<sr::Value>) {
        auto check = [mask = h->mask_, param = h->parameter_] {
          return !mask.Get().IsMutatorRunning(param);
        };
        return MakePollingWaitFuture(vm, check, false, h->event_);
      },
      sb::vararg);
  event_class.def(
      "wait_key",
      [](MaskEventHandle* h, sr::VM& vm, std::vector<sr::Value>) {
        auto check = [mask = h->mask_, param = h->parameter_] {
          return !mask.Get().IsMutatorRunning(param);
        };
        return MakePollingWaitFuture(vm, check, true, h->event_);
      },
      sb::vararg);

  // ------------------------------------------------------------------------------
  // Mask
  sb::class_<MaskHandle> mask_class(m, "Mask");
  mask_class.def("init", [](MaskHandle* mask) { mask->Get().Reset(); });
  mask_class.def(
      "create",
      [graphics](MaskHandle* mask, std::string filename) {
        if (!graphics)
          throw std::runtime_error("mask.create requires a graphics system");
        mask->Get().Reset();
        std::shared_ptr<SDLSurface> surface =
            graphics->GetSurfaceNamed(filename);
        mask->Get().Create(std::move(filename), std::move(surface));
      },
      sb::arg("filename"));
  mask_class.def("x",
                 [](const MaskHandle* mask) { return mask->Get().Param().x; });
  mask_class.def("set_x", [](MaskHandle* mask, int value) {
    mask->Get().Param().x = value;
  });
  mask_class.def("y",
                 [](const MaskHandle* mask) { return mask->Get().Param().y; });
  mask_class.def("set_y", [](MaskHandle* mask, int value) {
    mask->Get().Param().y = value;
  });
  mask_class.subcls("x_eve", event_class, [event](MaskHandle* mask) {
    return std::make_unique<MaskEventHandle>(*mask, &Mask::Parameters::x,
                                             event);
  });
  mask_class.subcls("y_eve", event_class, [event](MaskHandle* mask) {
    return std::make_unique<MaskEventHandle>(*mask, &Mask::Parameters::y,
                                             event);
  });

  // ------------------------------------------------------------------------------
  // MaskList
  struct MaskListHandle {
    std::shared_ptr<MaskList> list;
  };
  sb::class_<MaskListHandle> list_class(m, "MaskList");
  auto mask_list = list_class.inst("mask", list);  // top level |mask| element
  mask_list.def(
      "__getitem__",
      [mask_class](MaskListHandle* h, int index) mutable {
        sr::NativeInstance* mask_handle = mask_class.make_inst(h->list, index);
        return sr::Value(mask_handle);
      },
      sb::arg("index"));
  mask_list.def("size",
                [](MaskListHandle* h) -> int { return h->list->size(); });
}

RLVM_REGISTER(SiglusBindingRegistry, "1_mask", BindMask)

}  // namespace libsiglus::binding
