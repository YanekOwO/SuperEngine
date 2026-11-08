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

#include "core/capture.hpp"
#include "core/frame_action_manager.hpp"
#include "core/kidoku_table.hpp"
#include "core/message_back.hpp"
#include "core/mwnd_config.hpp"
#include "core/serialization_context.hpp"
#include "headless_resource_host.hpp"
#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/save_load.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "long_operations/siglus_wait.hpp"
#include "long_operations/wait_for_selection.hpp"
#include "m6/compiler_pipeline.hpp"
#include "mock_clock.hpp"
#include "poller_helpers.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/sound_system.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"
#include "vm/function.hpp"
#include "vm/future.hpp"

#include <gtest/gtest.h>
#include <atomic>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

namespace {
using namespace libsiglus;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

class SiglusSaveMessageTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static std::atomic<unsigned> sequence = 0;
    directory =
        fs::temp_directory_path() /
        ("rlvm-save-message-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()) +
         "-" + std::to_string(sequence.fetch_add(1)));
    runtime.save_pth = directory;
    runtime.archive = std::make_shared<Archive>(
        std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
    runtime.gameexe = std::make_shared<Gameexe>();
    for (const char* line :
         {"SCREEN_SIZE = 640,480", "MWND.CNT = 1", "MWND.0.MOJI_CNT = 42,4",
          "MWND.0.MOJI_SIZE = 20", "WAKU.CNT = 0", "WAKU.BTN.CNT = 0",
          "LOAD.WIPE = 0,0"})
      runtime.gameexe->parseLine(line);
    runtime.stage = std::make_unique<Stage>();
    runtime.counter_list = std::make_shared<CounterList>(1);
    runtime.frame_action_manager = std::make_shared<FrameActionManager>(
        *runtime.stage, runtime.local_config, 1);
    runtime.kidoku_table = std::make_shared<KidokuTable>();
    runtime.asset_scanner = std::make_shared<AssetScanner>();
    runtime.system = std::make_unique<System>(
        *runtime.gameexe, *runtime.stage, runtime.local_config,
        runtime.global_config, runtime.asset_scanner,
        MwndConfig::ParseSiglus(*runtime.gameexe));
    runtime.system->text().BindGraphics({.request_refresh = [] {}});
    runtime.binding_names = {"0_memory", "0_savepoint", "0_selection",
                             "1_mwnd",   "3_script",    "5_save_load",
                             "flow",     "textout",     "wipe"};
    runtime.configure_machine = [this](SiglusMachine& machine) {
      auto timer = std::make_unique<MockClock>();
      clock = timer.get();
      machine.vm->scheduler_ = serilang::Scheduler(
          std::make_unique<serilang::RecordingPoller>(), std::move(timer));
    };
    runtime.GameplayMachine().Initialize();
    runtime.global_config.message_speed = 20;
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
  }

  void TearDown() override {
    std::error_code error;
    fs::remove_all(directory, error);
  }

  void Run(const std::string& source) {
    auto& vm = *runtime.GameplayMachine().vm;
    m6::CompilerPipeline compiler(vm.gc_);
    compiler.compile(m6::SourceBuffer::Create(source, "<save message>"));
    if (!compiler.Ok())
      throw std::runtime_error(compiler.FormatErrors());
    auto* function = vm.gc_->Allocate<serilang::Function>(compiler.Get());
    vm.InvokeUntilBlocked(*function, {});
    while (runtime.Saves().pending())
      runtime.DriveScripts();
  }

  void Advance(std::chrono::milliseconds duration) {
    clock->AdvanceTime(duration);
    runtime.system->text().AdvanceMessageText(duration);
    runtime.GameplayMachine().vm->RunReadyUntilBlocked();
  }

  void Checkpoint() { Run("snapshot = savepoint();"); }

  void Reload() {
    ASSERT_TRUE(runtime.Saves().Load(runtime.GameplayMachine(), 0, false, false,
                                     false));
    while (runtime.Saves().pending())
      runtime.DriveScripts();
  }

  std::shared_ptr<TextWindow> Window() {
    return runtime.system->text().GetMwnd(0);
  }

