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

#include "libsiglus/backlog.hpp"
#include "core/input.hpp"
#include "headless_resource_host.hpp"
#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/element_parser.hpp"
#include "libsiglus/save_load.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "m6/compiler_pipeline.hpp"
#include "mock_clock.hpp"
#include "poller_helpers.hpp"
#include "siglus/execution_test_helpers.hpp"
#include "vm/function.hpp"

#include <gtest/gtest.h>

namespace {
using namespace libsiglus;
using namespace std::chrono_literals;

class SiglusBacklogTest : public ::testing::Test {
 protected:
  void Initialize(SiglusRuntime& runtime, MockClock*& clock) {
    runtime.archive = std::make_shared<Archive>(
        std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
    runtime.gameexe = std::make_shared<Gameexe>();
    for (const auto* line :
         {"SCREEN_SIZE = 640,480", "MWND.CNT = 1", "WAKU.CNT = 0",
          "MSGBK.WINDOW_SIZE = 640,120", "MSGBK.MOJI_SIZE = 20"})
      runtime.gameexe->parseLine(line);
    runtime.stage = std::make_unique<Stage>();
    runtime.system = std::make_unique<System>(
        *runtime.gameexe, *runtime.stage, runtime.local_config,
        runtime.global_config, std::make_shared<AssetScanner>(),
        MwndConfig::ParseSiglus(*runtime.gameexe));
    runtime.system->text().BindGraphics({.request_refresh = [] {}});
    runtime.input_event_listener = std::make_shared<InputListener>();
    runtime.script_input = std::make_shared<ScriptInput>();
    runtime.binding_names = {"0_memory",    "0_savepoint", "4_syscom",
                             "5_save_load", "flow",        "syscom_backlog"};
    runtime.GameplayMachine().Initialize();
    auto timer = std::make_unique<MockClock>();
    clock = timer.get();
    runtime.GameplayMachine().vm->scheduler_ = serilang::Scheduler(
        std::make_unique<serilang::FakePoller>(), std::move(timer));
  }
  void SetUp() override {
    Initialize(source, source_clock);
    Initialize(target, target_clock);
  }
  void AddMessages(SiglusRuntime& runtime, int count = 10) {
    for (int i = 0; i < count; ++i) {
      runtime.message_back->AddMessage("recorded message");
      runtime.message_back->GoNextMsg();
    }
  }
  void Run(const std::string& code) {
    auto& vm = *source.GameplayMachine().vm;
    m6::CompilerPipeline compiler(vm.gc_);
    compiler.compile(m6::SourceBuffer::Create(code, "<native operation test>"));
    if (!compiler.Ok())
      throw std::runtime_error(compiler.FormatErrors());
    auto* function = vm.gc_->Allocate<serilang::Function>(compiler.Get());
    vm.InvokeUntilBlocked(*function, {});
  }
  void Poll(SiglusRuntime& runtime, MockClock* clock) {
    clock->AdvanceTime(10ms);
    runtime.GameplayMachine().vm->scheduler_.DrainExpiredTimers();
    runtime.DriveScripts();
  }
  ScopedDummyDrivers drivers;
  SiglusRuntime source, target;
  MockClock *source_clock, *target_clock;
};

TEST_F(SiglusBacklogTest, EmptyBacklogReturnsImmediately) {
  // Opening empty history returns without installing a modal session.
  Run("await syscom.open_msgback(); finished = 1;");
  EXPECT_FALSE(source.Backlog().IsOpen());
  EXPECT_EQ(source.GameplayMachine().vm->globals_->at("finished"),
            serilang::Value(1));
}

TEST_F(SiglusBacklogTest, DisabledBacklogCannotOpen) {
  // The local disable flag prevents entry even when messages exist.
  AddMessages(source);
  source.local_config.disable_msgback = true;
  EXPECT_FALSE(source.Backlog().Open(0));
}

TEST_F(SiglusBacklogTest, OpeningWaitsUntilClose) {
  // A script opening populated history resumes only after that session closes.
  AddMessages(source);
  Run("await syscom.open_msgback(); finished = 1;");
  Poll(source, source_clock);
  EXPECT_TRUE(source.Backlog().IsOpen());
  EXPECT_FALSE(source.GameplayMachine().vm->globals_->contains("finished"));
  source.Backlog().Close();
  Poll(source, source_clock);
  EXPECT_EQ(source.GameplayMachine().vm->globals_->at("finished"),
            serilang::Value(1));
}

TEST_F(SiglusBacklogTest, ScrollingIsClampedToHistory) {
  // History opens at the newest row and clamps navigation to either end.
  AddMessages(source);
  auto session = source.Backlog().Open(0);
  ASSERT_NE(session, nullptr);
  ASSERT_GT(source.Backlog().MaxScroll(), 0);
  EXPECT_EQ(session->scroll, source.Backlog().MaxScroll());
  source.Backlog().Scroll(-100000);
  EXPECT_EQ(session->scroll, 0);
  source.Backlog().Scroll(100000);
  EXPECT_EQ(session->scroll, source.Backlog().MaxScroll());
}

TEST_F(SiglusBacklogTest, ClosingKeyDoesNotReachDialogue) {
  // Escape closes the panel and consumes its matching release as well.
  AddMessages(source);
  source.Backlog().Open(0);
  auto down = std::make_shared<Event>(KeyDown{KeyCode::ESCAPE});
  auto up = std::make_shared<Event>(KeyUp{KeyCode::ESCAPE});
  source.Backlog().OnEvent(down);
  source.Backlog().OnEvent(up);
  EXPECT_FALSE(source.Backlog().IsOpen());
  EXPECT_TRUE(std::holds_alternative<std::monostate>(*down));
  EXPECT_TRUE(std::holds_alternative<std::monostate>(*up));
}

TEST_F(SiglusBacklogTest, RestoreReconnectsSessionAndAwaitingFiber) {
  // Saved presentation and native wait share one restored session, so closing
  // it resumes the restored caller at the same scroll position.
  AddMessages(source);
  Run("await syscom.open_msgback(); finished = 1;");
  source.Backlog().Scroll(-30);
  ExecutionWriter writer;
  ExecutionReader reader;
  auto restored = serialization_test::RoundTripMachineResource(
      source.Backlog().Capture(), writer, reader, source.GameplayMachine(),
      target.GameplayMachine());
  target.message_back->RestoreCheckpoint(
      source.message_back->CaptureCheckpoint());
  target.Backlog().Restore(restored.first);
  PrepareNativeWaits(target.GameplayMachine(), reader, restored.second);
  reader.FinishRestore();
  ActivateNativeWaits(target.GameplayMachine());
  EXPECT_EQ(target.Backlog().Capture()->scroll,
            source.Backlog().Capture()->scroll);
  target.Backlog().Close();
  Poll(target, target_clock);
  EXPECT_EQ(target.GameplayMachine().vm->globals_->at("finished"),
            serilang::Value(1));
}

TEST_F(SiglusBacklogTest, TextHistoryNavigationOpensConfiguredView) {
  // The pause-window history shortcut enters the same configured panel.
  AddMessages(source);
  source.Backlog();
  source.system->text().BackPage();
  EXPECT_TRUE(source.Backlog().IsOpen());
  EXPECT_EQ(source.Backlog().RowCount(), 10u);
  EXPECT_TRUE(source.system->text().IsReadingBacklog());
}
TEST_F(SiglusBacklogTest, VoiceReplayCyclesPairedCharacterIds) {
  // Repeated replay cycles the row's voice list without losing character IDs.
  source.global_config.play_all_sound_check = false;
  source.message_back->AddKoe(100, 2);
  source.message_back->AddKoe(101, 7);
  source.message_back->AddMessage("two voices");
  source.Backlog().Open(0);
  EXPECT_EQ(source.Backlog().ReplayVoice(0),
            std::make_optional(std::pair(100, 2)));
  EXPECT_EQ(source.Backlog().ReplayVoice(0),
            std::make_optional(std::pair(101, 7)));
  EXPECT_EQ(source.Backlog().ReplayVoice(0),
            std::make_optional(std::pair(100, 2)));
}

TEST_F(SiglusBacklogTest, MissingVoiceIsNotReplayable) {
  // A text-only entry does not start audio or advance replay state.
  AddMessages(source, 1);
  source.Backlog().Open(0);
  EXPECT_FALSE(source.Backlog().ReplayVoice(0));
  EXPECT_EQ(source.Backlog().Capture()->voice_row, -1);
}

TEST_F(SiglusBacklogTest, ExpiredCheckpointDoesNotClosePanel) {
  // A row whose checkpoint is no longer retained cannot initiate rollback.
  AddMessages(source, 1);
  source.message_back->SetCheckpointId(999);
  source.Backlog().Open(0);
  EXPECT_FALSE(source.Backlog().RequestRollback(0));
  EXPECT_TRUE(source.Backlog().IsOpen());
}

TEST_F(SiglusBacklogTest, RollbackRestoresRecordedContinuation) {
  // A retained row uses the normal load path and resumes its saved script.
  source.gameexe->SetAt("MESSAGE_BACK_SAVE.CNT", {2});
  source.gameexe->SetAt("MESSAGE_BACK_SAVE.INTERVAL", {1});
  source.gameexe->SetAt("LOAD.WIPE", {0, 0});
  source.global_config.play_all_sound_check = false;
  Run("snapshot = savepoint(); A[0] = 12;");
  source.DriveScripts();
  source.Saves().AppendMessage("rollback target");
  source.memory->Write(IntBank::A, 0, 99);
  source.Backlog().Open(0);
  ASSERT_TRUE(source.Backlog().RequestRollback(0));
  while (source.Saves().pending())
    source.DriveScripts();
  EXPECT_EQ(source.memory->Read(IntBank::A, 0), 12);
  EXPECT_FALSE(source.Backlog().IsOpen());
}

TEST_F(SiglusBacklogTest, FreeingExResourcesClosesTheirPanel) {
  // An EX-owned view cannot outlive the resources that opened it.
  source.excall = std::make_unique<Excall>();
  source.excall->Alloc({.group_count = 1});
  AddMessages(source);
  source.Backlog().Open(1);
  source.FreeExcall(false);
  EXPECT_FALSE(source.Backlog().IsOpen());
}

TEST_F(SiglusBacklogTest, ParserAwaitsBacklogEntry) {
  // The bare open element lowers to a call that suspends until close.
  std::vector<Property> properties;
  std::vector<Command> commands;
  std::vector<Type> arguments;
  elm::ElementParser parser(properties, properties, commands, commands,
                            arguments, 0, {}, {});
  elm::ElementCode element{63, 192};
  const auto chain = parser.Parse(element);
  auto* call = std::get_if<elm::Call>(&chain.nodes.back().var);
  ASSERT_NE(call, nullptr);
  EXPECT_TRUE(call->await_result);
}

}  // namespace
