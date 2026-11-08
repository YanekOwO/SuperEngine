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

#include <SDL3/SDL.h>

#include "core/button_action_table.hpp"
#include "core/input.hpp"
#include "core/kidoku_table.hpp"
#include "core/mwnd_config.hpp"
#include "libsiglus/bindings/loader.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "m6/compiler_pipeline.hpp"
#include "m6/vm_factory.hpp"
#include "mock_clock.hpp"
#include "mock_graphics_backend.hpp"
#include "poller_helpers.hpp"
#include "siglus/fake_scene.hpp"
#include "systems/event_system.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"
#include "vm/function.hpp"
#include "vm/promise.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace libsiglus::binding {
namespace sr = serilang;
using namespace std::chrono_literals;

class DummyDriverHint {
 public:
  explicit DummyDriverHint(const char* name) : name_(name) {
    if (const auto* previous = SDL_GetHint(name_))
      previous_ = previous;
    SDL_SetHint(name_, "dummy");
  }
  ~DummyDriverHint() {
    if (previous_)
      SDL_SetHint(name_, previous_->c_str());
    else
      SDL_ResetHint(name_);
  }

 private:
  const char* name_;
  std::optional<std::string> previous_;
};

class SiglusMwndMessageTest : public ::testing::Test {
 protected:
  DummyDriverHint video_{SDL_HINT_VIDEO_DRIVER};
  DummyDriverHint audio_{SDL_HINT_AUDIO_DRIVER};
  SiglusRuntime runtime;
  MockClock* clock = nullptr;
  int refreshes = 0;

