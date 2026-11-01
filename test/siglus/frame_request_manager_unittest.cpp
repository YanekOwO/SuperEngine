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

#include <gtest/gtest.h>

#include "core/input.hpp"
#include "libsiglus/frame_request_manager.hpp"
#include "vm/gc.hpp"
#include "vm/promise.hpp"
#include "vm/vm.hpp"

#include <memory>
#include <stdexcept>

namespace libsiglus {
namespace sr = serilang;

class FrameRequestManagerTest : public ::testing::Test {
 protected:
  sr::VM vm{std::make_shared<sr::GarbageCollector>()};
  FrameRequestManager frames{[this] {
    auto promise = std::make_shared<sr::Promise>();
    vm.TrackPendingPromise(promise);
    return promise;
  }};
};

TEST_F(FrameRequestManagerTest, RequestsUseTheInjectedPromiseFactory) {
  // Supply distinct promises without a VM; each kind of frame request must
  // return the promise produced by its own factory invocation.
  auto first = std::make_shared<sr::Promise>();
  auto second = std::make_shared<sr::Promise>();
  int calls = 0;
  FrameRequestManager manager([&] { return ++calls == 1 ? first : second; });
  EXPECT_EQ(manager.RequestFrame(), first);
  EXPECT_EQ(manager.RequestMessageFrame(), second);
  EXPECT_EQ(calls, 2);
}

TEST_F(FrameRequestManagerTest, EmptyPromiseFactoryIsRejected) {
  // An empty factory cannot supply frame promises; construction reports the
  // invalid dependency instead of deferring failure until a request.
  EXPECT_THROW(FrameRequestManager(FrameRequestManager::MakePendingPromise{}),
               std::invalid_argument);
}

TEST_F(FrameRequestManagerTest, NullFactoryResultDoesNotQueueAFrame) {
  // A factory returns no promise; reject the request without leaving an
  // invalid entry for frame completion or cancellation to dereference.
  FrameRequestManager manager([] { return std::shared_ptr<sr::Promise>{}; });
  EXPECT_THROW(manager.RequestFrame(), std::logic_error);
  EXPECT_FALSE(manager.BeginFrameRequests());
}

TEST_F(FrameRequestManagerTest, NativeFrameRequestsKeepTheVmAlive) {
  // An explicit frame has no script awaiter; tracking its promise must keep
  // the VM pending until the activated frame completes.
  auto promise = frames.RequestFrame();
  EXPECT_TRUE(vm.HasPendingWork());
  ASSERT_TRUE(frames.BeginFrameRequests());
  frames.CompleteFrameRequests(nullptr);
  EXPECT_TRUE(promise->HasResult());
  EXPECT_FALSE(vm.HasPendingWork());
}

TEST_F(FrameRequestManagerTest, NativeMessageFramesKeepTheVmAlive) {
  // A native message wait has no VM Future; its promise must still keep the
  // VM pending until an engine frame completes.
  auto promise = frames.RequestMessageFrame();
  EXPECT_TRUE(vm.HasPendingWork());
  ASSERT_TRUE(frames.BeginFrameRequests());
  frames.CompleteFrameRequests(nullptr);
  EXPECT_TRUE(promise->HasResult());
  EXPECT_FALSE(vm.HasPendingWork());
}

TEST_F(FrameRequestManagerTest, RequestsAfterActivationWaitForTheNextFrame) {
  // Queue a message after the current frame activates its requests; only
  // the following frame may resolve that newly queued promise.
  EXPECT_FALSE(frames.BeginFrameRequests());
  auto promise = frames.RequestMessageFrame();
  frames.CompleteFrameRequests(nullptr);
  EXPECT_FALSE(promise->HasResult());
  ASSERT_TRUE(frames.BeginFrameRequests());
  frames.CompleteFrameRequests(nullptr);
  EXPECT_TRUE(promise->HasResult());
}

TEST_F(FrameRequestManagerTest, ActiveRequestsMustCompleteBeforeAnotherFrame) {
  // Activate an explicit request twice without completion; the second begin
  // must reject the invalid frame sequence instead of losing the batch.
  auto promise = frames.RequestFrame();
  ASSERT_TRUE(frames.BeginFrameRequests());
  EXPECT_THROW(frames.BeginFrameRequests(), std::logic_error);
  EXPECT_FALSE(promise->HasResult());
}

TEST_F(FrameRequestManagerTest, CompletionCallbacksQueueForTheNextFrame) {
  // A completion callback requests another frame; detaching the active batch
  // must leave that new promise pending until the next activation.
  auto first = frames.RequestFrame();
  std::shared_ptr<sr::Promise> next;
  first->AddWaker([&](const auto&) { next = frames.RequestMessageFrame(); });
  ASSERT_TRUE(frames.BeginFrameRequests());
  frames.CompleteFrameRequests(nullptr);
  ASSERT_NE(next, nullptr);
  EXPECT_FALSE(next->HasResult());
  ASSERT_TRUE(frames.BeginFrameRequests());
  frames.CompleteFrameRequests(nullptr);
  EXPECT_TRUE(next->HasResult());
}

TEST_F(FrameRequestManagerTest, MixedBatchAdvancesInputBeforeAllCompletions) {
  // Queue a release and another press behind the visible press; a mixed
  // batch must advance input once so every completion observes the release.
  ScriptInput input;
  input.OnEvent(std::make_shared<Event>(MouseDown{MouseButton::LEFT}));
  ASSERT_TRUE(input.Get(ScriptInput::Source::Left).down);
  input.OnEvent(std::make_shared<Event>(MouseUp{MouseButton::LEFT}));
  input.OnEvent(std::make_shared<Event>(MouseDown{MouseButton::LEFT}));
  auto message = frames.RequestMessageFrame();
  auto explicit_frame = frames.RequestFrame();
  int completions = 0;
  auto observe_input = [&](const auto&) {
    EXPECT_FALSE(input.Get(ScriptInput::Source::Left).down);
    ++completions;
  };
  message->AddWaker(observe_input);
  explicit_frame->AddWaker(observe_input);
  ASSERT_TRUE(frames.BeginFrameRequests());
  frames.CompleteFrameRequests(&input);
  EXPECT_EQ(completions, 2);
}

TEST_F(FrameRequestManagerTest, ResetCancelsActiveAndQueuedRequests) {
  // Reset with both explicit and message requests in the two batches;
  // cancellation must reject all of them and leave no outstanding work.
  auto active = frames.RequestFrame();
  ASSERT_TRUE(frames.BeginFrameRequests());
  auto pending = frames.RequestMessageFrame();
  frames.Reset();
  EXPECT_EQ(active->status, sr::Promise::Status::Rejected);
  EXPECT_EQ(pending->status, sr::Promise::Status::Rejected);
  EXPECT_FALSE(frames.BeginFrameRequests());
  EXPECT_FALSE(vm.HasPendingWork());
}

TEST_F(FrameRequestManagerTest, ResetCallbacksCanQueueANewFrame) {
  // Cancellation queues a replacement frame; reset must detach both old
  // batches before callbacks so the replacement survives cancellation.
  auto cancelled = frames.RequestMessageFrame();
  std::shared_ptr<sr::Promise> replacement;
  cancelled->AddWaker(
      [&](const auto&) { replacement = frames.RequestFrame(); });
  frames.Reset();
  ASSERT_NE(replacement, nullptr);
  EXPECT_FALSE(replacement->HasResult());
  ASSERT_TRUE(frames.BeginFrameRequests());
  frames.CompleteFrameRequests(nullptr);
  EXPECT_TRUE(replacement->HasResult());
}

TEST_F(FrameRequestManagerTest, DestructionCancelsActiveAndQueuedRequests) {
  // Destroy a standalone manager while its VM is alive; retained promises
  // from both request batches must receive cancellation.
  auto owner = std::make_unique<FrameRequestManager>([this] {
    auto promise = std::make_shared<sr::Promise>();
    vm.TrackPendingPromise(promise);
    return promise;
  });
  auto active = owner->RequestFrame();
  ASSERT_TRUE(owner->BeginFrameRequests());
  auto pending = owner->RequestMessageFrame();
  owner.reset();
  EXPECT_EQ(active->status, sr::Promise::Status::Rejected);
  EXPECT_EQ(pending->status, sr::Promise::Status::Rejected);
}

TEST_F(FrameRequestManagerTest, DestructionDoesNotAccessTheVm) {
  // Request a native frame while its VM is alive, then destroy the VM first;
  // destroying the manager must still cancel without accessing that VM.
  auto producer_vm =
      std::make_unique<sr::VM>(std::make_shared<sr::GarbageCollector>());
  auto owner = std::make_unique<FrameRequestManager>([vm = producer_vm.get()] {
    auto promise = std::make_shared<sr::Promise>();
    vm->TrackPendingPromise(promise);
    return promise;
  });
  auto promise = owner->RequestMessageFrame();
  producer_vm.reset();
  owner.reset();
  EXPECT_EQ(promise->status, sr::Promise::Status::Rejected);
}

}  // namespace libsiglus
