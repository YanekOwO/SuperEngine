// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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
//
// -----------------------------------------------------------------------

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include "core/button_action_table.hpp"
#include "core/capture.hpp"
#include "core/counter.hpp"
#include "core/input.hpp"
#include "core/interaction_manager.hpp"
#include "core/mwnd_config.hpp"
#include "libsiglus/bindings/flow.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "m6/compiler_pipeline.hpp"
#include "m6/vm_factory.hpp"
#include "mock_graphics_backend.hpp"
#include "siglus/fake_scene.hpp"
#include "srbind/srbind.hpp"
#include "systems/event_system.hpp"
#include "systems/frame_presenter.hpp"
#include "systems/system.hpp"
#include "vm/function.hpp"
#include "vm/future.hpp"
#include "vm/promise.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace libsiglus::binding {
namespace sr = serilang;
using namespace std::chrono_literals;

namespace {

// Both fixtures compose the same script harness without inheriting fixtures.
class FrameHarness {
 public:
  SiglusMachine runtime;

  FrameHarness() {
    runtime.vm = std::make_unique<sr::VM>(m6::VMFactory::Create());
    Bind("frame");
  }

  void Bind(const std::string& name) {
    const auto* binding = SiglusBindingRegistry::Find(name);
    if (!binding)
      throw std::logic_error("Missing Siglus binding: " + name);
    (*binding)(runtime);
  }

  sr::Function* Compile(std::string source) {
    m6::CompilerPipeline compiler(runtime.vm->gc_);
    compiler.compile(m6::SourceBuffer::Create(std::move(source), "<frame>"));
    if (!compiler.Ok())
      throw std::runtime_error(compiler.FormatErrors());
    return runtime.vm->gc_->Allocate<sr::Function>(compiler.Get());
  }

  void Invoke(std::string source) {
    runtime.vm->InvokeUntilBlocked(*Compile(std::move(source)), {});
  }

  void Schedule(std::string source) {
    runtime.vm->ScheduleExecute(Compile(std::move(source)));
  }

  void Pump() { runtime.vm->Run(1ms); }

  sr::Value Global(const std::string& name) const {
    return runtime.vm->globals_->at(name);
  }

  std::shared_ptr<sr::Promise> Request() {
    auto future = runtime.RequestFrame();
    return sr::get_promise(future);
  }
};

class ScopedDummyDrivers {
 public:
  ScopedDummyDrivers()
      : audio_(Read(SDL_HINT_AUDIO_DRIVER)),
        video_(Read(SDL_HINT_VIDEO_DRIVER)) {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  }

  ~ScopedDummyDrivers() {
    Restore(SDL_HINT_VIDEO_DRIVER, video_);
    Restore(SDL_HINT_AUDIO_DRIVER, audio_);
  }

 private:
  static std::optional<std::string> Read(const char* name) {
    if (const auto* value = SDL_GetHint(name))
      return std::string(value);
    return std::nullopt;
  }

  static void Restore(const char* name,
                      const std::optional<std::string>& value) {
    if (value)
      SDL_SetHint(name, value->c_str());
    else
      SDL_ResetHint(name);
  }

  std::optional<std::string> audio_, video_;
};

}  // namespace

class SiglusFrameWaitTest : public ::testing::Test {
 protected:
  FrameHarness harness;
};

TEST_F(SiglusFrameWaitTest, AwaitSuspendsUntilAFrameCompletes) {
  // Calling frame suspends the continuation; completion resumes it with nil.
  harness.Invoke("result = await frame(); resumed = 1;");
  EXPECT_FALSE(harness.runtime.vm->globals_->contains("resumed"));

  ASSERT_TRUE(harness.runtime.BeginFrameRequests());
  EXPECT_FALSE(harness.runtime.vm->globals_->contains("resumed"));
  harness.runtime.CompleteFrameRequests();
  harness.Pump();

  EXPECT_EQ(harness.Global("resumed"), sr::Value(1));
  EXPECT_EQ(harness.Global("result"), sr::nil);
}