  void SetUp() override {
    runtime.GameplayMachine().vm =
        std::make_unique<sr::VM>(m6::VMFactory::Create());
    runtime.GameplayMachine().frame_requests =
        std::make_unique<FrameRequestManager>(
            [vm = runtime.GameplayMachine().vm.get()] {
              auto promise = std::make_shared<sr::Promise>();
              vm->TrackPendingPromise(promise);
              return promise;
            });
    auto time = std::make_unique<MockClock>(Clock::timepoint_t{});
    clock = time.get();
    runtime.GameplayMachine().vm->scheduler_ =
        sr::Scheduler(std::make_unique<sr::RecordingPoller>(), std::move(time));
    runtime.archive = std::make_shared<Archive>(
        std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
    runtime.GameplayMachine().loader = std::make_unique<Loader>(
        *runtime.archive, *runtime.GameplayMachine().vm, false);
    runtime.kidoku_table = std::make_shared<KidokuTable>();
    runtime.gameexe = std::make_shared<Gameexe>();
    for (const auto* setting :
         {"SCREEN_SIZE = 1920,1080", "MWND.CNT = 5", "WAKU.CNT = 1",
          "WAKU.BTN.CNT = 0", "MWND.001.NOVEL_MODE = 1",
          "MWND.001.OVERFLOW_CHECK_SIZE = 60", "MWND.002.NOVEL_MODE = 1",
          "MWND.003.NOVEL_MODE = 1", "MWND.003.OVERFLOW_CHECK_SIZE = -1",
          "MWND.004.WINDOW_POS = 0,0", "MWND.004.WINDOW_SIZE = 1920,1080",
          "MWND.004.MESSAGE_POS = 163,524", "MWND.004.NOVEL_MODE = 1",
          "MWND.004.OVERFLOW_CHECK_SIZE = 200"})
      runtime.gameexe->parseLine(setting);
    for (int index = 0; index < 5; ++index) {
      const std::string prefix = "MWND." + std::to_string(index) + ".";
      runtime.gameexe->SetAt(prefix + "MOJI_CNT", {42, 4});
      runtime.gameexe->SetIntAt(prefix + "MOJI_SIZE", 38);
      runtime.gameexe->SetAt(prefix + "MOJI_SPACE", {0, 10});
    }
    runtime.stage = std::make_unique<Stage>();
    runtime.asset_scanner = std::make_shared<AssetScanner>();
    runtime.system = std::make_unique<System>(
        *runtime.gameexe, *runtime.stage, runtime.local_config,
        runtime.global_config, runtime.asset_scanner,
        MwndConfig::ParseSiglus(*runtime.gameexe));
    runtime.global_config.message_speed = 20;
    runtime.system->text().BindGraphics(
        {.request_refresh = [this] { ++refreshes; }});
    for (const char* name :
         {"0_memory", "flow", "1_mwnd", "textout", "3_script"}) {
      const auto* binding = SiglusBindingRegistry::Find(name);
      ASSERT_NE(binding, nullptr);
      (*binding)(runtime.GameplayMachine());
    }
    Run(R"(
      class MessageStageLayer {
        fn __init__(self, layer) { self.mwnd = MwndArray(layer); }
      }
      class MessageStage {
        fn __init__(self) {
          self.front = MessageStageLayer(0);
          self.back = MessageStageLayer(1);
        }
      }
      stage = MessageStage();
    )");
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
  }

  void Run(const std::string& source) {
    m6::CompilerPipeline compiler(runtime.GameplayMachine().vm->gc_);
    compiler.compile(m6::SourceBuffer::Create(source, "<mwnd message>"));
    if (!compiler.Ok())
      throw std::runtime_error(compiler.FormatErrors());
    auto* function = runtime.GameplayMachine().vm->gc_->Allocate<sr::Function>(
        compiler.Get());
    runtime.GameplayMachine().vm->InvokeUntilBlocked(*function, {});
  }

  void Advance(std::chrono::milliseconds delta) {
    clock->AdvanceTime(delta);
    Text().AdvanceMessageText(delta);
    runtime.GameplayMachine().vm->RunReadyUntilBlocked();
  }

  void PressSpace() {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_SPACE;
    event.key.key = SDLK_SPACE;
    SDL_PushEvent(&event);
    event.type = SDL_EVENT_KEY_UP;
    SDL_PushEvent(&event);
    runtime.system->event().ExecuteEventSystem();
    runtime.GameplayMachine().vm->RunReadyUntilBlocked();
  }

  void SetCtrl(bool pressed) {
    SDL_Event event{};
    event.type = pressed ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = SDL_SCANCODE_LCTRL;
    event.key.key = SDLK_LCTRL;
    SDL_PushEvent(&event);
    runtime.system->event().ExecuteEventSystem();
  }

  TextSystem& Text() { return runtime.system->text(); }
  std::shared_ptr<TextWindow> Window(int index = 0) {
    return Text().GetMwnd(index);
  }
  bool Completed(const char* name = "completed") {
    return runtime.GameplayMachine().vm->globals_->contains(name);
  }

  void InstallPrint(std::initializer_list<int> element) {
    FakeScene scene{.scene_name = "message", .command_name = "print_message"};
    scene.AppendElement(element);
    scene.AppendByte(ByteCode::Push);
    scene.AppendInt(static_cast<int>(Type::Int));
    scene.AppendInt(123);
    scene.AppendByte(ByteCode::Cmd);
    for (int value :
         {0, 1, static_cast<int>(Type::Int), 0, static_cast<int>(Type::None)})
      scene.AppendInt(value);
    if (*element.begin() == 12)
      scene.AppendInt(0);  // Global print carries a read marker after its call.
    scene.AddTo(*runtime.archive);
    (*runtime.GameplayMachine().vm->globals_)["print_message"] =
        runtime.GameplayMachine().loader->ResolveSceneCommand("message",
                                                              "print_message");
  }
};

TEST_F(SiglusMwndMessageTest, PrintWaitsForProgressiveReveal) {
  // Print two characters at 20 ms each; drawing progresses before the script
  // continuation resumes, and no character is revealed before its delay.
  Run("await mwnd.print(\"AB\"); completed = 1;");
  EXPECT_FALSE(Completed());
  EXPECT_TRUE(Window()->HasPendingMessageText());
  const int initial_refreshes = refreshes;
  Advance(19ms);
  EXPECT_EQ(refreshes, initial_refreshes);
  Advance(1ms);
  EXPECT_GT(refreshes, initial_refreshes);
  EXPECT_TRUE(Window()->HasPendingMessageText());
  EXPECT_FALSE(Completed());
  Advance(20ms);
  EXPECT_FALSE(Window()->HasPendingMessageText());
  EXPECT_TRUE(Completed());
}

TEST_F(SiglusMwndMessageTest, RecompiledGlobalPrintWaitsForReveal) {
  // Execute a global print instruction through the real scene translator;
  // its implicit wait must suspend the scene until every character appears.
  InstallPrint({12});
  Run("print_message(); completed = 1;");
  EXPECT_FALSE(Completed());
  EXPECT_TRUE(Window()->HasPendingMessageText());
  Advance(60ms);
  EXPECT_TRUE(Completed());
}

TEST_F(SiglusMwndMessageTest, RecompiledIndexedPrintWaitsForReveal) {
  // Translate an indexed message-window print; its implicit wait must use the
  // indexed window rather than the active global window.
  InstallPrint({38, 3, -1, 4, 4});
  Run("print_message(); completed = 1;");
  EXPECT_FALSE(Completed());
  EXPECT_TRUE(Window(4)->HasPendingMessageText());
  EXPECT_EQ(Text().FindMwnd(kLayerFg, 0), nullptr);
  Advance(60ms);
  EXPECT_TRUE(Completed());
}

TEST_F(SiglusMwndMessageTest, PrologueReserveReplacesThePreviousMessage) {
  // The novel window reserves more height than its four-line area; r must
  // mark a full clear so the next block contains only its new message.
  runtime.local_config.msg_nowait = true;
  Run("set_mwnd(4); mwnd.msg_block(); await mwnd.print(\"AB\"); "
      "await mwnd.r(); mwnd.msg_block(); await mwnd.print(\"C\");");
  EXPECT_EQ(Window(4)->GetCharacterCount(), 1);
  EXPECT_EQ(Window(4)->Layout().GetInsertionPoint().y(), 0);
}

TEST_F(SiglusMwndMessageTest, AsyncPrintQueuesWithoutBlocking) {
  // Explicit asynchronous mode lets the script continue while the queued
  // characters still reveal over engine time.
  Run("script.set_msg_async_mode_on(); await mwnd.print(\"AB\"); "
      "completed = 1;");
  EXPECT_TRUE(Completed());
  EXPECT_TRUE(Window()->HasPendingMessageText());
  Advance(40ms);
  EXPECT_FALSE(Window()->HasPendingMessageText());
}

TEST_F(SiglusMwndMessageTest, FullClearEndsOneShotAsyncMode) {
  // One-shot async mode lasts until a full clear-ready transition, which
  // restores synchronous output for the following message.
  Run("script.set_msg_async_mode_on_once(); mwnd.clear();");
  EXPECT_FALSE(runtime.local_config.async_msg_mode);
  EXPECT_FALSE(runtime.local_config.async_msg_mode_once);
}

TEST_F(SiglusMwndMessageTest, NowaitPrintCompletesImmediately) {
  // Message-nowait bypasses character delay and completes the awaited print
  // without leaving deferred glyphs behind.
  runtime.local_config.msg_nowait = true;
  Run("await mwnd.print(\"AB\"); completed = 1;");
  EXPECT_TRUE(Completed());
  EXPECT_FALSE(Window()->HasPendingMessageText());
}

TEST_F(SiglusMwndMessageTest, FastForwardPrintCompletesImmediately) {
  // Host fast-forward independently bypasses the normal character delay.
  runtime.system->set_force_fast_forward();
  Run("await mwnd.print(\"AB\"); completed = 1;");
  EXPECT_TRUE(Completed());
  EXPECT_FALSE(Window()->HasPendingMessageText());
}

TEST_F(SiglusMwndMessageTest, CtrlPausePreservesTextUntilTheNextFrame) {
  // Hold Ctrl through two prologue blocks; the first pause must suspend before
  // its text can be cleared and replaced without a display frame.
  SetCtrl(true);
  Run("set_mwnd(4); mwnd.msg_block(); await mwnd.print(\"AB\"); "
      "await mwnd.r(); mwnd.msg_block(); await mwnd.print(\"C\"); "
      "completed = 1;");
  EXPECT_FALSE(Completed());
  EXPECT_EQ(Window(4)->GetCharacterCount(), 2);
}

TEST_F(SiglusMwndMessageTest, CtrlPauseResumesAfterFrameCompletion) {
  // A skipped r queues a frame; elapsed real time alone cannot complete it,
  // but completing that frame permits its continuation to run.
  SetCtrl(true);
  Run("await mwnd.print(\"AB\"); await mwnd.r(); completed = 1;");
  Advance(100ms);
  EXPECT_FALSE(Completed());
  ASSERT_TRUE(runtime.GameplayMachine().frame_requests->BeginFrameRequests());
  runtime.GameplayMachine().frame_requests->CompleteFrameRequests(
      runtime.script_input.get());
  Advance(5ms);
  EXPECT_TRUE(Completed());
}

TEST_F(SiglusMwndMessageTest, CtrlYieldsEveryMessage) {
  // Skip ten pauses under Ctrl; each requires its own completed frame.
  SetCtrl(true);
  Run("advanced = 0; while (advanced < 10) { "
      "await mwnd.pp(); advanced = advanced + 1; } completed = 1;");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("advanced"),
            sr::Value(0));
  for (int advanced = 1; advanced <= 10; ++advanced) {
    ASSERT_TRUE(runtime.GameplayMachine().frame_requests->BeginFrameRequests());
    runtime.GameplayMachine().frame_requests->CompleteFrameRequests(
        runtime.script_input.get());
    Advance(5ms);
    EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("advanced"),
              sr::Value(advanced));
  }
  EXPECT_TRUE(Completed());
  EXPECT_FALSE(runtime.GameplayMachine().frame_requests->BeginFrameRequests());
}

