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

#include "core/message_back.hpp"
#include "libsiglus/bindings/loader.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/save_load.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "platforms/implementor.hpp"
#include "siglus/fake_scene.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace libsiglus {
namespace {
namespace fs = std::filesystem;

class SavePrompt : public IPlatformImplementor {
 public:
  fs::path SelectGameDirectory() override { return {}; }
  void ReportFatalError(const std::string&, const std::string&) override {}
  bool AskUserPrompt(const std::string& message,
                     const std::string&,
                     const std::string&,
                     const std::string&) override {
    text = message;
    return false;
  }
  std::string text;
};

class SiglusSaveCommandTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static std::atomic<unsigned> sequence = 0;
    directory =
        fs::temp_directory_path() /
        ("rlvm-save-command-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()) +
         "-" + std::to_string(sequence.fetch_add(1)));
    fs::create_directories(directory);
    runtime.archive = std::make_shared<Archive>(
        std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
    runtime.gameexe = std::make_shared<Gameexe>();
    runtime.save_pth = directory;
    runtime.platform_implementor = prompt;
    runtime.binding_names = {"0_memory", "0_savepoint", "5_save_load", "flow",
                             "frame"};
    runtime.GameplayMachine().Initialize();
    runtime.global_config.system.alert_saveload = true;
  }

  void TearDown() override {
    std::error_code error;
    fs::remove_all(directory, error);
  }

  void MakeDiskSlot(int slot) {
    runtime.Saves().manager().Capture("opaque runtime");
    ASSERT_TRUE(runtime.Saves().manager().SaveSlot(slot));
  }

  void CheckNestedMenu(bool save) {
    for (const std::string name : {"outer", "menu"}) {
      binding::FakeScene scene{.scene_name = name, .command_name = name};
      scene.AppendElement({6});
      scene.AppendByte(ByteCode::Cmd);
      for (const int value : {0, 0, 0, static_cast<int>(Type::None)})
        scene.AppendInt(value);
      scene.AddTo(*runtime.archive);
    }
    runtime.gameexe->SetStringAt(save ? "SAVE_SCENE" : "LOAD_SCENE", "menu");
    if (save)
      runtime.Saves().manager().Capture("opaque runtime");
    ASSERT_TRUE(
        runtime.OpenExcall(ScriptCall{.target = SceneTarget{"outer", 0}}));
    runtime.DriveScripts();
    ASSERT_TRUE(runtime.IsExcall());
    auto* outer = &runtime.ActiveMachine();
    runtime.Saves().OpenMenu(save);
    runtime.DriveScripts();
    auto& menu = runtime.ActiveMachine();
    ASSERT_NE(&menu, outer);
    ASSERT_TRUE(menu.frame_requests->BeginFrameRequests());
    menu.frame_requests->CompleteFrameRequests(nullptr);
    runtime.DriveScripts();
    EXPECT_EQ(&runtime.ActiveMachine(), outer);
    ASSERT_TRUE(outer->frame_requests->BeginFrameRequests());
    outer->frame_requests->CompleteFrameRequests(nullptr);
    runtime.DriveScripts();
    EXPECT_FALSE(runtime.IsExcall());
  }

  SiglusRuntime runtime;
  fs::path directory;
  std::shared_ptr<SavePrompt> prompt = std::make_shared<SavePrompt>();
};

TEST_F(SiglusSaveCommandTest, NormalSaveUsesConfiguredWarningText) {
  // Existing normal slots use the normal save prompt and honor cancellation.
  runtime.gameexe->SetStringAt("SAVEINFO.WARNING_STR", "normal save");
  MakeDiskSlot(0);
  EXPECT_FALSE(runtime.Saves().Save(0, true, false));
  EXPECT_EQ(prompt->text, "normal save");
}

TEST_F(SiglusSaveCommandTest, QuickSaveUsesConfiguredWarningText) {
  // Quick save translates the slot and uses its distinct overwrite prompt.
  runtime.gameexe->SetStringAt("SAVEINFO.QUICK_WARNING_STR", "quick save");
  MakeDiskSlot(*runtime.Saves().manager().QuickSlot(0));
  EXPECT_FALSE(
      runtime.Saves().Save(0, true, false, SiglusSaveLoad::Source::Quick));
  EXPECT_EQ(prompt->text, "quick save");
}

TEST_F(SiglusSaveCommandTest, NormalLoadUsesConfiguredWarningText) {
  // Loading a present slot asks the configured load prompt before preparation.
  runtime.gameexe->SetStringAt("LOADINFO.WARNING_STR", "normal load");
  MakeDiskSlot(0);
  EXPECT_FALSE(
      runtime.Saves().Load(runtime.GameplayMachine(), 0, true, false, false));
  EXPECT_EQ(prompt->text, "normal load");
}

