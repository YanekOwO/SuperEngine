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
#include "mock_clock.hpp"
#include "mock_graphics_backend.hpp"
#include "poller_helpers.hpp"
#include "siglus/fake_scene.hpp"
#include "srbind/srbind.hpp"
#include "systems/event_system.hpp"
#include "systems/frame_presenter.hpp"
#include "systems/system.hpp"
#include "vm/function.hpp"
#include "vm/future.hpp"
#include "vm/promise.hpp"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace libsiglus::binding {
namespace sr = serilang;
using namespace std::chrono_literals;

// Both fixtures compose the same script harness without inheriting fixtures.
class FrameHarness {
 public:
  SiglusMachine runtime;

  FrameHarness() {
    runtime.vm = std::make_unique<sr::VM>(m6::VMFactory::Create());
    runtime.frame_requests =
        std::make_unique<FrameRequestManager>([vm = runtime.vm.get()] {
          auto promise = std::make_shared<sr::Promise>();
          vm->TrackPendingPromise(promise);
          return promise;
        });
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

  void Pump() { runtime.vm->RunReadyUntilBlocked(); }

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

class SiglusFrameWaitTest : public ::testing::Test {
 protected:
  FrameHarness harness;
};

TEST_F(SiglusFrameWaitTest, AwaitSuspendsUntilAFrameCompletes) {
  // Calling frame suspends the continuation; completion resumes it with nil.
  harness.Invoke("result = await frame(); resumed = 1;");
  EXPECT_FALSE(harness.runtime.vm->globals_->contains("resumed"));

  ASSERT_TRUE(harness.runtime.frame_requests->BeginFrameRequests());
  EXPECT_FALSE(harness.runtime.vm->globals_->contains("resumed"));
  harness.runtime.frame_requests->CompleteFrameRequests(
      harness.runtime.script_input.get());
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
  ASSERT_TRUE(runtime.frame_requests->BeginFrameRequests());
  runtime.frame_requests->CompleteFrameRequests(runtime.script_input.get());
  harness.Pump();

  EXPECT_EQ(harness.Global("result"), sr::Value(42));
}

TEST_F(SiglusFrameWaitTest, RecompiledDispElementSuspendsItsScene) {
  // A synthetic scene executes DISP before returning 42; the real parser
  // and recompiler must suspend it without an explicit script await.
  auto& runtime = harness.runtime;
  runtime.archive = std::make_shared<Archive>(
      std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
  runtime.loader =
      std::make_unique<Loader>(*runtime.archive, *runtime.vm, false);
  harness.Bind("0_memory");
  harness.Bind("flow");
  FakeScene scene{.scene_name = "display", .command_name = "advance"};
  scene.AppendElement({96});
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
      ResolveSceneCommand(*runtime.loader, "display", "advance");

  harness.Invoke("result = entry();");
  EXPECT_FALSE(runtime.vm->globals_->contains("result"));
  ASSERT_TRUE(runtime.frame_requests->BeginFrameRequests());
  runtime.frame_requests->CompleteFrameRequests(runtime.script_input.get());
  harness.Pump();

  EXPECT_EQ(harness.Global("result"), sr::Value(42));
}

TEST_F(SiglusFrameWaitTest, ConcurrentRequestsCompleteAtTheSameBoundary) {
  // Two independent waiters share a system frame; both resume on its
  // completion.
  harness.Invoke("await frame(); first = 1;");
  harness.Invoke("await frame(); second = 1;");

  ASSERT_TRUE(harness.runtime.frame_requests->BeginFrameRequests());
  harness.runtime.frame_requests->CompleteFrameRequests(
      harness.runtime.script_input.get());
  harness.Pump();

  EXPECT_EQ(harness.Global("first"), sr::Value(1));
  EXPECT_EQ(harness.Global("second"), sr::Value(1));
  EXPECT_FALSE(harness.runtime.frame_requests->BeginFrameRequests());
}

TEST_F(SiglusFrameWaitTest, SequentialCallsCrossSeparateBoundaries) {
  // One continuation calls frame twice; the second wait needs another frame.
  harness.Invoke("await frame(); first = 1; await frame(); second = 1;");
  ASSERT_TRUE(harness.runtime.frame_requests->BeginFrameRequests());
  harness.runtime.frame_requests->CompleteFrameRequests(
      harness.runtime.script_input.get());
  harness.Pump();
  EXPECT_EQ(harness.Global("first"), sr::Value(1));
  EXPECT_FALSE(harness.runtime.vm->globals_->contains("second"));

  ASSERT_TRUE(harness.runtime.frame_requests->BeginFrameRequests());
  harness.runtime.frame_requests->CompleteFrameRequests(
      harness.runtime.script_input.get());
  harness.Pump();

  EXPECT_EQ(harness.Global("second"), sr::Value(1));
}

TEST_F(SiglusFrameWaitTest, RequestsDuringUpdatesWaitForTheFollowingFrame) {
  // A request arrives after a frame begins; completing that frame cannot
  // resolve the request before a subsequent frame has advanced.
  EXPECT_FALSE(harness.runtime.frame_requests->BeginFrameRequests());
  auto promise = harness.Request();
  harness.runtime.frame_requests->CompleteFrameRequests(
      harness.runtime.script_input.get());
  EXPECT_FALSE(promise->HasResult());

  ASSERT_TRUE(harness.runtime.frame_requests->BeginFrameRequests());
  harness.runtime.frame_requests->CompleteFrameRequests(
      harness.runtime.script_input.get());

  EXPECT_EQ(promise->status, sr::Promise::Status::Resolved);
}

TEST_F(SiglusFrameWaitTest, CompletionCallbacksCanRequestAnotherFrame) {
  // Completing one batch queues another request through a callback; the new
  // request must survive and wait for the next boundary.
  auto first = harness.Request();
  std::shared_ptr<sr::Promise> next;
  first->AddWaker([&](const auto&) { next = harness.Request(); });
  ASSERT_TRUE(harness.runtime.frame_requests->BeginFrameRequests());

  harness.runtime.frame_requests->CompleteFrameRequests(
      harness.runtime.script_input.get());

  ASSERT_NE(next, nullptr);
  EXPECT_FALSE(next->HasResult());
  ASSERT_TRUE(harness.runtime.frame_requests->BeginFrameRequests());
  harness.runtime.frame_requests->CompleteFrameRequests(
      harness.runtime.script_input.get());
  EXPECT_EQ(next->status, sr::Promise::Status::Resolved);
}

TEST_F(SiglusFrameWaitTest, AnUnawaitedRequestKeepsTheRuntimeAlive) {
  // An eagerly queued request has no waiter; it still keeps the VM alive until
  // its system frame completes, then leaves no pending work.
  auto promise = harness.Request();
  EXPECT_TRUE(harness.runtime.vm->HasPendingWork());

  ASSERT_TRUE(harness.runtime.frame_requests->BeginFrameRequests());
  harness.runtime.frame_requests->CompleteFrameRequests(
      harness.runtime.script_input.get());

  EXPECT_TRUE(promise->HasResult());
  EXPECT_FALSE(harness.runtime.vm->HasPendingWork());
}

TEST_F(SiglusFrameWaitTest, InvalidArgumentsDoNotQueueAFrame) {
  // An argument is supplied to the zero-argument binding; rejection must leave
  // the script able to catch the error without suppressing a future frame.
  harness.Invoke("caught = 0; try { frame(1); } catch (e) { caught = 1; }");
  harness.Pump();

  EXPECT_EQ(harness.Global("caught"), sr::Value(1));
  EXPECT_FALSE(harness.runtime.frame_requests->BeginFrameRequests());
}

TEST_F(SiglusFrameWaitTest, LocalResetCancelsQueuedAndActiveRequests) {
  // A local runtime reset occurs with both batches populated; all existing
  // requests must be rejected and the next frame must allow presentation.
  auto active = harness.Request();
  ASSERT_TRUE(harness.runtime.frame_requests->BeginFrameRequests());
  auto pending = harness.Request();

  for (auto& reset : harness.runtime.reset_local_memory)
    reset();

  EXPECT_EQ(active->status, sr::Promise::Status::Rejected);
  EXPECT_EQ(pending->status, sr::Promise::Status::Rejected);
  EXPECT_FALSE(harness.runtime.frame_requests->BeginFrameRequests());
}

TEST_F(SiglusFrameWaitTest, ResetRejectionReachesTheWaitingScript) {
  // A suspended script catches frame cancellation and must be able to resume
  // through its ordinary exception handler after the reset.
  harness.Invoke(
      "caught = 0; try { await frame(); } catch (e) { caught = 1; }");

  harness.runtime.frame_requests->Reset();
  harness.Pump();

  EXPECT_EQ(harness.Global("caught"), sr::Value(1));
}

TEST_F(SiglusFrameWaitTest, DestructionCancelsQueuedAndActiveRequests) {
  // Destroy a runtime with two outstanding batches; retained promises must be
  // rejected before their VM and native services are destroyed.
  auto other = std::make_unique<FrameHarness>();
  auto active = other->Request();
  ASSERT_TRUE(other->runtime.frame_requests->BeginFrameRequests());
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
  ASSERT_TRUE(harness.runtime.frame_requests->BeginFrameRequests());

  harness.runtime.Update(16ms, 16ms);
  harness.runtime.frame_requests->CompleteFrameRequests(
      harness.runtime.script_input.get());

  EXPECT_EQ(counter->Get(), 0);
  EXPECT_EQ(promise->status, sr::Promise::Status::Resolved);
}

TEST_F(SiglusFrameWaitTest, MessageFramePreservesTheScriptInputSnapshot) {
  // Queue a physical release behind a published press; a message-pacing
  // boundary must retain the pressed script snapshot until explicit advance.
  auto& runtime = harness.runtime;
  runtime.script_input = std::make_shared<ScriptInput>();
  runtime.script_input->OnEvent(
      std::make_shared<Event>(MouseDown{MouseButton::LEFT}));
  ASSERT_TRUE(runtime.script_input->Get(ScriptInput::Source::Left).down);
  runtime.script_input->OnEvent(
      std::make_shared<Event>(MouseUp{MouseButton::LEFT}));
  auto promise = runtime.frame_requests->RequestMessageFrame();
  ASSERT_TRUE(runtime.frame_requests->BeginFrameRequests());
  runtime.frame_requests->CompleteFrameRequests(runtime.script_input.get());
  EXPECT_TRUE(promise->HasResult());
  EXPECT_TRUE(runtime.script_input->Get(ScriptInput::Source::Left).down);
}

TEST_F(SiglusFrameWaitTest, ExplicitFrameStillAdvancesAMixedBatch) {
  // Share a boundary between an explicit frame and message pacing; the
  // explicit request must retain its normal script-input advance.
  auto& runtime = harness.runtime;
  runtime.script_input = std::make_shared<ScriptInput>();
  runtime.script_input->OnEvent(
      std::make_shared<Event>(MouseDown{MouseButton::LEFT}));
  ASSERT_TRUE(runtime.script_input->Get(ScriptInput::Source::Left).down);
  runtime.script_input->OnEvent(
      std::make_shared<Event>(MouseUp{MouseButton::LEFT}));
  auto message = runtime.frame_requests->RequestMessageFrame();
  auto explicit_frame = harness.Request();
  ASSERT_TRUE(runtime.frame_requests->BeginFrameRequests());
  runtime.frame_requests->CompleteFrameRequests(runtime.script_input.get());
  EXPECT_FALSE(runtime.script_input->Get(ScriptInput::Source::Left).down);
  EXPECT_TRUE(message->HasResult());
  EXPECT_TRUE(explicit_frame->HasResult());
}

TEST_F(SiglusFrameWaitTest, DestructionCancelsMessageFrames) {
  // A native caller retains a frame promise past runtime destruction; teardown
  // must reject it before the VM and other runtime services disappear.
  auto other = std::make_unique<FrameHarness>();
  auto promise = other->runtime.frame_requests->RequestMessageFrame();
  other.reset();
  EXPECT_EQ(promise->status, sr::Promise::Status::Rejected);
}

class SiglusFrameLoopTest : public ::testing::Test {
 protected:
  ScopedDummyDrivers drivers;
  FrameHarness harness;
  std::shared_ptr<::testing::NiceMock<MockGraphicsBackend>> backend =
      std::make_shared<::testing::NiceMock<MockGraphicsBackend>>();
  int updates = 0, presentations = 0;

  template <class T>
  void Send(T value) {
    auto event = std::make_shared<Event>(value);
    harness.runtime.input_event_listener->OnEvent(event);
    harness.runtime.script_input->OnEvent(event);
  }

  void InitMenu() {
    // Model the shared capture-on-press, confirm-on-release menu protocol.
    // The hit test uses the position from the same script snapshot.
    harness.Invoke(R"(
      captured = -1;
      confirmed = 0;
      fn menu_step() {
        global captured, confirmed;
        previous = captured;
        hovered = -1;
        if (mouse.pos_x() < 20) hovered = 0;
        if (input.decide.is_up()) captured = -1;
        if (input.decide.is_down() && previous == -1) captured = hovered;
        if (input.decide.on_down_up()) {
          if (previous == hovered && hovered != -1) confirmed = confirmed + 1;
          captured = -1;
          input.clear();
        }
        input.next();
      }
    )");
  }

  void MenuStep() { harness.Invoke("menu_step();"); }

  MockClock& UseMockClock() {
    auto owner = std::make_unique<MockClock>();
    MockClock& clock = *owner;
    harness.runtime.vm->scheduler_ = sr::Scheduler(
        std::make_unique<sr::AdvancingPoller>(clock), std::move(owner));
    return clock;
  }

  void SetUp() override {
    auto& runtime = harness.runtime;
    runtime.gameexe = std::make_shared<Gameexe>();
    runtime.stage = std::make_unique<Stage>(1);
    runtime.asset_scanner = std::make_shared<AssetScanner>();
    runtime.system = std::make_unique<System>(
        *runtime.gameexe, *runtime.stage, runtime.local_config,
        runtime.global_config, runtime.asset_scanner, MwndConfig{});
    harness.Bind("0_input");
    runtime.vm->SetHostEventPump([&runtime] { runtime.PumpHostEvents(); });
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

TEST_F(SiglusFrameLoopTest, DispPresentsSceneChangesBeforeResuming) {
  // Show a hidden object before DISP; presentation must see it before the
  // continuation hides it again.
  auto object = harness.runtime.stage->objects.Get(kLayerFg, 0);
  std::vector<int> displayed;
  ON_CALL(*backend, RenderFrame(::testing::_, ::testing::_, ::testing::_))
      .WillByDefault([&](const auto&, const auto&, const auto&) {
        displayed.push_back(object->Param().visible());
      });
  srbind::module_ globals(harness.runtime.vm->gc_.get(),
                          harness.runtime.vm->globals_.get());
  globals.def("show", [&] { object->Param().SetVisible(1); });
  globals.def("hide", [&] { object->Param().SetVisible(0); });
  harness.Schedule("show(); await disp(); hide();");

  harness.runtime.Run();

  EXPECT_EQ(displayed, (std::vector<int>{0, 1}));
  EXPECT_EQ(object->Param().visible(), 0);
}

TEST_F(SiglusFrameLoopTest, ConsecutiveDispCallsCrossFramesDuringFastForward) {
  // Fast-forward runs two consecutive DISP calls; each continuation must
  // observe a separate system update.
  harness.runtime.system->set_force_fast_forward();
  std::vector<int> observed_updates;
  srbind::module_ globals(harness.runtime.vm->gc_.get(),
                          harness.runtime.vm->globals_.get());
  globals.def("observe", [&] { observed_updates.push_back(updates); });
  harness.Schedule("await disp(); observe(); await disp(); observe();");

  harness.runtime.Run();

  EXPECT_EQ(observed_updates, (std::vector<int>{2, 3}));
}

TEST_F(SiglusFrameLoopTest, ResumesAfterAnUpdateAndPresentation) {
  // The first VM slice requests a frame; its continuation must observe a
  // second update and presentation rather than starving display updates.
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
  EXPECT_EQ(observed_presentations, 2);
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

TEST_F(SiglusFrameLoopTest, DoesNotDisplayTemporaryMenuButtonPattern) {
  // A menu spends time between assigning normal and disabled cuts; only the
  // completed logical frame may reach the display.
  auto& clock = UseMockClock();
  auto object = harness.runtime.stage->objects.Get(kLayerFg, 0);
  object->Param().SetPattNo(4);
  std::vector<int> displayed;
  ON_CALL(*backend, RenderFrame(::testing::_, ::testing::_, ::testing::_))
      .WillByDefault([&](const auto&, const auto&, const auto&) {
        displayed.push_back(object->Param().pattern_number);
      });
  srbind::module_ globals(harness.runtime.vm->gc_.get(),
                          harness.runtime.vm->globals_.get());
  globals.def("normal", [&] {
    object->Param().SetPattNo(0);
    clock.AdvanceTime(100ms);
  });
  globals.def("disabled", [&] { object->Param().SetPattNo(4); });
  harness.Schedule(R"(
    await frame();
    normal();
    for (i = 0; i < 1000; i += 1) padding = i;
    disabled();
    await frame();
    done = 1;
  )");

  harness.runtime.Run();

  EXPECT_EQ(harness.Global("done"), sr::Value(1));
  ASSERT_GE(displayed.size(), 3u);
  EXPECT_THAT(displayed, ::testing::Each(4));
}

TEST_F(SiglusFrameLoopTest, HostPumpsPreserveTheCurrentLogicalFrame) {
  // A script computes long enough for three host pumps; neither simulation nor
  // presentation may advance before its explicit frame wait.
  auto& clock = UseMockClock();
  std::vector<int> observed_updates, observed_presentations;
  harness.runtime.vm->SetHostEventPump([&] {
    observed_updates.push_back(updates);
    observed_presentations.push_back(presentations);
    harness.runtime.PumpHostEvents();
  });
  srbind::module_ globals(harness.runtime.vm->gc_.get(),
                          harness.runtime.vm->globals_.get());
  globals.def("compute", [&] { clock.AdvanceTime(100ms); });
  harness.Schedule(R"(
    await frame();
    for (slice = 0; slice < 3; slice += 1) {
      compute();
      for (i = 0; i < 1000; i += 1) padding = i;
    }
    await frame();
  )");

  harness.runtime.Run();

  EXPECT_THAT(observed_updates, ::testing::ElementsAre(2, 2, 2));
  EXPECT_THAT(observed_presentations, ::testing::Each(2));
}

TEST_F(SiglusFrameLoopTest, ElapsedComputationTimeAdvancesAtTheNextFrame) {
  // Script computation advances the clock by 100 ms; counters must receive
  // that elapsed time once, at the next logical frame rather than a host pump.
  auto& clock = UseMockClock();
  auto counter = harness.runtime.CreateCounter();
  counter->StartReal();
  int during_computation = -1;
  srbind::module_ globals(harness.runtime.vm->gc_.get(),
                          harness.runtime.vm->globals_.get());
  globals.def("compute", [&] { clock.AdvanceTime(100ms); });
  globals.def("observe", [&] { during_computation = counter->Get(); });
  harness.Schedule(R"(
    await frame();
    compute();
    for (i = 0; i < 1000; i += 1) padding = i;
    observe();
    await frame();
  )");

  harness.runtime.Run();

  EXPECT_EQ(during_computation, 0);
  EXPECT_EQ(counter->Get(), 100);
  EXPECT_EQ(updates, 3);
}

TEST_F(SiglusFrameLoopTest, HostPumpQuitCancelsWithoutAdvancingTheFrame) {
  // An endless script queues a window quit and advances the watchdog clock;
  // cancellation must occur without another simulation update or presentation.
  auto& clock = UseMockClock();
  std::shared_ptr<sr::Promise> pending;
  srbind::module_ globals(harness.runtime.vm->gc_.get(),
                          harness.runtime.vm->globals_.get());
  globals.def("queue_quit", [&] {
    pending = harness.Request();
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
  });
  globals.def("compute", [&] { clock.AdvanceTime(20ms); });
  harness.Schedule("queue_quit(); while (true) compute();");

  harness.runtime.Run();

  EXPECT_TRUE(harness.runtime.vm->IsStopRequested());
  EXPECT_EQ(updates, 1);
  EXPECT_EQ(presentations, 1);
  ASSERT_NE(pending, nullptr);
  EXPECT_EQ(pending->status, sr::Promise::Status::Rejected);
}

TEST_F(SiglusFrameLoopTest, PresentationResumesDuringAnAsynchronousWait) {
  // After prolonged computation, the script restores its image and awaits a
  // non-frame future; suspension must allow the restored image to be shown.
  auto& clock = UseMockClock();
  auto object = harness.runtime.stage->objects.Get(kLayerFg, 0);
  object->Param().SetPattNo(4);
  auto* future = harness.runtime.vm->gc_->Allocate<sr::Future>();
  auto promise = future->promise;
  (*harness.runtime.vm->globals_)["pause"] = future;
  bool restored = false, displayed_while_waiting = false;
  ON_CALL(*backend, RenderFrame(::testing::_, ::testing::_, ::testing::_))
      .WillByDefault([&](const auto&, const auto&, const auto&) {
        if (restored && !promise->HasResult()) {
          displayed_while_waiting = object->Param().pattern_number == 4;
          promise->Resolve(sr::nil);
        }
      });
  srbind::module_ globals(harness.runtime.vm->gc_.get(),
                          harness.runtime.vm->globals_.get());
  globals.def("normal", [&] {
    object->Param().SetPattNo(0);
    clock.AdvanceTime(100ms);
  });
  globals.def("disabled", [&] {
    object->Param().SetPattNo(4);
    restored = true;
  });
  harness.Schedule(R"(
    normal();
    for (i = 0; i < 1000; i += 1) padding = i;
    disabled();
    await pause;
    done = 1;
  )");

  harness.runtime.Run();

  EXPECT_TRUE(displayed_while_waiting);
  EXPECT_EQ(harness.Global("done"), sr::Value(1));
}

TEST_F(SiglusFrameLoopTest, HoverPortraitAppearsWhileScriptWaitsForFrame) {
  // The script shows a hidden portrait and plays hover SE; the next displayed
  // frame must contain the portrait even though the script waits on frame().
  auto object = harness.runtime.stage->objects.Get(kLayerFg, 0);
  std::vector<int> displayed;
  int sounds = 0;
  ON_CALL(*backend, RenderFrame(::testing::_, ::testing::_, ::testing::_))
      .WillByDefault([&](const auto&, const auto&, const auto&) {
        displayed.push_back(object->Param().visible());
      });
  srbind::module_ globals(harness.runtime.vm->gc_.get(),
                          harness.runtime.vm->globals_.get());
  globals.def("show_portrait", [&] { object->Param().SetVisible(1); });
  globals.def("hover_se", [&] { ++sounds; });
  harness.Schedule("show_portrait(); hover_se(); await frame();");

  harness.runtime.Run();

  EXPECT_EQ(sounds, 1);
  EXPECT_EQ(displayed, (std::vector<int>{0, 1}));
}

TEST_F(SiglusFrameLoopTest, LeavingEntryRemovesPortraitFromDisplayedFrame) {
  // The displayed portrait is hidden by the script; frame() must not leave its
  // old image on screen after the pointer leaves the entry.
  auto object = harness.runtime.stage->objects.Get(kLayerFg, 0);
  object->Param().SetVisible(1);
  std::vector<int> displayed;
  ON_CALL(*backend, RenderFrame(::testing::_, ::testing::_, ::testing::_))
      .WillByDefault([&](const auto&, const auto&, const auto&) {
        displayed.push_back(object->Param().visible());
      });
  srbind::module_ globals(harness.runtime.vm->gc_.get(),
                          harness.runtime.vm->globals_.get());
  globals.def("hide_portrait", [&] { object->Param().SetVisible(0); });
  harness.Schedule("hide_portrait(); await frame();");

  harness.runtime.Run();

  EXPECT_EQ(displayed, (std::vector<int>{1, 0}));
}

TEST_F(SiglusFrameLoopTest, FastClickConfirmsCapturedMenuEntry) {
  // A complete click arrives before either script step; the menu must first
  // capture the pressed entry and then confirm it once on release.
  InitMenu();
  Send(MouseDown{MouseButton::LEFT});
  Send(MouseUp{MouseButton::LEFT});
  MenuStep();
  EXPECT_EQ(harness.Global("confirmed"), sr::Value(0));
  MenuStep();
  MenuStep();

  EXPECT_EQ(harness.Global("confirmed"), sr::Value(1));
}

TEST_F(SiglusFrameLoopTest, HeldClickConfirmsCapturedMenuEntry) {
  // A held press spans several script steps; confirmation must wait for the
  // matching release and occur only once.
  InitMenu();
  Send(MouseDown{MouseButton::LEFT});
  MenuStep();
  MenuStep();
  EXPECT_EQ(harness.Global("confirmed"), sr::Value(0));
  Send(MouseUp{MouseButton::LEFT});
  MenuStep();

  EXPECT_EQ(harness.Global("confirmed"), sr::Value(1));
}

TEST_F(SiglusFrameLoopTest, DelayedScriptStillConfirmsReleasedMenuEntry) {
  // The live frame clears its edges before a suspended script resumes; its
  // buffered release must still confirm the entry captured by the script.
  InitMenu();
  Send(MouseDown{MouseButton::LEFT});
  MenuStep();
  Send(MouseUp{MouseButton::LEFT});
  harness.runtime.input_event_listener->ResetState();
  MenuStep();

  EXPECT_EQ(harness.Global("confirmed"), sr::Value(1));
}

TEST_F(SiglusFrameLoopTest, DraggingAwayDoesNotConfirmCapturedMenuEntry) {
  // A fast drag presses inside and releases outside before script execution;
  // ordered pointer snapshots must prevent selection of the captured entry.
  InitMenu();
  Send(MouseDown{MouseButton::LEFT});
  Send(MouseMotion{Point(30, 0)});
  Send(MouseUp{MouseButton::LEFT});
  MenuStep();
  MenuStep();

  EXPECT_EQ(harness.Global("confirmed"), sr::Value(0));
}

TEST_F(SiglusFrameLoopTest, RepeatedFastClicksConfirmOnceEach) {
  // Successive clicks each arrive before the menu runs; each capture/release
  // pair must confirm once despite clearing input after confirmation.
  InitMenu();
  for (int i = 0; i < 2; ++i) {
    Send(MouseDown{MouseButton::LEFT});
    Send(MouseUp{MouseButton::LEFT});
    MenuStep();
    MenuStep();
  }
  MenuStep();

  EXPECT_EQ(harness.Global("confirmed"), sr::Value(2));
}

TEST_F(SiglusFrameLoopTest, FrameAloneAdvancesScriptInput) {
  // A script samples the press then awaits frame without next; frame
  // completion must make the queued release visible on resumption.
  Send(MouseDown{MouseButton::LEFT});
  Send(MouseUp{MouseButton::LEFT});
  harness.Schedule(R"(
    pressed = input.decide.is_down();
    await frame();
    completed = input.decide.on_down_up();
  )");

  harness.runtime.Run();

  EXPECT_EQ(harness.Global("pressed"), sr::Value(1));
  EXPECT_EQ(harness.Global("completed"), sr::Value(1));
}

TEST_F(SiglusFrameLoopTest, NextBeforeFrameRetainsReleaseOnResumption) {
  // The normal menu sequence advances input before awaiting frame; the
  // boundary must not consume its release a second time.
  Send(MouseDown{MouseButton::LEFT});
  Send(MouseUp{MouseButton::LEFT});
  harness.Schedule(R"(
    pressed = input.decide.is_down();
    input.next();
    await frame();
    completed = input.decide.on_down_up();
  )");

  harness.runtime.Run();

  EXPECT_EQ(harness.Global("completed"), sr::Value(1));
}

TEST_F(SiglusFrameLoopTest, LocalResetReleasesNativeAndScriptInput) {
  // A scene reset with buffered clicks must release both consumers so stale
  // input cannot select a control in the new scene.
  Send(MouseDown{MouseButton::LEFT});
  for (auto& reset : harness.runtime.reset_local_memory)
    reset();
  harness.Invoke("held = input.decide.is_down();");

  EXPECT_FALSE(harness.runtime.input_event_listener->decide.down);
  EXPECT_EQ(harness.Global("held"), sr::Value(0));
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
  // A nondisplaying capture and frame wait are queued together; the capture's
  // display option must still suppress window presentation for that frame.
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
  auto capture =
      runtime.RequestCapture({.kind = CaptureKind::Object, .display = false});
  auto promise = sr::get_promise(capture);
  harness.Invoke("await frame(); done = 1;");

  runtime.Run();

  EXPECT_EQ(captures, 1);
  EXPECT_EQ(promise->status, sr::Promise::Status::Resolved);
  EXPECT_EQ(presentations, 0);
}

TEST_F(SiglusFrameLoopTest, DisplayingCapturePresentsDuringFrameWait) {
  // A capture explicitly allowing display must present normally while a
  // frame wait completes, preserving the capture's display policy.
  auto& runtime = harness.runtime;
  runtime.captures = std::make_shared<CaptureManager>(
      CaptureConfig{.screen_size = Size(1, 1)}, std::filesystem::path{},
      [](const CaptureRequest&) {
        return std::make_shared<SDLSurface>(Size(1, 1));
      },
      CaptureManager::Load{});
  runtime.RequestCapture({.kind = CaptureKind::Object, .display = true});
  harness.Invoke("await frame();");

  runtime.Run();

  EXPECT_EQ(presentations, 1);
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