TEST_F(SiglusMwndMessageTest, VsyncOffStillYieldsEveryMessage) {
  // Disable vsync waiting; skipped pauses must still yield every message.
  runtime.local_config.wait_display_vsync_off = true;
  SetCtrl(true);
  Run("advanced = 0; while (advanced < 6) { "
      "await mwnd.pp(); advanced = advanced + 1; }");
  ASSERT_TRUE(runtime.GameplayMachine().frame_requests->BeginFrameRequests());
  runtime.GameplayMachine().frame_requests->CompleteFrameRequests(
      runtime.script_input.get());
  Advance(5ms);
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("advanced"),
            sr::Value(1));
}

TEST_F(SiglusMwndMessageTest, CtrlPpWaitsForAFrame) {
  // A skipped pp uses the same frame boundary as r without marking a clear.
  SetCtrl(true);
  Run("await mwnd.pp(); completed = 1;");
  EXPECT_FALSE(Completed());
  ASSERT_TRUE(runtime.GameplayMachine().frame_requests->BeginFrameRequests());
  runtime.GameplayMachine().frame_requests->CompleteFrameRequests(
      runtime.script_input.get());
  Advance(5ms);
  EXPECT_TRUE(Completed());
  EXPECT_FALSE(Window()->message_state()->clear_ready);
}