  std::shared_ptr<Selection> ButtonSelection() {
    auto session = BuildSelection(*runtime.system,
                                  {.kind = SelectionRequest::Kind::Buttons,
                                   .options = {{.text = "choice"}}},
                                  {.open_time = 100}, {}, Size(640, 480));
    session->Start();
    runtime.GameplayMachine().selection_state->active_ = session;
    runtime.stage->selection[kLayerFg] = session;
    return session;
  }

  ScopedDummyDrivers drivers;
  SiglusRuntime runtime;
  MockClock* clock = nullptr;
  fs::path directory;
};

TEST_F(SiglusSaveMessageTest, DialogueRecordsOnceBeforeReveal) {
  // Logical dialogue enters history once, independently of glyph revelation.
  Run("await __builtin_textout(2, 9, \"hello\");");
  Advance(200ms);
  const auto entries = runtime.message_back->GetEntries();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].msg, "hello");
  EXPECT_EQ(entries[0].scene_no, 2);
}

TEST_F(SiglusSaveMessageTest, MutedVoiceRetainsCharacterForReplay) {
  // Muted dialogue still records the voice ID and its character preference.
  runtime.global_config.play_all_sound_check = false;
  Run("mwnd.koe(123, 4); await __builtin_textout(2, 9, \"voiced\");");
  const auto entries = runtime.message_back->GetEntries();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].koe_no_list, std::vector<int>({123}));
  EXPECT_EQ(entries[0].chara_no_list, std::vector<int>({4}));
}

TEST_F(SiglusSaveMessageTest, LineBreakRemainsInHistory) {
  // Explicit newline commands retain their separation in recorded dialogue.
  runtime.local_config.async_msg_mode = true;
  Run("await __builtin_textout(2, 9, \"first\"); mwnd.nl(); "
      "await __builtin_textout(2, 10, \"second\");");
  const auto entries = runtime.message_back->GetEntries();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].msg, "first\nsecond");
}

TEST_F(SiglusSaveMessageTest, RecordingCanBeDisabled) {
  // Disabling automatic recording leaves the backlog empty after dialogue.
  runtime.local_config.msg_back_off = true;
  Run("await __builtin_textout(2, 9, \"hidden\");");
  Advance(200ms);
  EXPECT_FALSE(runtime.message_back->HasMessages());
}

TEST_F(SiglusSaveMessageTest, ExplicitHistoryBypassesRecordingDisable) {
  // The script may insert special-scene history while automatic recording is
  // disabled; the text is not duplicated.
  (*binding::SiglusBindingRegistry::Find("msgbk"))(runtime.GameplayMachine());
  runtime.local_config.msg_back_off = true;
  Run("msgbk.add_namae(\"speaker\"); msgbk.add_msg(\"manual\");");
  const auto entries = runtime.message_back->GetEntries();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].msg, "manual");
  EXPECT_EQ(entries[0].original_name, "speaker");
}

TEST_F(SiglusSaveMessageTest, HistoryAttachesLiveCheckpoint) {
  // A recorded message points at the retained rollback snapshot for its block.
  runtime.gameexe->SetAt("MESSAGE_BACK_SAVE.CNT", {2});
  runtime.gameexe->SetAt("MESSAGE_BACK_SAVE.INTERVAL", {1});
  Run("mwnd.msg_block(); await __builtin_textout(2, 9, \"saved\");");
  const auto entries = runtime.message_back->GetEntries();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_NE(entries[0].checkpoint_id, 0u);
  EXPECT_TRUE(runtime.Saves().HasBacklogCheckpoint(entries[0].checkpoint_id));
}

TEST_F(SiglusSaveMessageTest, NewMessageBlockSeparatesHistory) {
  // A new message block starts a separate entry after the previous text.
  runtime.message_back->AddMessage("previous");
  Run("mwnd.msg_block(); await __builtin_textout(2, 9, \"next\");");
  const auto entries = runtime.message_back->GetEntries();
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[0].msg, "previous");
  EXPECT_EQ(entries[1].msg, "next");
}

