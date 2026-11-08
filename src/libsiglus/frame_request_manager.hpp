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

#pragma once

#include <functional>
#include <memory>
#include <vector>

class ScriptInput;
namespace serilang {
class Promise;
}  // namespace serilang

namespace libsiglus {

// Owns frame promises created by the injected factory. The factory must return
// a fresh, tracked pending promise; reset and destruction do not invoke it.
// All operations run on the driving thread.
class FrameRequestManager {
 public:
  using MakePendingPromise =
      std::function<std::shared_ptr<serilang::Promise>()>;

  explicit FrameRequestManager(MakePendingPromise make_pending_promise);
  ~FrameRequestManager();
  FrameRequestManager(const FrameRequestManager&) = delete;
  FrameRequestManager& operator=(const FrameRequestManager&) = delete;

  // Explicit script frames advance input; message frames preserve it.
  std::shared_ptr<serilang::Promise> RequestFrame();
  std::shared_ptr<serilang::Promise> RequestMessageFrame();

  // Activate only requests queued before this frame's updates begin.
  bool BeginFrameRequests();
  // Advance input before resolving any promise in the active batch.
  void CompleteFrameRequests(ScriptInput* script_input);
  // Cancel queued and active requests.
  void Reset();

 public:
  struct FrameRequest {
    std::shared_ptr<serilang::Promise> promise;
    bool advance_script_input;
  };
  struct SavedState {
    std::vector<FrameRequest> pending, active;
  };
  SavedState SaveState() const {
    return {pending_frame_requests_, active_frame_requests_};
  }
  void RestoreState(SavedState state) {
    pending_frame_requests_ = std::move(state.pending);
    active_frame_requests_ = std::move(state.active);
  }

 private:
  std::shared_ptr<serilang::Promise> QueueFrame(bool advance_script_input);

  MakePendingPromise make_pending_promise_;
  std::vector<FrameRequest> pending_frame_requests_, active_frame_requests_;
};

}  // namespace libsiglus