TEST_F(SiglusMwndMessageTest, CtrlPageDefersClearUntilAFrameCompletes) {
  // An explicit page under Ctrl must preserve its current block until the
  // frame boundary, then perform the usual clear-ready transition.
  SetCtrl(true);
  Run("await mwnd.page(); completed = 1;");
  EXPECT_FALSE(Window()->message_state()->clear_ready);
  ASSERT_TRUE(runtime.GameplayMachine().frame_requests->BeginFrameRequests());
  runtime.GameplayMachine().frame_requests->CompleteFrameRequests(
      runtime.script_input.get());
  Advance(5ms);
  EXPECT_TRUE(Completed());
  EXPECT_TRUE(Window()->message_state()->clear_ready);
}

TEST_F(SiglusMwndMessageTest, CtrlPressedDuringPauseStillWaitsForAFrame) {
  // Starting Ctrl while a normal pause listens for input commits its advance,
  // but must not let that key event bypass the display boundary.
  Run("await mwnd.r(); completed = 1;");
  SetCtrl(true);
  runtime.GameplayMachine().vm->RunReadyUntilBlocked();
  EXPECT_FALSE(Completed());
  ASSERT_TRUE(runtime.GameplayMachine().frame_requests->BeginFrameRequests());
  runtime.GameplayMachine().frame_requests->CompleteFrameRequests(
      runtime.script_input.get());
  Advance(5ms);
  EXPECT_TRUE(Completed());
}

TEST_F(SiglusMwndMessageTest, CtrlRepeatCannotCompleteThePacingFrame) {
  // Repeated Ctrl-down notifications reach an already skipped pause; its
  // noninterruptible pacing wait must still require an engine frame.
  SetCtrl(true);
  Run("await mwnd.r(); completed = 1;");
  SetCtrl(true);
  Advance(10ms);
  EXPECT_FALSE(Completed());
}

TEST_F(SiglusMwndMessageTest, ReleasingCtrlFinishesTheCommittedAdvance) {
  // Release Ctrl during the pacing frame; the committed first pause finishes,
  // and the following message returns to normal timed character reveal.
  SetCtrl(true);
  Run("await mwnd.r(); advanced = 1; await mwnd.print(\"AB\"); "
      "completed = 1;");
  SetCtrl(false);
  ASSERT_TRUE(runtime.GameplayMachine().frame_requests->BeginFrameRequests());
  runtime.GameplayMachine().frame_requests->CompleteFrameRequests(
      runtime.script_input.get());
  Advance(5ms);
  EXPECT_TRUE(Completed("advanced"));
  EXPECT_FALSE(Completed());
  EXPECT_TRUE(Window()->HasPendingMessageText());
}

TEST_F(SiglusMwndMessageTest, ReadSkipUsesMessagePacing) {
  // Read-skip without Ctrl shares interactive message pacing when the current
  // read marker permits skipping.
  Text().SetKidokuRead(true);
  Text().SetSkipMode(true);
  Run("await mwnd.r(); completed = 1;");
  EXPECT_FALSE(Completed());
  EXPECT_TRUE(runtime.GameplayMachine().frame_requests->BeginFrameRequests());
}

TEST_F(SiglusMwndMessageTest, MessageOnlyWaitDoesNotRequestAPacingFrame) {
  // msg_wait only drains pending glyphs, so Ctrl must complete it without
  // adding the display boundary reserved for message key pauses.
  SetCtrl(true);
  Run("await mwnd.print(\"AB\"); await mwnd.msg_wait(); completed = 1;");
  EXPECT_TRUE(Completed());
  EXPECT_FALSE(runtime.GameplayMachine().frame_requests->BeginFrameRequests());
}

