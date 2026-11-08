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

#pragma once

#include "vm/value.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace lop {
struct NativeWaitScope;
}
namespace serilang {
class VM;
class Promise;
struct Fiber;
}  // namespace serilang
namespace srbind {
template <class T>
class class_;
}

template <class... Ts>
class IntListFacade;
template <class... Ts>
class StrListFacade;
struct ActionReference;
struct CaptureRequest;
class Counter;
struct CounterList;
class EventListener;
class GraphicsObject;
struct SelState;
class TextWindow;

namespace libsiglus {
using SiglusIntListFacade = ::IntListFacade<serilang::Fiber&>;
using SiglusStrListFacade = ::StrListFacade<serilang::Fiber&>;

class FrameRequestManager;
namespace binding {
class Loader;
}
struct SiglusRuntime;

struct SiglusMachine {
  explicit SiglusMachine(SiglusRuntime& runtime, int callback_owner = -1);
  ~SiglusMachine();
  void Initialize();
  void Cancel();
  lop::NativeWaitScope WaitScope(int owner = -1) const;
  serilang::Value RequestFrame();
  serilang::Value RequestCapture(CaptureRequest request);
  std::unique_ptr<FrameRequestManager> frame_requests;
  std::shared_ptr<SelState> selection_state;
  std::shared_ptr<EventListener> selection_event_listener;
  std::vector<std::function<void()>> reset_local_memory;
  std::shared_ptr<char> lifetime = std::make_shared<char>();
  std::shared_ptr<serilang::Promise> entry_completion;
  std::shared_ptr<serilang::Promise> caller_completion;
  SiglusMachine* caller = nullptr;
  std::weak_ptr<char> caller_lifetime;
  std::shared_ptr<TextWindow> saved_window;
  int callback_owner = -1;

  inline SiglusRuntime& host() { return runtime_; }
  inline const SiglusRuntime& host() const { return runtime_; }
  std::unique_ptr<serilang::VM> vm;
  std::unique_ptr<binding::Loader> loader;
  std::shared_ptr<srbind::class_<SiglusIntListFacade>> ilist_cls;
  std::shared_ptr<srbind::class_<SiglusStrListFacade>> slist_cls;
  std::function<serilang::Value(std::shared_ptr<Counter>)> make_counter;
  std::function<serilang::Value(std::shared_ptr<CounterList>)>
      make_counter_list;
  std::function<serilang::Value(std::shared_ptr<GraphicsObject>, int)>
      make_owned_object;
  std::function<serilang::Value(ActionReference, int)> make_owned_frame_action;
  std::function<serilang::Value(std::shared_ptr<GraphicsObject>, int)>
      make_owned_frame_action_list;
  std::function<serilang::Value(std::shared_ptr<GraphicsObject>)> make_object;
  std::function<serilang::Value(ActionReference)> make_frame_action;
  std::function<serilang::Value(std::shared_ptr<GraphicsObject>)>
      make_frame_action_list;

 private:
  SiglusRuntime& runtime_;
};

}  // namespace libsiglus