TEST_F(SiglusFrameWaitTest, RecompiledFrameElementSuspendsItsScene) {
  // A synthetic scene executes element six before returning 42; the real
  // parser and recompiler must suspend it without an explicit script await.
  auto& runtime = harness.runtime;
  runtime.archive = std::make_shared<Archive>(
      std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
  runtime.loader =
      std::make_unique<Loader>(*runtime.archive, *runtime.vm, false);
  harness.Bind("0_memory");
  harness.Bind("flow");
  FakeScene scene{.scene_name = "frames", .command_name = "advance"};
  scene.AppendElement({6});
  scene.AppendByte(ByteCode::Cmd);
  for (int value : {0, 0, 0, static_cast<int>(Type::None)})
    scene.AppendInt(value);
  scene.AppendByte(ByteCode::Push);
  scene.AppendInt(static_cast<int>(Type::Int));
  scene.AppendInt(42);
  scene.AppendByte(ByteCode::Return);
  scene.AppendInt(1);
  scene.AppendInt(static_cast<int>(Type::Int));
  scene.AddTo(*runtime.archive);
  (*runtime.vm->globals_)["entry"] =
      ResolveSceneCommand(*runtime.loader, "frames", "advance");

  harness.Invoke("result = entry();");
  EXPECT_FALSE(runtime.vm->globals_->contains("result"));
  ASSERT_TRUE(runtime.BeginFrameRequests());
  runtime.CompleteFrameRequests();
  harness.Pump();

  EXPECT_EQ(harness.Global("result"), sr::Value(42));
}

TEST_F(SiglusFrameWaitTest, ConcurrentRequestsCompleteAtTheSameBoundary) {
  // Two independent waiters share a system frame; both resume on its
  // completion.
  harness.Invoke("await frame(); first = 1;");
  harness.Invoke("await frame(); second = 1;");

  ASSERT_TRUE(harness.runtime.BeginFrameRequests());
  harness.runtime.CompleteFrameRequests();
  harness.Pump();

  EXPECT_EQ(harness.Global("first"), sr::Value(1));
  EXPECT_EQ(harness.Global("second"), sr::Value(1));
  EXPECT_FALSE(harness.runtime.BeginFrameRequests());
}

TEST_F(SiglusFrameWaitTest, SequentialCallsCrossSeparateBoundaries) {
  // One continuation calls frame twice; the second wait needs another frame.
  harness.Invoke("await frame(); first = 1; await frame(); second = 1;");
  ASSERT_TRUE(harness.runtime.BeginFrameRequests());
  harness.runtime.CompleteFrameRequests();
  harness.Pump();
  EXPECT_EQ(harness.Global("first"), sr::Value(1));
  EXPECT_FALSE(harness.runtime.vm->globals_->contains("second"));

  ASSERT_TRUE(harness.runtime.BeginFrameRequests());
  harness.runtime.CompleteFrameRequests();
  harness.Pump();

  EXPECT_EQ(harness.Global("second"), sr::Value(1));
}

TEST_F(SiglusFrameWaitTest, RequestsDuringUpdatesWaitForTheFollowingFrame) {
  // A request arrives after a frame begins; completing that frame cannot
  // resolve the request before a subsequent frame has advanced.
  EXPECT_FALSE(harness.runtime.BeginFrameRequests());
  auto promise = harness.Request();
  harness.runtime.CompleteFrameRequests();
  EXPECT_FALSE(promise->HasResult());

  ASSERT_TRUE(harness.runtime.BeginFrameRequests());
  harness.runtime.CompleteFrameRequests();

  EXPECT_EQ(promise->status, sr::Promise::Status::Resolved);
}

TEST_F(SiglusFrameWaitTest, CompletionCallbacksCanRequestAnotherFrame) {
  // Completing one batch queues another request through a callback; the new
  // request must survive and wait for the next boundary.
  auto first = harness.Request();
  std::shared_ptr<sr::Promise> next;
  first->AddWaker([&](const auto&) { next = harness.Request(); });
  ASSERT_TRUE(harness.runtime.BeginFrameRequests());

  harness.runtime.CompleteFrameRequests();

  ASSERT_NE(next, nullptr);
  EXPECT_FALSE(next->HasResult());
  ASSERT_TRUE(harness.runtime.BeginFrameRequests());
  harness.runtime.CompleteFrameRequests();
  EXPECT_EQ(next->status, sr::Promise::Status::Resolved);
}

TEST_F(SiglusFrameWaitTest, AnUnawaitedRequestKeepsTheRuntimeAlive) {
  // An eagerly queued request has no waiter; it still keeps the VM alive until
  // its system frame completes, then leaves no pending work.
  auto promise = harness.Request();
  EXPECT_TRUE(harness.runtime.vm->HasPendingWork());

  ASSERT_TRUE(harness.runtime.BeginFrameRequests());
  harness.runtime.CompleteFrameRequests();

  EXPECT_TRUE(promise->HasResult());
  EXPECT_FALSE(harness.runtime.vm->HasPendingWork());
}

TEST_F(SiglusFrameWaitTest, InvalidArgumentsDoNotQueueAFrame) {
  // An argument is supplied to the zero-argument binding; rejection must leave
  // the script able to catch the error without suppressing a future frame.
  harness.Invoke("caught = 0; try { frame(1); } catch (e) { caught = 1; }");
  harness.Pump();

  EXPECT_EQ(harness.Global("caught"), sr::Value(1));
  EXPECT_FALSE(harness.runtime.BeginFrameRequests());
}

TEST_F(SiglusFrameWaitTest, LocalResetCancelsQueuedAndActiveRequests) {
  // A local runtime reset occurs with both batches populated; all existing
  // requests must be rejected and the next frame must allow presentation.
  auto active = harness.Request();
  ASSERT_TRUE(harness.runtime.BeginFrameRequests());
  auto pending = harness.Request();

  for (auto& reset : harness.runtime.reset_local_memory)
    reset();

  EXPECT_EQ(active->status, sr::Promise::Status::Rejected);
  EXPECT_EQ(pending->status, sr::Promise::Status::Rejected);
  EXPECT_FALSE(harness.runtime.BeginFrameRequests());
}

TEST_F(SiglusFrameWaitTest, ResetRejectionReachesTheWaitingScript) {
  // A suspended script catches frame cancellation and must be able to resume
  // through its ordinary exception handler after the reset.
  harness.Invoke(
      "caught = 0; try { await frame(); } catch (e) { caught = 1; }");

  harness.runtime.ResetFrameRequests();
  harness.Pump();

  EXPECT_EQ(harness.Global("caught"), sr::Value(1));
}

TEST_F(SiglusFrameWaitTest, DestructionCancelsQueuedAndActiveRequests) {
  // Destroy a runtime with two outstanding batches; retained promises must be
  // rejected before their VM and native services are destroyed.
  auto other = std::make_unique<FrameHarness>();
  auto active = other->Request();
  ASSERT_TRUE(other->runtime.BeginFrameRequests());
  auto pending = other->Request();

  other.reset();

  EXPECT_EQ(active->status, sr::Promise::Status::Rejected);
  EXPECT_EQ(pending->status, sr::Promise::Status::Rejected);
}

TEST_F(SiglusFrameWaitTest, TimeStopDoesNotBlockFrameCompletion) {
  // Frozen game time leaves a running counter unchanged; the frame boundary
  // must still complete because it is independent of game-time timers.
  auto counter = harness.runtime.CreateCounter();
  counter->StartGame();
  harness.runtime.local_config.time_stop = true;
  auto promise = harness.Request();
  ASSERT_TRUE(harness.runtime.BeginFrameRequests());

  harness.runtime.Update(16ms, 16ms);
  harness.runtime.CompleteFrameRequests();

  EXPECT_EQ(counter->Get(), 0);
  EXPECT_EQ(promise->status, sr::Promise::Status::Resolved);
}

class SiglusFrameLoopTest : public ::testing::Test {
 protected:
  ScopedDummyDrivers drivers;
  FrameHarness harness;
  std::shared_ptr<::testing::NiceMock<MockGraphicsBackend>> backend =
      std::make_shared<::testing::NiceMock<MockGraphicsBackend>>();
  int updates = 0, presentations = 0;

  void SetUp() override {
    auto& runtime = harness.runtime;
    runtime.gameexe = std::make_shared<Gameexe>();
    runtime.stage = std::make_unique<Stage>(1);
    runtime.asset_scanner = std::make_shared<AssetScanner>();
    runtime.system = std::make_unique<System>(
        *runtime.gameexe, *runtime.stage, runtime.local_config,
        runtime.global_config, runtime.asset_scanner, MwndConfig{});
    runtime.input_event_listener = std::make_shared<InputListener>();
    runtime.system->event().AddListener(runtime.input_event_listener);
    runtime.interaction_manager = std::make_unique<InteractionManager>(
        *runtime.stage, *runtime.input_event_listener, ButtonActionTable{});
    ON_CALL(*backend, Resize(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Size(16, 16)));
    ON_CALL(*backend, RenderFrame(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(
            [this](const auto&, const auto&, const auto&) { ++presentations; });
    runtime.presenter =
        std::make_unique<FramePresenter>(backend, Size(16, 16), false);
    runtime.update_wipe_after_frame_actions = [this] {
      ++updates;
      if (updates > 8)
        harness.runtime.vm->RequestStop();
    };
  }
};

TEST_F(SiglusFrameLoopTest, ResumesAfterAnUpdateWithoutPresentation) {
  // The first VM slice requests a frame; its continuation must observe a
  // second update while presentation remains at the first frame's count.
  int observed_updates = 0, observed_presentations = 0;
  srbind::module_ globals(harness.runtime.vm->gc_.get(),
                          harness.runtime.vm->globals_.get());
  globals.def("observe", [&] {
    observed_updates = updates;
    observed_presentations = presentations;
  });
  harness.Schedule("await frame(); observe();");

  harness.runtime.Run();

  EXPECT_EQ(observed_updates, 2);
  EXPECT_EQ(observed_presentations, 1);
}

TEST_F(SiglusFrameLoopTest, NormalPresentationResumesAfterFrameCompletion) {
  // Finish a frame wait, then run an ordinary script; the later iteration must
  // present normally rather than retaining the suppression flag.
  harness.Schedule("await frame();");
  harness.runtime.Run();
  const int before = presentations;

  harness.Schedule("done = 1;");
  harness.runtime.Run();

  EXPECT_EQ(presentations, before + 1);
}

TEST_F(SiglusFrameLoopTest, FastForwardStillCrossesAFrameBoundary) {
  // Fast-forward is active before the script calls frame; the continuation
  // must still wait until the second system update.
  harness.runtime.system->set_force_fast_forward();
  int observed_updates = 0;
  srbind::module_ globals(harness.runtime.vm->gc_.get(),
                          harness.runtime.vm->globals_.get());
  globals.def("observe", [&] { observed_updates = updates; });
  harness.Schedule("await frame(); observe();");

  harness.runtime.Run();

  EXPECT_EQ(observed_updates, 2);
}

TEST_F(SiglusFrameLoopTest, RequestDuringUpdateWaitsForAnotherIteration) {
  // A native callback requests a frame during the first update; it cannot
  // resume until the following iteration has performed its own update.
  int observed_updates = 0;
  srbind::module_ globals(harness.runtime.vm->gc_.get(),
                          harness.runtime.vm->globals_.get());
  globals.def("observe", [&] { observed_updates = updates; });
  auto update = harness.runtime.update_wipe_after_frame_actions;
  harness.runtime.update_wipe_after_frame_actions = [&, update] {
    update();
    if (updates == 1)
      harness.Invoke("await frame(); observe();");
  };
  harness.Schedule("started = 1;");

  harness.runtime.Run();

  EXPECT_EQ(observed_updates, 2);
}

TEST_F(SiglusFrameLoopTest, CapturesCompleteWithoutWindowPresentation) {
  // A capture and frame wait are queued together; the offscreen capture must
  // complete even though the system frame does not draw or present a window.
  auto& runtime = harness.runtime;
  auto surface = std::make_shared<SDLSurface>(Size(1, 1));
  int captures = 0;
  runtime.captures = std::make_shared<CaptureManager>(
      CaptureConfig{.screen_size = Size(1, 1)}, std::filesystem::path{},
      [&](const CaptureRequest&) {
        ++captures;
        return surface;
      },
      CaptureManager::Load{});
  auto capture = runtime.RequestCapture({.kind = CaptureKind::Object});
  auto promise = sr::get_promise(capture);
  harness.Invoke("await frame(); done = 1;");

  runtime.Run();

  EXPECT_EQ(captures, 1);
  EXPECT_EQ(promise->status, sr::Promise::Status::Resolved);
  EXPECT_EQ(presentations, 0);
}

TEST_F(SiglusFrameLoopTest, StopCancelsPendingFrameRequests) {
  // Stop the VM before its queued frame begins; Run must cancel that request
  // instead of leaving an externally retained promise permanently pending.
  auto promise = harness.Request();
  harness.runtime.vm->RequestStop();

  harness.runtime.Run();

  EXPECT_EQ(promise->status, sr::Promise::Status::Rejected);
}

TEST_F(SiglusFrameLoopTest, QuitDuringUpdateCancelsActiveFrameRequests) {
  // Quit after activating a frame batch; cancellation must reach that active
  // request before any script continuation or presentation can run.
  auto promise = harness.Request();
  harness.runtime.update_wipe_after_frame_actions = [&] {
    harness.runtime.system->RequestQuit();
  };

  harness.runtime.Run();

  EXPECT_EQ(promise->status, sr::Promise::Status::Rejected);
  EXPECT_EQ(presentations, 0);
}

}  // namespace libsiglus::binding