TEST_F(SiglusMwndMessageTest, ForcedFastForwardWaitsForTheFirstMessageFrame) {
  // Forced skipping flushes reveal immediately, but its first message pause
  // must still wait for an engine frame before resuming the script.
  runtime.system->set_force_fast_forward();
  Run("await mwnd.print(\"AB\"); await mwnd.r(); completed = 1;");
  EXPECT_FALSE(Completed());
  ASSERT_TRUE(runtime.GameplayMachine().frame_requests->BeginFrameRequests());
  runtime.GameplayMachine().frame_requests->CompleteFrameRequests(
      runtime.script_input.get());
  Advance(5ms);
  EXPECT_TRUE(Completed());
}

TEST_F(SiglusMwndMessageTest, ForcedFastForwardStillYieldsEveryMessage) {
  // Force skipping with Ctrl disabled; every skipped pause still needs a frame.
  runtime.local_config.disable_ctrl = true;
  runtime.system->set_force_fast_forward();
  Run("advanced = 0; while (advanced < 8) { "
      "await mwnd.pp(); advanced = advanced + 1; }");
  ASSERT_TRUE(runtime.GameplayMachine().frame_requests->BeginFrameRequests());
  runtime.GameplayMachine().frame_requests->CompleteFrameRequests(
      runtime.script_input.get());
  Advance(5ms);
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("advanced"),
            sr::Value(1));
}

TEST_F(SiglusMwndMessageTest, ResetRejectsThePacingWaitWithoutClearingText) {
  // Reset a suspended pacing frame; the script receives cancellation and the
  // abandoned pause must not apply its clear-ready cleanup.
  SetCtrl(true);
  Run("caught = 0; try { await mwnd.r(); } catch (e) { caught = 1; }");
  runtime.GameplayMachine().frame_requests->Reset();
  Advance(5ms);
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("caught"), sr::Value(1));
  EXPECT_FALSE(Window()->message_state()->clear_ready);
}

TEST_F(SiglusMwndMessageTest, CtrlMessageIsPresentedBeforeReplacement) {
  // Run the real engine loop with a recording presenter; the first message
  // must reach display before the script can replace it with the next block.
  auto backend = std::make_shared<::testing::NiceMock<MockGraphicsBackend>>();
  ON_CALL(*backend, Resize(::testing::_, ::testing::_))
      .WillByDefault(::testing::Return(Size(1920, 1080)));
  std::vector<int> displayed;
  ON_CALL(*backend, RenderFrame(::testing::_, ::testing::_, ::testing::_))
      .WillByDefault([&](const auto&, const auto&, const auto&) {
        displayed.push_back(Window(4)->GetCharacterCount());
      });
  runtime.presenter =
      std::make_unique<FramePresenter>(backend, Size(1920, 1080), false);
  runtime.input_event_listener = std::make_shared<InputListener>();
  runtime.interaction_manager = std::make_unique<InteractionManager>(
      *runtime.stage, *runtime.input_event_listener, ButtonActionTable{});
  int frames = 0;
  runtime.update_wipe_after_frame_actions = [&] {
    clock->AdvanceTime(10ms);
    if (++frames > 5)
      runtime.GameplayMachine().vm->RequestStop();
  };
  SetCtrl(true);
  Run("set_mwnd(4); mwnd.msg_block(); await mwnd.print(\"AB\"); "
      "await mwnd.r(); mwnd.msg_block(); await mwnd.print(\"C\"); "
      "completed = 1;");
  runtime.Run();
  EXPECT_TRUE(Completed());
  EXPECT_EQ(displayed, (std::vector<int>{2}));
  EXPECT_EQ(Window(4)->GetCharacterCount(), 1);
}

class SiglusFastForwardOptionsTest : public ::testing::Test {
 protected:
  DummyDriverHint video_{SDL_HINT_VIDEO_DRIVER};
  DummyDriverHint audio_{SDL_HINT_AUDIO_DRIVER};
  SiglusRuntime runtime;
  MockClock* clock = nullptr;

  void SetUp() override {
    runtime.GameplayMachine().vm =
        std::make_unique<sr::VM>(m6::VMFactory::Create());
    runtime.GameplayMachine().frame_requests =
        std::make_unique<FrameRequestManager>(
            [vm = runtime.GameplayMachine().vm.get()] {
              auto promise = std::make_shared<sr::Promise>();
              vm->TrackPendingPromise(promise);
              return promise;
            });
    auto time = std::make_unique<MockClock>(Clock::timepoint_t{});
    clock = time.get();
    runtime.GameplayMachine().vm->scheduler_ =
        sr::Scheduler(std::make_unique<sr::RecordingPoller>(), std::move(time));
    runtime.gameexe = std::make_shared<Gameexe>();
    runtime.gameexe->parseLine("MWND.CNT = 1");
    runtime.gameexe->parseLine("WAKU.BTN.CNT = 0");
    runtime.stage = std::make_unique<Stage>();
    runtime.asset_scanner = std::make_shared<AssetScanner>();
    runtime.system = std::make_unique<System>(
        *runtime.gameexe, *runtime.stage, runtime.local_config,
        runtime.global_config, runtime.asset_scanner,
        MwndConfig::ParseSiglus(*runtime.gameexe),
        SystemOptions{.fast_forward = true});
    (*SiglusBindingRegistry::Find("1_mwnd"))(runtime.GameplayMachine());
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    runtime.system->event().ExecuteEventSystem();
  }