TEST_F(SiglusSaveMessageTest, SaveBindingsPreserveSystemModuleMembers) {
  // Bind the system extensions in registry order; configuration, persistence,
  // and capture commands must coexist in the same script-visible module.
  auto& machine = runtime.GameplayMachine();
  for (auto entry = binding::SiglusBindingRegistry::cbegin();
       entry != binding::SiglusBindingRegistry::cend(); ++entry) {
    if (entry->first == "4_syscom" || entry->first == "5_save_load" ||
        entry->first == "syscom_capture")
      entry->second(machine);
  }
  auto* syscom = machine.vm->builtins_->at("syscom").Get_if<serilang::Module>();
  ASSERT_NE(syscom, nullptr);
  for (const char* name : {"set_all_volume_default", "set_readskip", "save",
                           "create_capture_buffer"})
    EXPECT_TRUE(syscom->globals->contains(name)) << name;
  Run("syscom.set_all_volume(13); syscom.set_all_volume_default(); "
      "syscom.set_readskip(1); saved_count = syscom.get_save_count(); "
      "readskip = syscom.get_readskip();");
  EXPECT_EQ(runtime.global_config.all_sound_user_volume,
            MakeSiglusGlobalConfig(*runtime.gameexe).all_sound_user_volume);
  EXPECT_EQ(machine.vm->globals_->at("readskip"), serilang::Value(1));
  EXPECT_EQ(machine.vm->globals_->at("saved_count"), serilang::Value(10));
}

TEST_F(SiglusSaveMessageTest, DisabledWipeLoadsWithoutWaitingForTransition) {
  // A long configured load wipe is disabled globally; both load phases must
  // complete immediately and resume the checkpoint with its restored result.
  Run("snapshot = savepoint(); A[0] = snapshot;");
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  runtime.gameexe->SetAt("LOAD.WIPE", {0, 60000});
  runtime.global_config.system.disable_wipe_anime = true;
  runtime.memory->Write(IntBank::A, 0, 9);
  auto* previous = &runtime.GameplayMachine();
  ASSERT_TRUE(runtime.Saves().Load(*previous, 0, false, false, true));
  runtime.DriveScripts();
  EXPECT_NE(&runtime.GameplayMachine(), previous);
  runtime.DriveScripts();
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 1);
}

TEST_F(SiglusSaveMessageTest,
       AutomaticMessageCheckpointRestoresBlockContinuation) {
  // A real message block captures before its following local-bank increment.
  runtime.memory->Write(IntBank::A, 0, 1);
  Run("mwnd.msg_block(); A[0] = A[0] + 1;");
  ASSERT_TRUE(runtime.Saves().manager().HasCheckpoint());
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  runtime.memory->Write(IntBank::A, 0, 99);
  Reload();
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 2);
}

TEST_F(SiglusSaveMessageTest,
       DisabledAutomaticMessageCheckpointClearsPreviousPoint) {
  // Entering a new block clears the previous source while automatic capture is
  // off.
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().manager().HasCheckpoint());
  Run("script.set_auto_savepoint_off(); mwnd.msg_block();");
  EXPECT_FALSE(runtime.Saves().manager().HasCheckpoint());
}

TEST_F(SiglusSaveMessageTest,
       TextoutLoadRestartsRevealWithoutSubmittingTextTwice) {
  // A saved reveal starts the page again without repeating text submission.
  Run("await __builtin_textout(0,7,\"abcdef\"); A[0] = 1;");
  Advance(40ms);
  ASSERT_TRUE(Window()->HasPendingMessageText());
  ASSERT_EQ(Window()->GetCharacterCount(), 6);
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  Window()->FlushMessageText();
  Advance(5ms);
  Reload();
  EXPECT_EQ(Window()->GetCharacterCount(), 6);
  EXPECT_TRUE(Window()->HasPendingMessageText());
  Advance(115ms);
  EXPECT_TRUE(Window()->HasPendingMessageText());
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 0);
  Advance(5ms);
  EXPECT_FALSE(Window()->HasPendingMessageText());
  EXPECT_EQ(Window()->GetCharacterCount(), 6);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 1);
}

