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

#include <boost/algorithm/string/predicate.hpp>
#include "libsiglus/bindings/registry.hpp"
#include "long_operations/siglus_wait.hpp"
#include "m6/vm_factory.hpp"
#include "srbind/srbind.hpp"
#include "systems/event_system.hpp"
#include "systems/text_system.hpp"
#include "vm/future.hpp"
#include "vm/promise.hpp"
#include "vm/string.hpp"

namespace libsiglus {
namespace sr = serilang;

SiglusMachine::SiglusMachine(SiglusRuntime& runtime, int owner)
    : callback_owner(owner), runtime_(runtime) {}

SiglusMachine::~SiglusMachine() { Cancel(); }

void SiglusMachine::Cancel() {
  lifetime.reset();
  if (frame_requests)
    frame_requests->Reset();
  if (selection_state)
    selection_state->pending_.reset();
  if (vm) {
    for (auto* fiber : vm->fibres_)
      if (fiber->state != sr::FiberState::Dead)
        fiber->Kill(sr::nil);
    vm->RequestStop();
  }
}

void SiglusMachine::Initialize() {
  vm = std::make_unique<sr::VM>(m6::VMFactory::Create());
  loader = std::make_unique<binding::Loader>(*host().archive, *vm, host().debug,
                                             host().trace);
  frame_requests = std::make_unique<FrameRequestManager>([this] {
    auto promise = std::make_shared<sr::Promise>();
    vm->TrackPendingPromise(promise);
    return promise;
  });
  vm->SetHostEventPump([this] { host().PumpHostEvents(); });
  for (const auto& name : host().binding_names) {
    if (const auto* bind = binding::SiglusBindingRegistry::Find(name))
      (*bind)(*this);
  }
  srbind::module_ m(vm->gc_.get(), vm->globals_.get());
  m.def("__builtin_streq", [](sr::Value lhs, sr::Value rhs) {
    auto* a = lhs.Get_if<sr::String>();
    auto* b = rhs.Get_if<sr::String>();
    return a && b ? boost::iequals(a->str_, b->str_) : lhs.Hash() == rhs.Hash();
  });
  m.def("savepoint", [] { return 0; });
  m.def("check_savepoint", [] { return 0; });
  (*vm->globals_)["__debug"] = host().debug;
  if (host().configure_machine)
    host().configure_machine(*this);
}

lop::NativeWaitScope SiglusMachine::WaitScope(int owner) const {
  const std::weak_ptr<char> life = std::weak_ptr(lifetime);
  const bool resource_bound = owner == 1 || callback_owner == 1 ||
                              (owner == -1 && callback_owner == -1 &&
                               this != &host().GameplayMachine() &&
                               host().excall && host().excall->allocated);
  const std::weak_ptr<char> resources =
      resource_bound && host().excall ? std::weak_ptr(host().excall->lifetime)
                                      : std::weak_ptr<char>{};

  std::function<bool()> active = [this, life] {
    if (life.expired())
      return false;
    if (callback_owner >= 0)
      return callback_owner == (host().IsExcall() ? 1 : 0);
    return &host().ActiveMachine() == this;
  };
  std::function<bool()> alive = [life, resources, resource_bound] {
    return !life.expired() && (!resource_bound || !resources.expired());
  };
  return {std::move(active), std::move(alive)};
}

sr::Value SiglusMachine::RequestFrame() {
  auto* future = vm->gc_->Allocate<sr::Future>();
  future->promise = frame_requests->RequestFrame();
  return future;
}

sr::Value SiglusMachine::RequestCapture(CaptureRequest request) {
  return host().RequestCapture(*this, std::move(request));
}

}  // namespace libsiglus