  void Run(const std::string& source) {
    m6::CompilerPipeline compiler(runtime.GameplayMachine().vm->gc_);
    compiler.compile(m6::SourceBuffer::Create(source, "<automated message>"));
    if (!compiler.Ok())
      throw std::runtime_error(compiler.FormatErrors());
    auto* function = runtime.GameplayMachine().vm->gc_->Allocate<sr::Function>(
        compiler.Get());
    runtime.GameplayMachine().vm->InvokeUntilBlocked(*function, {});
  }
};

TEST_F(SiglusFastForwardOptionsTest,
       CliFastForwardWaitsForTheFirstMessageFrame) {
  // CLI skipping retains its first message until an actual engine frame
  // completes, rather than allowing unrestricted message replacement.
  Run("await mwnd.print(\"AB\"); await mwnd.r(); completed = 1;");
  EXPECT_FALSE(runtime.GameplayMachine().vm->globals_->contains("completed"));
  ASSERT_TRUE(runtime.GameplayMachine().frame_requests->BeginFrameRequests());
  runtime.GameplayMachine().frame_requests->CompleteFrameRequests(
      runtime.script_input.get());
  clock->AdvanceTime(5ms);
  runtime.GameplayMachine().vm->RunReadyUntilBlocked();
  EXPECT_TRUE(runtime.GameplayMachine().vm->globals_->contains("completed"));
}

TEST_F(SiglusFastForwardOptionsTest, CliFastForwardYieldsEveryMessage) {
  // CLI skipping must suspend at every message until an engine frame completes.
  Run("advanced = 0; while (advanced < 18) { "
      "await mwnd.pp(); advanced = advanced + 1; } completed = 1;");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("advanced"),
            sr::Value(0));
  for (int advanced = 1; advanced <= 18; ++advanced) {
    ASSERT_TRUE(runtime.GameplayMachine().frame_requests->BeginFrameRequests());
    runtime.GameplayMachine().frame_requests->CompleteFrameRequests(
        runtime.script_input.get());
    clock->AdvanceTime(5ms);
    runtime.GameplayMachine().vm->RunReadyUntilBlocked();
    EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("advanced"),
              sr::Value(advanced));
  }
  EXPECT_TRUE(runtime.GameplayMachine().vm->globals_->contains("completed"));
  EXPECT_FALSE(runtime.GameplayMachine().frame_requests->BeginFrameRequests());
}

TEST_F(SiglusFastForwardOptionsTest,
       CombiningAutomationModesStillYieldsEveryMessage) {
  // Enable forced and CLI skipping together; every pause still needs a frame.
  runtime.system->set_force_fast_forward();
  Run("advanced = 0; while (advanced < 8) { "
      "await mwnd.pp(); advanced = advanced + 1; }");
  ASSERT_TRUE(runtime.GameplayMachine().frame_requests->BeginFrameRequests());
  runtime.GameplayMachine().frame_requests->CompleteFrameRequests(
      runtime.script_input.get());
  clock->AdvanceTime(5ms);
  runtime.GameplayMachine().vm->RunReadyUntilBlocked();
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("advanced"),
            sr::Value(1));
}

TEST_F(SiglusFastForwardOptionsTest, CliFastForwardRespectsCtrlDisable) {
  // CLI fast-forward must preserve scripts' existing ability to
  // disable Ctrl skipping rather than turning it into forced fast-forward.
  runtime.local_config.disable_ctrl = true;
  Run("await mwnd.r(); completed = 1;");
  EXPECT_FALSE(runtime.GameplayMachine().vm->globals_->contains("completed"));
  EXPECT_FALSE(runtime.GameplayMachine().frame_requests->BeginFrameRequests());
}

TEST_F(SiglusMwndMessageTest, MultiMessageStreamsRemainNonblocking) {
  // Two multi-message streams advance together while the script continues;
  // each retains its own two-character reveal rather than flushing at print.
  Run("mwnd.enable_multi_msg(); await mwnd.print(\"AB\"); mwnd.next_msg(); "
      "await mwnd.print(\"CD\"); completed = 1;");
  EXPECT_TRUE(Completed());
  EXPECT_TRUE(Window()->HasPendingMessageText());
  Advance(20ms);
  EXPECT_TRUE(Window()->HasPendingMessageText());
  Advance(20ms);
  EXPECT_FALSE(Window()->HasPendingMessageText());
}