TEST_F(SiglusSaveMessageTest, AutomaticPauseLoadRestartsWaitTime) {
  // A half-finished automatic pause gets a full interval after loading.
  runtime.global_config.enable_auto_mode = true;
  runtime.global_config.auto_mode_moji_wait = 0;
  runtime.global_config.auto_mode_min_wait = 100;
  Run("await mwnd.r(); A[0] = 1;");
  Advance(40ms);
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  Advance(60ms);
  ASSERT_EQ(runtime.memory->Read(IntBank::A, 0), 1);
  Reload();
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 0);
  Advance(99ms);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 0);
  Advance(5ms);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 1);
}

TEST_F(SiglusSaveMessageTest, CompletedPageLoadsWithoutRevealingAgain) {
  // A fully revealed page is reconstructed immediately while its input wait
  // remains.
  Run("await __builtin_textout(0,7,\"complete\"); await mwnd.r(); A[0] = 1;");
  Advance(160ms);
  ASSERT_FALSE(Window()->HasPendingMessageText());
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  Window()->ClearWin();
  Reload();
  EXPECT_EQ(Window()->GetCharacterCount(), 8);
  EXPECT_FALSE(Window()->HasPendingMessageText());
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 0);
}

TEST_F(SiglusSaveMessageTest, SelectionLoadRegeneratesTextWithSavedGeometry) {
  // Regenerated choice text keeps custom button geometry rather than text
  // dimensions.
  auto session = ButtonSelection();
  session->LayoutButtons(Size(100, 30));
  const auto original_text = session->items[0].normal;
  const auto original_rect = session->items[0].hit_rect;
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  Reload();
  auto loaded = runtime.selection_state->active_;
  ASSERT_NE(loaded, nullptr);
  ASSERT_NE(loaded->items[0].normal, nullptr);
  EXPECT_NE(loaded->items[0].normal, original_text);
  EXPECT_EQ(loaded->items[0].hit_rect, original_rect);
  EXPECT_EQ(loaded->items[0].size, Size(100, 30));
}

TEST_F(SiglusSaveMessageTest, SelectionLoadRestartsOpeningAnimation) {
  // A partly opened menu remains in its opening phase with a fresh animation.
  auto session = ButtonSelection();
  session->Advance(40);
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  Reload();
  auto loaded = runtime.selection_state->active_;
  ASSERT_NE(loaded, nullptr);
  EXPECT_EQ(loaded->phase(), Selection::Phase::Opening);
  EXPECT_EQ(loaded->elapsed(), 0);
}

TEST_F(SiglusSaveMessageTest, SelectionLoadDiscardsPendingInput) {
  // Loading an unresolved choice discards its hovered and pressed item.
  auto session = ButtonSelection();
  session->focused = session->pressed = 0;
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  Reload();
  auto loaded = runtime.selection_state->active_;
  ASSERT_NE(loaded, nullptr);
  EXPECT_FALSE(loaded->focused);
  EXPECT_FALSE(loaded->pressed);
}

TEST_F(SiglusSaveMessageTest, SelectionLoadDiscardsCapturedBackground) {
  // A transient selection background is omitted when restoring the selection.
  auto session = ButtonSelection();
  session->capture = std::make_shared<SDLSurface>(Size(1, 1));
  session->capture->Fill(RGBAColour(12, 34, 56, 255));
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  Reload();
  auto loaded = runtime.selection_state->active_;
  ASSERT_NE(loaded, nullptr);
  EXPECT_FALSE(loaded->capture);
}

TEST_F(SiglusSaveMessageTest,
       ActiveSelectionRestoresWithoutRepeatingOpeningHook) {
  // The opened session retains its consumed hook while its native waiter
  // resumes.
  SelectionRequest request{.options = {{.text = "choice"}},
                           .capture = true,
                           .start_scene = "must_not_reopen"};
  auto session = std::make_shared<Selection>(request);
  session->LayoutButtons(Size(100, 20));
  session->window = Window();
  session->Start();
  session->open_hook_done = true;
  session->focused = 0;
  auto& machine = runtime.GameplayMachine();
  machine.selection_state->active_ = session;
  runtime.stage->selection[kLayerFg] = session;
  auto operation = std::make_unique<lop::WaitForSelection>(
      machine.selection_state, session,
      [this](std::shared_ptr<Selection> selected) {
        runtime.FinishSelection(std::move(selected));
      });
  lop::FutureBackedCoroutineTask pending(*machine.vm, std::move(operation),
                                         runtime.system->event_ptr().get(), {},
                                         machine.WaitScope());
  (*machine.vm->globals_)["pending"] = pending.MakeFuture(*machine.vm->gc_);
  Run("A[2] = await pending; A[0] = 1;");
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  Reload();
  ASSERT_TRUE(runtime.selection_state->active_);
  EXPECT_TRUE(runtime.selection_state->active_->open_hook_done);
  EXPECT_EQ(runtime.selection_state->active_->phase(),
            Selection::Phase::Waiting);
  ASSERT_TRUE(runtime.selection_state->Choose(0));
  Advance(5ms);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 1);
}

