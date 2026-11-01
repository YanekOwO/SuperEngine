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

#include "libsiglus/frame_request_manager.hpp"

#include "core/input.hpp"
#include "vm/promise.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>

namespace libsiglus {
namespace sr = serilang;

FrameRequestManager::FrameRequestManager(
    MakePendingPromise make_pending_promise)
    : make_pending_promise_(std::move(make_pending_promise)) {
  if (!make_pending_promise_)
    throw std::invalid_argument("Frame request promise factory is empty");
}

FrameRequestManager::~FrameRequestManager() { Reset(); }

std::shared_ptr<sr::Promise> FrameRequestManager::RequestFrame() {
  return QueueFrame(true);
}

std::shared_ptr<sr::Promise> FrameRequestManager::RequestMessageFrame() {
  return QueueFrame(false);
}

std::shared_ptr<sr::Promise> FrameRequestManager::QueueFrame(
    bool advance_script_input) {
  std::shared_ptr<sr::Promise> promise = make_pending_promise_();
  if (!promise)
    throw std::logic_error("Frame request promise factory returned null");
  pending_frame_requests_.push_back({promise, advance_script_input});
  return promise;
}

bool FrameRequestManager::BeginFrameRequests() {
  if (!active_frame_requests_.empty())
    throw std::logic_error("Previous frame requests have not completed");
  active_frame_requests_.swap(pending_frame_requests_);
  return !active_frame_requests_.empty();
}

void FrameRequestManager::CompleteFrameRequests(ScriptInput* script_input) {
  auto active = std::exchange(active_frame_requests_, {});
  if (script_input &&
      std::ranges::any_of(active, &FrameRequest::advance_script_input))
    script_input->AdvanceFrame();
  for (auto& request : active)
    request.promise->Resolve(sr::nil);
}

void FrameRequestManager::Reset() {
  auto pending = std::exchange(pending_frame_requests_, {});
  auto active = std::exchange(active_frame_requests_, {});
  for (auto& request : active)
    request.promise->Reject("Frame wait cancelled by runtime reset");
  for (auto& request : pending)
    request.promise->Reject("Frame wait cancelled by runtime reset");
}

}  // namespace libsiglus