TEST_F(SiglusSaveCommandTest, QuickLoadUsesConfiguredWarningText) {
  // Quick loading uses the quick-specific text and stops on cancellation.
  runtime.gameexe->SetStringAt("LOADINFO.QUICK_WARNING_STR", "quick load");
  MakeDiskSlot(*runtime.Saves().manager().QuickSlot(0));
  EXPECT_FALSE(runtime.Saves().Load(runtime.GameplayMachine(), 0, true, false,
                                    false, SiglusSaveLoad::Source::Quick));
  EXPECT_EQ(prompt->text, "quick load");
}

TEST_F(SiglusSaveCommandTest, SelectionRollbackUsesItsOwnWarningText) {
  // Selection rollback warns independently of the ordinary save/load alert
  // flag.
  runtime.gameexe->SetStringAt("WARNINGINFO.RETURNSEL_WARNING_STR",
                               "selection");
  runtime.Saves().manager().Capture("opaque runtime");
  runtime.Saves().manager().selection_points().Mark();
  runtime.global_config.system.alert_saveload = false;
  EXPECT_FALSE(runtime.Saves().Load(runtime.GameplayMachine(), 0, true, false,
                                    false, SiglusSaveLoad::Source::Selection));
  EXPECT_EQ(prompt->text, "selection");
}

TEST_F(SiglusSaveCommandTest, ConfiguredSaveMenuNestsInsideAnExistingExcall) {
  // Opening the save menu suspends and later returns to its existing EX caller.
  CheckNestedMenu(true);
}

TEST_F(SiglusSaveCommandTest, ConfiguredLoadMenuNestsInsideAnExistingExcall) {
  // Opening the load menu suspends and later returns to its existing EX caller.
  CheckNestedMenu(false);
}

TEST_F(SiglusSaveCommandTest, LocalResetPreservesEditedGlobalName) {
  // Local initialization reapplies configured names without rewinding globals.
  runtime.gameexe->SetStringAt("NAME.A", "default");
  runtime.memory->Write(StrBank::global_name, 0, "edited");
  runtime.ResetLocalMemory();
  EXPECT_EQ(runtime.memory->Read(StrBank::global_name, 0), "edited");
}

TEST_F(SiglusSaveCommandTest, PreservingBacklogKeepsDisplayedHistory) {
  // A menu return that preserves backlog retains the existing message entries.
  runtime.Saves();
  runtime.message_back->AddMessage("history");
  runtime.ResetLocalMemory(true);
  const auto entries = runtime.message_back->GetEntries();
  ASSERT_EQ(entries.size(), 1);
  EXPECT_EQ(entries.front().msg, "history");
}

TEST_F(SiglusSaveCommandTest, PreservingBacklogKeepsRestorableCheckpoints) {
  // Preserved backlog retains its selected checkpoint after local reset.
  runtime.gameexe->SetIntAt("MESSAGE_BACK_SAVE.CNT", 5);
  binding::Execute(*runtime.GameplayMachine().vm,
                   "loaded = savepoint(); A[0] = loaded;");
  runtime.DriveScripts();
  runtime.Saves().AppendMessage("message");
  const auto id = runtime.Saves().manager().active().id;
  runtime.Saves().SelectBacklog(id);
  runtime.ResetLocalMemory(true);
  ASSERT_TRUE(runtime.Saves().Load(runtime.GameplayMachine(), 0, false, false,
                                   false, SiglusSaveLoad::Source::Backlog));
  while (runtime.Saves().pending())
    runtime.DriveScripts();
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 1);
}

TEST_F(SiglusSaveCommandTest, NumericPlayTimeCommandWritesFiveBankReferences) {
  // Numeric output references survive parsing and compilation into live banks.
  binding::FakeScene scene{.scene_name = "playtime",
                           .command_name = "playtime"};
  scene.AppendElement({63, 199});
  for (int i = 0; i < 5; ++i)
    scene.AppendElement({25, -1, i});
  scene.AppendByte(ByteCode::Cmd);
  scene.AppendInt(0);
  scene.AppendInt(5);
  for (int i = 0; i < 5; ++i)
    scene.AppendInt(static_cast<int>(Type::IntRef));
  scene.AppendInt(0);
  scene.AppendInt(static_cast<int>(Type::None));
  scene.AddTo(*runtime.archive);
  auto& machine = runtime.GameplayMachine();
  (*machine.vm->globals_)["entry"] =
      machine.loader->ResolveSceneCommand("playtime", "playtime");
  runtime.total_play_time = (((2LL * 24 + 3) * 60 + 4) * 60 + 5) * 1000 + 6;
  binding::Execute(*machine.vm, "entry();");
  for (int i = 0; i < 5; ++i)
    EXPECT_EQ(runtime.memory->Read(IntBank::A, i), i + 2);
}

}  // namespace
}  // namespace libsiglus