TEST_F(SiglusSaveMessageTest, PreparedSelectionStaysUnopenedAfterLoad) {
  // A prepared session is restored without becoming an active input session.
  auto session = std::make_shared<Selection>(
      SelectionRequest{.options = {{.text = "prepared"}}});
  runtime.GameplayMachine().selection_state->prepared_ = session;
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  Reload();
  ASSERT_TRUE(runtime.selection_state->prepared_);
  EXPECT_EQ(runtime.selection_state->prepared_->phase(),
            Selection::Phase::Prepared);
  EXPECT_FALSE(runtime.selection_state->prepared_->open_hook_done);
  EXPECT_FALSE(runtime.selection_state->active_);
}

TEST_F(SiglusSaveMessageTest, AcceptedSelectionKeepsItsResult) {
  // Restarting a decide animation retains the accepted choice and cannot accept
  // another.
  auto session =
      BuildSelection(*runtime.system,
                     {.kind = SelectionRequest::Kind::Buttons,
                      .options = {{.text = "chosen"}, {.text = "other"}}},
                     {.open_type = 0}, {}, Size(640, 480));
  session->Start();
  session->open_hook_done = true;
  ASSERT_TRUE(session->Choose(0));
  session->Advance(100);
  runtime.GameplayMachine().selection_state->active_ = session;
  runtime.stage->selection[kLayerFg] = session;
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  Reload();
  auto loaded = runtime.selection_state->active_;
  ASSERT_NE(loaded, nullptr);
  EXPECT_EQ(loaded->result, 0);
  EXPECT_EQ(loaded->phase(), Selection::Phase::Deciding);
  EXPECT_EQ(loaded->elapsed(), 0);
  EXPECT_FALSE(loaded->Choose(1));
}

TEST_F(SiglusSaveMessageTest, PreparingLoadDoesNotInstallCandidateWipeHooks) {
  // Prepared wipe callbacks cannot enter the live frame pump before commit.
  Run("await wipe.wipe_all(0,100);");
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  const auto hooks = runtime.wipe_hooks.size();
  ASSERT_TRUE(
      runtime.Saves().Load(runtime.GameplayMachine(), 0, false, false, false));
  EXPECT_EQ(runtime.wipe_hooks.size(), hooks);
  while (runtime.Saves().pending())
    runtime.DriveScripts();
}

TEST_F(SiglusSaveMessageTest, PartialWipeRestartsWithoutRepeatingItsStageSwap) {
  // A loaded wipe restarts and completes once after its full duration.
  for (auto& layer : runtime.stage->objects.storage)
    layer.resize(1);
  runtime.stage->objects.Get(kLayerFg, 0)->Param().SetX(10);
  runtime.stage->objects.Get(kLayerBg, 0)->Param().SetX(20);
  Run("await wipe.wipe_all(0,100); A[3] = 1;");
  const int foreground_x = runtime.stage->objects.Get(kLayerFg, 0)->Param().x();
  const int next_x = runtime.stage->objects.Get(kLayerNext, 0)->Param().x();
  clock->AdvanceTime(40ms);
  runtime.Update(40ms, 40ms);
  runtime.GameplayMachine().vm->RunReadyUntilBlocked();
  const double alpha = runtime.stage->foreground_render_alpha();
  ASSERT_GT(alpha, 0.0);
  ASSERT_LT(alpha, 1.0);
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  Reload();
  EXPECT_DOUBLE_EQ(runtime.stage->foreground_render_alpha(), 0.0);
  EXPECT_EQ(runtime.stage->objects.Get(kLayerFg, 0)->Param().x(), foreground_x);
  EXPECT_EQ(runtime.stage->objects.Get(kLayerNext, 0)->Param().x(), next_x);
  clock->AdvanceTime(60ms);
  runtime.Update(60ms, 60ms);
  runtime.GameplayMachine().vm->RunReadyUntilBlocked();
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 3), 0);
  EXPECT_NEAR(runtime.stage->foreground_render_alpha(), 0.6, 0.001);
  clock->AdvanceTime(40ms);
  runtime.Update(40ms, 40ms);
  runtime.GameplayMachine().vm->RunReadyUntilBlocked();
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 3), 0);
  runtime.Update(0ms, 0ms);
  runtime.GameplayMachine().vm->RunReadyUntilBlocked();
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 3), 1);
}