TEST_F(SiglusMwndMessageTest, ExplicitWaitDrainsAsyncText) {
  // An explicit message wait still suspends after asynchronous submission,
  // and resumes only when that window's timed reveal finishes.
  Run("script.set_msg_async_mode_on(); await mwnd.print(\"AB\"); "
      "queued = 1; await mwnd.msg_wait(); completed = 1;");
  EXPECT_TRUE(Completed("queued"));
  EXPECT_FALSE(Completed());
  Advance(40ms);
  EXPECT_TRUE(Completed());
}

TEST_F(SiglusMwndMessageTest, RevealClickDoesNotDismissTheFollowingPause) {
  // A click finishes the current reveal; the subsequent r needs a separate
  // click rather than reusing the event that skipped the characters.
  Run("await mwnd.print(\"AB\"); revealed = 1; await mwnd.r(); completed = 1;");
  PressSpace();
  EXPECT_FALSE(Window()->HasPendingMessageText());
  EXPECT_TRUE(Completed("revealed"));
  EXPECT_FALSE(Completed());
  PressSpace();
  EXPECT_TRUE(Completed());
}

TEST_F(SiglusMwndMessageTest, NovelWindowWithRoomKeepsItsParagraphs) {
  // A novel window with disabled overflow reserve retains the prior message
  // and starts its next paragraph one line below it.
  runtime.local_config.msg_nowait = true;
  Run("set_mwnd(2); mwnd.msg_block(); await mwnd.print(\"AB\"); "
      "await mwnd.r(); mwnd.msg_block(); await mwnd.print(\"C\");");
  EXPECT_EQ(Window(2)->GetCharacterCount(), 3);
  EXPECT_EQ(Window(2)->Layout().GetInsertionPoint().y(), 48);
}

TEST_F(SiglusMwndMessageTest,
       NovelReserveClearsWhenTheNextParagraphDoesNotFit) {
  // A positive reserve allows several paragraphs, then starts a fresh page
  // when projecting the next newline reaches its reserved bottom region.
  runtime.local_config.msg_nowait = true;
  Run("set_mwnd(1); mwnd.msg_block(); await mwnd.print(\"A\"); "
      "await mwnd.r(); mwnd.msg_block(); await mwnd.print(\"B\"); "
      "await mwnd.r(); mwnd.msg_block(); await mwnd.print(\"C\"); "
      "await mwnd.r(); mwnd.msg_block(); await mwnd.print(\"D\");");
  EXPECT_EQ(Window(1)->GetCharacterCount(), 1);
  EXPECT_EQ(Window(1)->Layout().GetInsertionPoint().y(), 0);
}

TEST_F(SiglusMwndMessageTest, BlockEntryClearsWhenTheReserveDoesNotFit) {
  // Enter a new message block without an r transition; the same reserve check
  // must clear existing content before that block can submit more text.
  runtime.local_config.msg_nowait = true;
  Run("set_mwnd(4); await mwnd.print(\"AB\"); mwnd.msg_block();");
  EXPECT_EQ(Window(4)->GetCharacterCount(), 0);
  EXPECT_EQ(Window(4)->Layout().GetInsertionPoint(), Point(0, 0));
}

TEST_F(SiglusMwndMessageTest, RepeatedBlockMarkerDoesNotClearWithinABlock) {
  // Repeated markers inside one block are idempotent even when the reserve
  // would otherwise force a new page.
  runtime.local_config.msg_nowait = true;
  Run("set_mwnd(4); mwnd.msg_block(); await mwnd.print(\"AB\"); "
      "mwnd.msg_block();");
  EXPECT_EQ(Window(4)->GetCharacterCount(), 2);
}

TEST_F(SiglusMwndMessageTest, NonpositiveReserveDisablesTheFitCheck) {
  // Zero and negative reserves disable automatic pagination even with a
  // cursor beyond the message area's bottom edge.
  for (int index : {2, 3}) {
    SCOPED_TRACE(index);
    auto window = Window(index);
    window->Layout().SetInsertionPoint(Point(0, 1000));
    EXPECT_TRUE(window->CanFitMessage(false));
    EXPECT_TRUE(window->CanFitMessage(true));
  }
}

TEST_F(SiglusMwndMessageTest, ClonedWindowRetainsItsOverflowReserve) {
  // A cloned prologue window must retain its conservative fit policy rather
  // than reverting to the default disabled reserve.
  auto clone = Window(4)->Clone();
  EXPECT_FALSE(clone->CanFitMessage(false));
}

TEST_F(SiglusMwndMessageTest, IndexedPrintCapturesItsTargetBeforeAwait) {
  // Retain a background-window print future, then change active selection;
  // awaiting it must still submit into the original indexed window.
  Run("pending = stage.back.mwnd[4].print(\"AB\"); set_mwnd(0); "
      "await pending; completed = 1;");
  const auto target = Text().FindMwnd(kLayerBg, 4);
  ASSERT_NE(target, nullptr);
  EXPECT_TRUE(target->HasPendingMessageText());
  EXPECT_EQ(Text().FindMwnd(kLayerFg, 0), nullptr);
  Advance(40ms);
  EXPECT_TRUE(Completed());
  EXPECT_EQ(target->GetCharacterCount(), 2);
}

TEST_F(SiglusMwndMessageTest, ClearAtRResetsOneShotAsyncMode) {
  // A normal window's r marks a full clear and ends one-shot asynchronous
  // mode, just like an explicit clear command.
  runtime.local_config.msg_nowait = true;
  Run("script.set_msg_async_mode_on_once(); await mwnd.r();");
  EXPECT_FALSE(runtime.local_config.async_msg_mode);
  EXPECT_FALSE(runtime.local_config.async_msg_mode_once);
}

TEST_F(SiglusMwndMessageTest, PageResetsOneShotAsyncMode) {
  // Explicit page clearing ends one-shot asynchronous mode in a novel window.
  runtime.local_config.msg_nowait = true;
  Run("set_mwnd(2); script.set_msg_async_mode_on_once(); await mwnd.page();");
  EXPECT_FALSE(runtime.local_config.async_msg_mode);
  EXPECT_FALSE(runtime.local_config.async_msg_mode_once);
}

TEST_F(SiglusMwndMessageTest, NovelParagraphPreservesOneShotAsyncMode) {
  // A novel r with room for another paragraph does not end one-shot async
  // mode because no full clear-ready transition occurred.
  runtime.local_config.msg_nowait = true;
  Run("set_mwnd(2); script.set_msg_async_mode_on_once(); await mwnd.r();");
  EXPECT_TRUE(runtime.local_config.async_msg_mode);
  EXPECT_TRUE(runtime.local_config.async_msg_mode_once);
  EXPECT_FALSE(Window(2)->message_state()->clear_ready);
}

TEST_F(SiglusMwndMessageTest, NovelClearPreservesThePageAndAsyncMode) {
  // Explicit novel cleanup keeps existing text and one-shot async mode while
  // moving the insertion point to a fresh paragraph.
  Run("script.set_msg_async_mode_on_once(); await mwnd.print(\"AB\"); "
      "mwnd.novel_clear();");
  EXPECT_EQ(Window()->GetCharacterCount(), 2);
  EXPECT_EQ(Window()->Layout().GetInsertionPoint().y(), 48);
  EXPECT_FALSE(Window()->message_state()->clear_ready);
  EXPECT_TRUE(runtime.local_config.async_msg_mode_once);
}

TEST_F(SiglusMwndMessageTest, FullClearPreservesPersistentAsyncMode) {
  // Persistent async output remains enabled across full clear transitions.
  Run("script.set_msg_async_mode_on(); mwnd.clear();");
  EXPECT_TRUE(runtime.local_config.async_msg_mode);
  EXPECT_FALSE(runtime.local_config.async_msg_mode_once);
}

TEST_F(SiglusMwndMessageTest, BuiltinTextoutHonorsAsyncMode) {
  // Builtin textout follows the same nonblocking reveal rule and still records
  // its read marker when asynchronous output is enabled.
  Run("script.set_msg_async_mode_on(); "
      "await __builtin_textout(0, 7, \"AB\"); completed = 1;");
  EXPECT_TRUE(Completed());
  EXPECT_TRUE(Window()->HasPendingMessageText());
  EXPECT_TRUE(runtime.kidoku_table->HasBeenRead(0, 7));
  Advance(40ms);
  EXPECT_FALSE(Window()->HasPendingMessageText());
}

TEST_F(SiglusMwndMessageTest, BuiltinTextoutStillWaitsInSynchronousMode) {
  // Synchronous builtin textout keeps waiting for the shared reveal operation
  // after it is changed to capture an explicit target window.
  Run("await __builtin_textout(0, 7, \"AB\"); completed = 1;");
  EXPECT_FALSE(Completed());
  Advance(40ms);
  EXPECT_TRUE(Completed());
}

TEST_F(SiglusMwndMessageTest, EmptyPrintCompletesWithoutCreatingAWindow) {
  // Empty submissions return a resolved awaitable without creating a message
  // window or waiting for a character that will never be queued.
  Run("await mwnd.print(\"\"); completed = 1;");
  EXPECT_TRUE(Completed());
  EXPECT_EQ(Text().FindMwnd(kLayerFg, 0), nullptr);
}

}  // namespace libsiglus::binding