TEST_F(SiglusSaveMessageTest, EndSaveWritesAfterPreparedCaptureCompletes) {
  // End saving accepts capture work, then writes the slot and its thumbnail.
  runtime.gameexe->SetIntAt("END_SAVE.CNT", 1);
  runtime.gameexe->SetIntAt("SAVE_THUMB.USE", 1);
  runtime.gameexe->SetIntAt("SAVE_THUMB.TYPE", 1);
  int captures = 0;
  runtime.captures = std::make_shared<CaptureManager>(
      CaptureConfig{.save_thumbnail = true,
                    .thumbnail_size = Size(2, 2),
                    .png_thumbnail = true,
                    .screen_size = Size(2, 2)},
      directory,
      [&](const CaptureRequest&) {
        ++captures;
        auto image = std::make_shared<SDLSurface>(Size(2, 2));
        image->Fill(RGBAColour(1, 2, 3, 255));
        return image;
      },
      CaptureManager::Load{});
  runtime.Saves().manager().Capture("opaque checkpoint");
  const int slot = *runtime.Saves().manager().EndSlot();
  ASSERT_TRUE(
      runtime.Saves().Save(0, false, false, SiglusSaveLoad::Source::End));
  EXPECT_FALSE(runtime.Saves().manager().Exists(slot));
  runtime.ProcessCaptureFrame();
  runtime.Saves().Process();
  EXPECT_EQ(captures, 1);
  EXPECT_TRUE(runtime.Saves().manager().Exists(slot));
  EXPECT_TRUE(fs::exists(runtime.Saves().manager().ThumbnailPath(slot)));
}

TEST_F(SiglusSaveMessageTest, NewCheckpointIncludesThePreviousNovelText) {
  // A new block's full metadata includes retained page text and its new
  // message.
  runtime.local_config.async_msg_mode = true;
  Run("await mwnd.print(\"first\");");
  Window()->FlushMessageText();
  Run("mwnd.msg_block(); await mwnd.print(\"second\");");
  EXPECT_EQ(runtime.Saves().manager().active().message, "second");
  EXPECT_EQ(runtime.Saves().manager().active().full_message, "firstsecond");
}

TEST_F(SiglusSaveMessageTest, NewPageClearsThePriorFullMessagePrefix) {
  // An actual page clear discards the prior page before taking its checkpoint.
  runtime.local_config.async_msg_mode = true;
  Run("await mwnd.print(\"old\"); mwnd.clear(); mwnd.msg_block();");
  EXPECT_TRUE(runtime.current_full_message.empty());
  EXPECT_TRUE(runtime.Saves().manager().active().full_message.empty());
}

TEST_F(SiglusSaveMessageTest, HiddenSpeakerNameDoesNotEnterSaveMetadata) {
  // Names suppressed by the window's display mode do not appear in slot text.
  Window()->Layout().SetNameMode(TextLayout::NameMode::Disable);
  Checkpoint();
  Run("__builtin_name(\"hidden\");");
  EXPECT_TRUE(runtime.Saves().manager().active().message.empty());
}

TEST_F(SiglusSaveMessageTest, UnimplementedStageClassesRemainUnavailable) {
  // Save/load support does not introduce placeholder Btnsel or World classes.
  const auto& globals = *runtime.GameplayMachine().vm->globals_;
  EXPECT_FALSE(globals.contains("Btnsel"));
  EXPECT_FALSE(globals.contains("World"));
}

}  // namespace
