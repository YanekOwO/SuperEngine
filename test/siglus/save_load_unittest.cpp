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

#include "core/frame_action_manager.hpp"
#include "core/kidoku_table.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/save_load.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "m6/compiler_pipeline.hpp"
#include "siglus/fake_scene.hpp"
#include "vm/function.hpp"
#include "vm/object.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>

namespace {
namespace sr = serilang;
using namespace libsiglus;

class SiglusSaveLoadTest : public ::testing::Test {
 protected:
  SiglusRuntime runtime;
  std::filesystem::path directory;

  void Initialize(SiglusRuntime& host) {
    host.archive = std::make_shared<Archive>(
        std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
    host.gameexe = std::make_shared<Gameexe>();
    for (const char* line :
         {"SAVE.CNT = 3", "QUICK_SAVE.CNT = 2", "END_SAVE.CNT = 1",
          "INNER_SAVE.CNT = 2", "SEL_SAVE.CNT = 3"})
      host.gameexe->parseLine(line);
    host.save_pth = directory;
    host.kidoku_table = std::make_shared<KidokuTable>();
    host.binding_names = {"0_memory", "0_savepoint", "5_save_load", "flow",
                          "selpoint"};
    host.GameplayMachine().Initialize();
  }
  void SetUp() override {
    static std::atomic<unsigned> sequence{0};
    directory =
        std::filesystem::temp_directory_path() /
        ("rlvm-siglus-save-load-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()) +
         "-" + std::to_string(sequence++));
    std::filesystem::create_directories(directory);
    Initialize(runtime);
  }
  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
  }
  void Run(std::string source) {
    auto& vm = *runtime.GameplayMachine().vm;
    m6::CompilerPipeline compiler(vm.gc_);
    compiler.compile(
        m6::SourceBuffer::Create(std::move(source), "<save-test>"));
    ASSERT_TRUE(compiler.Ok()) << compiler.FormatErrors();
    auto* entry = vm.gc_->Allocate<sr::Function>(compiler.Get());
    vm.InvokeUntilBlocked(*entry, {});
  }
  void InitializePropertyList() {
    runtime.archive->prop_ = {
        {.form = Type::IntList, .size = 2, .name = "items"}};
    runtime.InitializeProperties();
    Run("retained = __builtin_load_prop(-1, 0); retained[0] = 7;");
  }
  void EnableCaptureFiles() {
    runtime.captures = std::make_shared<CaptureManager>(
        CaptureConfig{.screen_size = Size(2, 2)}, directory,
        CaptureManager::Render{}, CaptureManager::Load{});
    auto* bind = binding::SiglusBindingRegistry::Find("syscom_capture");
    ASSERT_NE(bind, nullptr);
    (*bind)(runtime.GameplayMachine());
    runtime.captures->CreateBuffer(Size(2, 2));
  }
  void Checkpoint() {
    Run("loaded = savepoint(); A[0] = loaded;");
    runtime.DriveScripts();
  }
  sr::Value Global(const std::string& name) {
    return runtime.GameplayMachine().vm->globals_->at(name);
  }
  void ResumeLoad() {
    runtime.DriveScripts();
    runtime.DriveScripts();
  }
};

TEST_F(SiglusSaveLoadTest, ExplicitCheckpointReturnsZeroDuringNormalExecution) {
  // A checkpoint yields for capture and then resumes the original result zero.
  Checkpoint();
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 0);
  EXPECT_TRUE(runtime.Saves().manager().HasCheckpoint());
}

TEST_F(SiglusSaveLoadTest, DiskLoadResumesWithTheCapturedResultOne) {
  // A disk save copies the checkpoint and resumes its return value as one.
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  runtime.memory->Write(IntBank::A, 0, 9);
  ASSERT_TRUE(
      runtime.Saves().Load(runtime.GameplayMachine(), 0, false, false, false));
  ResumeLoad();
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 1);
}

TEST_F(SiglusSaveLoadTest, LocalLoadPreservesCurrentGlobalBanks) {
  // Loading rewinds local execution while the current global value survives.
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  runtime.memory->Write(IntBank::G, 2, 83);
  ASSERT_TRUE(
      runtime.Saves().Load(runtime.GameplayMachine(), 0, false, false, false));
  ResumeLoad();
  EXPECT_EQ(runtime.memory->Read(IntBank::G, 2), 83);
}

TEST_F(SiglusSaveLoadTest, SlotSaveUsesTheEstablishedCheckpoint) {
  // Changes after capture do not become part of the saved local state.
  runtime.memory->Write(IntBank::A, 5, 31);
  Checkpoint();
  runtime.memory->Write(IntBank::A, 5, 72);
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  ASSERT_TRUE(
      runtime.Saves().Load(runtime.GameplayMachine(), 0, false, false, false));
  ResumeLoad();
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 5), 31);
}

TEST_F(SiglusSaveLoadTest, InnerLoadRestoresItsCopiedCheckpoint) {
  // An inner slot restores without requiring a disk slot.
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().manager().SaveInner(0));
  runtime.memory->Write(IntBank::A, 0, 17);
  ASSERT_TRUE(runtime.Saves().Load(runtime.GameplayMachine(), 0, false, false,
                                   false, SiglusSaveLoad::Source::Inner));
  ResumeLoad();
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 1);
}

TEST_F(SiglusSaveLoadTest, SelectionRollbackRestoresItsCopiedCheckpoint) {
  // Marked selection history restores the checkpoint preceding registration.
  Checkpoint();
  runtime.Saves().manager().selection_points().Mark();
  ASSERT_TRUE(runtime.Saves().Load(runtime.GameplayMachine(), 0, false, false,
                                   false, SiglusSaveLoad::Source::Selection));
  ResumeLoad();
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 1);
  EXPECT_FALSE(runtime.Saves().manager().selection_points().HasPoint());
}

TEST_F(SiglusSaveLoadTest, MissingSlotDoesNotReplaceTheLiveMachine) {
  // A failed existence check leaves the same machine and local data alive.
  auto* machine = &runtime.GameplayMachine();
  runtime.memory->Write(IntBank::A, 3, 61);
  EXPECT_FALSE(runtime.Saves().Load(*machine, 0, false, false, false));
  EXPECT_EQ(&runtime.GameplayMachine(), machine);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 3), 61);
}

TEST_F(SiglusSaveLoadTest, CorruptSlotDoesNotReplaceTheLiveMachine) {
  // Malformed bytes fail before cancellation or local-memory mutation.
  std::ofstream(runtime.Saves().manager().SlotPath(0), std::ios::binary)
      << "broken";
  auto* machine = &runtime.GameplayMachine();
  runtime.memory->Write(IntBank::A, 3, 61);
  EXPECT_FALSE(runtime.Saves().Load(*machine, 0, false, false, false));
  EXPECT_EQ(&runtime.GameplayMachine(), machine);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 3), 61);
}

TEST_F(SiglusSaveLoadTest, ClearingCheckpointDisablesSaving) {
  // Clearing a captured point removes its availability and disk-save source.
  Checkpoint();
  Run("clear_savepoint(); available = check_savepoint(); result = "
      "syscom.save(0,0,0);");
  EXPECT_EQ(Global("available"), sr::Value(0));
  EXPECT_EQ(Global("result"), sr::Value(0));
}

TEST_F(SiglusSaveLoadTest, SaveBindingsExposeConfiguredCounts) {
  // Both slot classes expose their configured count through native bindings.
  Run("normal = syscom.get_save_count(); quick = syscom.get_qsave_count();");
  EXPECT_EQ(Global("normal"), sr::Value(3));
  EXPECT_EQ(Global("quick"), sr::Value(2));
}

TEST_F(SiglusSaveLoadTest, HeaderCustomValuesUseLiveListOffset) {
  // Header values begin at zero while start addresses the live bank offset.
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  Run("A[4]=53; A[5]=81; syscom.set_save_value(0,A,4,2); "
      "syscom.get_save_value(0,B,7,2);");
  EXPECT_EQ(runtime.memory->Read(IntBank::B, 7), 53);
  EXPECT_EQ(runtime.memory->Read(IntBank::B, 8), 81);
}

TEST_F(SiglusSaveLoadTest, HeaderCustomValuesAcceptOwnedLists) {
  // Header transfers work with the new owned handle as well as bank handles.
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  Run("source = make_intlist(2); source.Set(0, 53, 81); "
      "destination = make_intlist(2); syscom.set_save_value(0,source,0,2); "
      "syscom.get_save_value(0,destination,0,2); value = destination[1];");
  EXPECT_EQ(Global("value"), sr::Value(81));
}

TEST_F(SiglusSaveLoadTest, OutputReferencesAcceptOwnedListsBanksAndBits) {
  // Native output references write through each supported integer handle kind.
  runtime.total_play_time = 90061001;
  Run("items = make_intlist(2); "
      "syscom.get_total_playtime([0,items,0],[0,A,1],[0,items.b8,4],"
      "[0,B,0],[0,C,0]); days = items[0]; minutes = items[1];");
  EXPECT_EQ(Global("days"), sr::Value(1));
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 1), 1);
  EXPECT_EQ(Global("minutes"), sr::Value(1));
  EXPECT_EQ(runtime.memory->Read(IntBank::B, 0), 1);
  EXPECT_EQ(runtime.memory->Read(IntBank::C, 0), 1);
}

TEST_F(SiglusSaveLoadTest, LoadedPropertyHandleAliasesRuntimeStorage) {
  // A disk load restores the property table and retained handle to the same
  // cell.
  InitializePropertyList();
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  Run("retained[0] = 99;");
  ASSERT_TRUE(
      runtime.Saves().Load(runtime.GameplayMachine(), 0, false, false, false));
  ResumeLoad();
  Run("value = retained[0]; retained[1] = 43;");
  EXPECT_EQ(Global("value"), sr::Value(7));
  EXPECT_EQ(std::get<PropertyStorage::Integers>(
                runtime.properties.Get(-1, 0).value)[1],
            43);
}

TEST_F(SiglusSaveLoadTest, PreparingLoadDoesNotMutateLivePropertyCell) {
  // Candidate graph hydration must not rewind the live cell before commit.
  InitializePropertyList();
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  Run("retained[0] = 99;");
  auto cell = runtime.properties.GetCell(-1, 0);
  ASSERT_TRUE(
      runtime.Saves().Load(runtime.GameplayMachine(), 0, false, false, false));
  EXPECT_EQ(runtime.properties.GetCell(-1, 0), cell);
  EXPECT_EQ(std::get<PropertyStorage::Integers>(cell->value)[0], 99);
}

TEST_F(SiglusSaveLoadTest, FailedGraphLoadPreservesLivePropertyCell) {
  // Reject trailing data after graph hydration; live property ownership
  // survives.
  InitializePropertyList();
  Checkpoint();
  auto& manager = runtime.Saves().manager();
  const auto payload = manager.MakePayload();
  auto bytes = payload.blobs.at(payload.active.id).runtime_state;
  manager.Capture(bytes + "trailing");
  ASSERT_TRUE(manager.SaveSlot(0));
  Run("retained[0] = 99;");
  auto cell = runtime.properties.GetCell(-1, 0);
  auto* machine = &runtime.GameplayMachine();
  EXPECT_FALSE(runtime.Saves().Load(*machine, 0, false, false, false));
  EXPECT_EQ(&runtime.GameplayMachine(), machine);
  EXPECT_EQ(runtime.properties.GetCell(-1, 0), cell);
  Run("value = retained[0];");
  EXPECT_EQ(Global("value"), sr::Value(99));
}

TEST_F(SiglusSaveLoadTest, CaptureFlagsCopyBanksIntoOwnedLists) {
  // Export bank values, then import into owned lists through the native
  // bindings.
  EnableCaptureFiles();
  Run(R"(A[1] = 37; S[1] = "saved";
    saved = await syscom.save_capture_buffer_to_file(0, ["flags", "bmp"],
      {"_2": A, "_3": 1, "_4": 1, "_5": S, "_6": 1, "_7": 1});
    ints = make_intlist(1); strs = make_strlist(1);
    loaded = await syscom.load_flag_from_capture_file(0, ["flags", "bmp"],
      {"_2": ints, "_4": 1, "_5": strs, "_7": 1});
    number = ints[0]; text = strs[0];)");
  EXPECT_EQ(Global("saved"), sr::Value(1));
  EXPECT_EQ(Global("loaded"), sr::Value(1));
  EXPECT_EQ(Global("number"), sr::Value(37));
  EXPECT_EQ(Global("text"), "saved");
}

TEST_F(SiglusSaveLoadTest, CaptureFlagsCopyOwnedListsIntoBanks) {
  // Export owned lists, then import into bank offsets through the same
  // bindings.
  EnableCaptureFiles();
  Run(R"(ints = make_intlist(1); ints[0] = 53;
    strs = make_strlist(1); strs[0] = "owned";
    await syscom.save_capture_buffer_to_file(0, ["flags", "bmp"],
      {"_2": ints, "_4": 1, "_5": strs, "_7": 1});
    loaded = await syscom.load_flag_from_capture_file(0, ["flags", "bmp"],
      {"_2": A, "_3": 2, "_4": 1, "_5": S, "_6": 2, "_7": 1});)");
  EXPECT_EQ(Global("loaded"), sr::Value(1));
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 2), 53);
  EXPECT_EQ(runtime.memory->Read(StrBank::S, 2), "owned");
}

TEST_F(SiglusSaveLoadTest, GlobalStateSurvivesFreshRuntimeInitialization) {
  // A fresh runtime reloads global flags/read state independently of slots.
  runtime.memory->Write(IntBank::G, 3, 27);
  runtime.kidoku_table->RecordKidoku(5, 8);
  runtime.total_play_time = 123456;
  runtime.global_config.message_speed = 37;
  ASSERT_TRUE(runtime.Saves().FlushGlobal());
  SiglusRuntime fresh;
  Initialize(fresh);
  fresh.Saves().LoadGlobal();
  EXPECT_EQ(fresh.memory->Read(IntBank::G, 3), 27);
  EXPECT_TRUE(fresh.kidoku_table->HasBeenRead(5, 8));
  EXPECT_EQ(fresh.total_play_time, 123456);
  EXPECT_EQ(fresh.global_config.message_speed, 37);
}

TEST_F(SiglusSaveLoadTest, MissingLoadSceneReportsAnError) {
  // No dumped game needs a built-in menu; missing configuration is explicit.
  EXPECT_THROW(runtime.Saves().OpenMenu(false), sr::RuntimeError);
}

TEST_F(SiglusSaveLoadTest, SaveMenuWithoutCheckpointDoesNothing) {
  // Save-menu eligibility rejects a missing checkpoint before scene lookup.
  EXPECT_NO_THROW(runtime.Saves().OpenMenu(true));
}

TEST_F(SiglusSaveLoadTest, TitleMetadataFreezesAtCheckpointCapture) {
  // The checkpoint title remains independent of later live scene-title changes.
  runtime.scene_title = "first";
  Checkpoint();
  runtime.scene_title = "later";
  EXPECT_EQ(runtime.Saves().manager().active().title, "first");
}

namespace {
void AddCheckpointScene(Archive& archive, std::string name) {
  binding::FakeScene scene{.scene_name = name, .command_name = name};
  scene.AppendElement({36});
  scene.AppendByte(ByteCode::Cmd);
  for (int value : {0, 0, 0, static_cast<int>(Type::Int)})
    scene.AppendInt(value);
  scene.AppendByte(ByteCode::Return);
  scene.AppendInt(1);
  scene.AppendInt(static_cast<int>(Type::Int));
  scene.AddTo(archive);
}
int AddBridgeScene(Archive& archive,
                   std::string name,
                   std::vector<Type> arguments = {}) {
  binding::FakeScene scene{
      .scene_name = name, .command_name = name, .args = std::move(arguments)};
  scene.AppendElement({39, 1});
  scene.AppendByte(ByteCode::Push);
  scene.AppendInt(static_cast<int>(Type::Int));
  scene.AppendInt(0);
  scene.AppendByte(ByteCode::Cmd);
  for (int value :
       {0, 1, static_cast<int>(Type::Int), 0, static_cast<int>(Type::String)})
    scene.AppendInt(value);
  scene.AppendByte(ByteCode::Return);
  scene.AppendInt(1);
  scene.AppendInt(static_cast<int>(Type::String));
  return scene.AddTo(archive);
}
void InstallBridge(SiglusMachine& machine, std::string body) {
  binding::Execute(*machine.vm, "fn test_bridge(n) { " + body + " }");
  auto* module = machine.vm->gc_->Allocate<sr::Module>("math");
  (*module->globals)["tostr"] = machine.vm->globals_->at("test_bridge");
  (*machine.vm->globals_)["math"] = module;
}
}  // namespace

TEST_F(SiglusSaveLoadTest, CheckpointInsideNestedExcallRestoresCallerChain) {
  // Two EX machines retain their pending return promises through a disk load.
  AddCheckpointScene(*runtime.archive, "inner");
  AddBridgeScene(*runtime.archive, "outer");
  runtime.configure_machine = [](SiglusMachine& machine) {
    InstallBridge(machine,
                  "return await __builtin_excall(\"inner\", 0, [], []);");
  };
  runtime.GameplayMachine().Initialize();
  Run("result = await __builtin_excall(\"outer\", 0, [], []); A[0] = result;");
  runtime.DriveScripts();
  runtime.DriveScripts();
  ASSERT_TRUE(runtime.IsExcall());
  ASSERT_TRUE(runtime.Saves().manager().HasCheckpoint());
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  for (int i = 0; i < 5; ++i)
    runtime.DriveScripts();
  ASSERT_FALSE(runtime.IsExcall());
  ASSERT_EQ(runtime.memory->Read(IntBank::A, 0), 0);
  ASSERT_TRUE(
      runtime.Saves().Load(runtime.GameplayMachine(), 0, false, false, false));
  for (int i = 0; i < 8; ++i)
    runtime.DriveScripts();
  EXPECT_FALSE(runtime.IsExcall());
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 1);
}

TEST_F(SiglusSaveLoadTest, CallbackCheckpointCapturesCompletedEffects) {
  // A callback finishes before capture; load retains its effects without
  // restoring a callback fiber or changing its savepoint result.
  runtime.stage = std::make_unique<Stage>();
  runtime.counter_list = std::make_shared<CounterList>(0);
  runtime.frame_action_manager = std::make_shared<FrameActionManager>(
      *runtime.stage, runtime.local_config, 1);
  runtime.binding_names.push_back("1_frame_action");
  const int scene =
      AddBridgeScene(*runtime.archive, "callback", {Type::FrameAction});
  runtime.configure_machine = [](SiglusMachine& machine) {
    InstallBridge(machine,
                  "loaded = savepoint(); A[1] = loaded; return loaded;");
  };
  runtime.GameplayMachine().Initialize();
  auto action = std::make_shared<FrameAction>();
  runtime.DispatchFrameAction(0, {action, {}},
                              ScriptCall{.target = CmdTarget{scene, 5}});
  runtime.DriveScripts();
  ASSERT_TRUE(runtime.Saves().manager().HasCheckpoint());
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  runtime.Update(std::chrono::milliseconds(0), std::chrono::milliseconds(0));
  ASSERT_EQ(runtime.memory->Read(IntBank::A, 1), 0);
  ASSERT_TRUE(
      runtime.Saves().Load(runtime.GameplayMachine(), 0, false, false, false));
  ResumeLoad();
  runtime.Update(std::chrono::milliseconds(0), std::chrono::milliseconds(0));
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 1), 0);
}
TEST_F(SiglusSaveLoadTest, CheckpointWaitsForCallbackFrameWait) {
  // A callback awaits a frame after requesting a checkpoint. Capture waits for
  // its effects, and does not invoke the repeating action again while draining.
  runtime.stage = std::make_unique<Stage>();
  runtime.counter_list = std::make_shared<CounterList>(0);
  runtime.frame_action_manager = std::make_shared<FrameActionManager>(
      *runtime.stage, runtime.local_config, 1);
  runtime.binding_names.insert(runtime.binding_names.end(),
                               {"1_frame_action", "frame"});
  const int scene =
      AddBridgeScene(*runtime.archive, "callback", {Type::FrameAction});
  runtime.configure_machine = [](SiglusMachine& machine) {
    InstallBridge(machine,
                  "A[0] = A[0] + 1; savepoint(); await frame(); "
                  "A[1] = 73; return 0;");
  };
  runtime.GameplayMachine().Initialize();
  auto& manager = *runtime.frame_action_manager;
  manager.Start({manager.global_action(), {}}, -1, false,
                ScriptCall{.target = CmdTarget{scene, 5}});
  runtime.Update(std::chrono::milliseconds(10), std::chrono::milliseconds(10));
  runtime.DriveScripts();
  ASSERT_FALSE(runtime.Saves().manager().HasCheckpoint());
  ASSERT_TRUE(runtime.Saves().CheckpointPending());
  runtime.Update(std::chrono::milliseconds(10), std::chrono::milliseconds(10));
  runtime.DriveScripts();
  EXPECT_TRUE(runtime.Saves().manager().HasCheckpoint());
  EXPECT_FALSE(runtime.Saves().CheckpointPending());
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 1);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 1), 73);
  EXPECT_EQ(manager.global_action()->GetCounter()->Get(), 20);
}

TEST_F(SiglusSaveLoadTest, GameplayCheckpointKeepsItsResultWhileCallbackWaits) {
  // An already-blocked callback delays a gameplay savepoint. Its completed
  // effects and the gameplay continuation's loaded result survive a disk load.
  runtime.stage = std::make_unique<Stage>();
  runtime.frame_action_manager = std::make_shared<FrameActionManager>(
      *runtime.stage, runtime.local_config, 1);
  runtime.binding_names.insert(runtime.binding_names.end(),
                               {"1_frame_action", "frame"});
  const int scene =
      AddBridgeScene(*runtime.archive, "callback", {Type::FrameAction});
  runtime.configure_machine = [](SiglusMachine& machine) {
    InstallBridge(machine, "await frame(); A[1] = 73; return 0;");
  };
  runtime.GameplayMachine().Initialize();
  runtime.DispatchFrameAction(0, {std::make_shared<FrameAction>(), {}},
                              ScriptCall{.target = CmdTarget{scene, 5}});
  Run("loaded = savepoint(); A[0] = loaded;");
  runtime.DriveScripts();
  ASSERT_FALSE(runtime.Saves().manager().HasCheckpoint());
  runtime.Update(std::chrono::milliseconds(0), std::chrono::milliseconds(0));
  runtime.DriveScripts();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  runtime.memory->Write(IntBank::A, 1, 0);
  ASSERT_TRUE(
      runtime.Saves().Load(runtime.GameplayMachine(), 0, false, false, false));
  ResumeLoad();
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 1);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 1), 73);
}

TEST_F(SiglusSaveLoadTest, CallbackExcallFinishesBeforeCheckpoint) {
  // A callback requests a checkpoint and awaits an EX child. The child must
  // keep running while capture is deferred, then disappear from the save.
  runtime.stage = std::make_unique<Stage>();
  runtime.frame_action_manager = std::make_shared<FrameActionManager>(
      *runtime.stage, runtime.local_config, 1);
  runtime.binding_names.push_back("1_frame_action");
  const int scene =
      AddBridgeScene(*runtime.archive, "callback", {Type::FrameAction});
  AddBridgeScene(*runtime.archive, "child");
  runtime.configure_machine = [](SiglusMachine& machine) {
    InstallBridge(machine,
                  machine.callback_owner >= 0
                      ? "savepoint(); A[1] = await "
                        "__builtin_excall(\"child\", 0, [], []); return 0;"
                      : "return 73;");
  };
  runtime.GameplayMachine().Initialize();
  runtime.DispatchFrameAction(0, {std::make_shared<FrameAction>(), {}},
                              ScriptCall{.target = CmdTarget{scene, 5}});
  for (int i = 0; i < 4; ++i) {
    runtime.DriveScripts();
    runtime.Update(std::chrono::milliseconds(0), std::chrono::milliseconds(0));
  }
  ASSERT_TRUE(runtime.Saves().manager().HasCheckpoint());
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  runtime.memory->Write(IntBank::A, 1, 0);
  ASSERT_TRUE(
      runtime.Saves().Load(runtime.GameplayMachine(), 0, false, false, false));
  ResumeLoad();
  EXPECT_FALSE(runtime.IsExcall());
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 1), 73);
}

TEST_F(SiglusSaveLoadTest, CallbackCheckpointFinishesTheWholeTraversal) {
  // The first callback requests a checkpoint; later channels must complete
  // before capture rather than being saved as unfinished traversal jobs.
  runtime.stage = std::make_unique<Stage>();
  runtime.frame_action_manager = std::make_shared<FrameActionManager>(
      *runtime.stage, runtime.local_config, 1);
  runtime.binding_names.push_back("1_frame_action");
  const int scene =
      AddBridgeScene(*runtime.archive, "callback", {Type::FrameAction});
  runtime.configure_machine = [](SiglusMachine& machine) {
    InstallBridge(machine, "savepoint(); A[1] = A[1] + 1; return 0;");
  };
  runtime.GameplayMachine().Initialize();
  auto& manager = *runtime.frame_action_manager;
  const ScriptCall call{.target = CmdTarget{scene, 5}};
  manager.Start({manager.global_action(), {}}, -1, false, call);
  manager.Start({TouchFrameAction(manager.global_channels()[0]), {}}, -1, false,
                call);
  runtime.Update(std::chrono::milliseconds(0), std::chrono::milliseconds(0));
  runtime.DriveScripts();
  ASSERT_EQ(runtime.memory->Read(IntBank::A, 1), 2);
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  ASSERT_TRUE(
      runtime.Saves().Load(runtime.GameplayMachine(), 0, false, false, false));
  ResumeLoad();
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 1), 4);
}

TEST_F(SiglusSaveLoadTest, ChangedGameAssetsDoNotReplaceTheLiveMachine) {
  // Even a changed scene that is not on the saved call stack rejects the slot.
  Checkpoint();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  SiglusRuntime fresh;
  Initialize(fresh);
  binding::FakeScene{.scene_name = "different", .command_name = "different"}
      .AddTo(*fresh.archive);
  auto* original = &fresh.GameplayMachine();
  EXPECT_FALSE(fresh.Saves().Load(*original, 0, false, false, false));
  EXPECT_EQ(&fresh.GameplayMachine(), original);
}

TEST_F(SiglusSaveLoadTest, FinalCallbackCompletesBeforeCheckpoint) {
  // A final callback records its final flag before capture, and load keeps
  // the completed action inactive while resuming its gameplay caller.
  runtime.stage = std::make_unique<Stage>();
  runtime.counter_list = std::make_shared<CounterList>(0);
  runtime.frame_action_manager = std::make_shared<FrameActionManager>(
      *runtime.stage, runtime.local_config, 1);
  runtime.binding_names.push_back("1_frame_action");
  const int scene =
      AddBridgeScene(*runtime.archive, "final", {Type::FrameAction});
  runtime.configure_machine = [](SiglusMachine& machine) {
    InstallBridge(machine,
                  "loaded = savepoint(); A[0] = loaded; "
                  "A[1] = frame_action.is_end_action(); return 0;");
  };
  runtime.GameplayMachine().Initialize();
  auto& manager = *runtime.frame_action_manager;
  manager.Start({manager.global_action(), {}}, -1, false,
                ScriptCall{.target = CmdTarget{scene, 5}});
  Run("await frame_action.end(); A[2] = 97;");
  ASSERT_FALSE(manager.global_action()->end_action);
  runtime.DriveScripts();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  ASSERT_TRUE(
      runtime.Saves().Load(runtime.GameplayMachine(), 0, false, false, false));
  ResumeLoad();
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 0);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 1), 1);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 2), 97);
  EXPECT_FALSE(manager.global_action()->end_action);
}

TEST_F(SiglusSaveLoadTest, LoadInvokesFreshCallbackWithSavedCounter) {
  // Loading invokes an active command from its entry with the saved counter.
  runtime.stage = std::make_unique<Stage>();
  runtime.counter_list = std::make_shared<CounterList>(0);
  runtime.frame_action_manager = std::make_shared<FrameActionManager>(
      *runtime.stage, runtime.local_config, 1);
  runtime.binding_names.push_back("1_frame_action");
  const int scene =
      AddBridgeScene(*runtime.archive, "regular", {Type::FrameAction});
  runtime.configure_machine = [](SiglusMachine& machine) {
    InstallBridge(
        machine,
        "A[0] = A[0] + 1; loaded = savepoint(); A[1] = loaded; return 0;");
  };
  runtime.GameplayMachine().Initialize();
  auto& manager = *runtime.frame_action_manager;
  manager.Start({manager.global_action(), {}}, -1, false,
                ScriptCall{.target = CmdTarget{scene, 5}});
  runtime.Update(std::chrono::milliseconds(10), std::chrono::milliseconds(10));
  runtime.DriveScripts();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  manager.global_action()->counter->Set(90);
  ASSERT_TRUE(
      runtime.Saves().Load(runtime.GameplayMachine(), 0, false, false, false));
  ResumeLoad();
  EXPECT_EQ(manager.global_action()->counter->Get(), 10);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 2);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 1), 0);
}

TEST_F(SiglusSaveLoadTest, ReplacementStartCompletesBeforeCheckpoint) {
  // A savepoint in the old final callback captures the installed replacement.
  runtime.stage = std::make_unique<Stage>();
  runtime.counter_list = std::make_shared<CounterList>(0);
  runtime.frame_action_manager = std::make_shared<FrameActionManager>(
      *runtime.stage, runtime.local_config, 1);
  runtime.binding_names.push_back("1_frame_action");
  const int old_scene =
      AddBridgeScene(*runtime.archive, "old", {Type::FrameAction});
  const int new_scene =
      AddBridgeScene(*runtime.archive, "new", {Type::FrameAction});
  runtime.configure_machine = [](SiglusMachine& machine) {
    InstallBridge(machine,
                  "loaded = savepoint(); A[0] = loaded; "
                  "A[1] = frame_action.is_end_action(); return 0;");
  };
  runtime.GameplayMachine().Initialize();
  auto& manager = *runtime.frame_action_manager;
  manager.Start({manager.global_action(), {}}, -1, false,
                ScriptCall{.target = CmdTarget{old_scene, 5}});
  manager.Start({manager.global_action(), {}}, 200, false,
                ScriptCall{.target = CmdTarget{new_scene, 5}});
  runtime.DriveScripts();
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  ASSERT_TRUE(
      runtime.Saves().Load(runtime.GameplayMachine(), 0, false, false, false));
  ResumeLoad();
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 0);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 1), 0);
  EXPECT_TRUE(manager.global_action()->IsActive());
  EXPECT_EQ(manager.global_action()->end_time, 200);
  EXPECT_EQ(std::get<CmdTarget>(manager.global_action()->call.target).scene_id,
            new_scene);
}

TEST_F(SiglusSaveLoadTest, ExcallFreeCompletesBeforeFinalCallbackCheckpoint) {
  // Freeing EX resources completes final effects before capture; loading
  // preserves the released state and the caller can return normally.
  runtime.stage = std::make_unique<Stage>();
  runtime.counter_list = std::make_shared<CounterList>(0);
  runtime.frame_action_manager = std::make_shared<FrameActionManager>(
      *runtime.stage, runtime.local_config, 1);
  runtime.binding_names.insert(runtime.binding_names.end(),
                               {"1_frame_action", "3_excall"});
  AddBridgeScene(*runtime.archive, "free_outer");
  const int final_scene =
      AddBridgeScene(*runtime.archive, "free_final", {Type::FrameAction});
  runtime.configure_machine = [](SiglusMachine& machine) {
    InstallBridge(machine,
                  machine.callback_owner == 1
                      ? "loaded = savepoint(); A[0] = loaded; return 0;"
                      : "await excall.free(); return 23;");
  };
  runtime.GameplayMachine().Initialize();
  runtime.excall->Alloc({.frame_action_count = 1});
  auto& manager = *runtime.excall->actions;
  manager.Start({manager.global_action(), {}}, -1, false,
                ScriptCall{.target = CmdTarget{final_scene, 5}});
  Run("result = await __builtin_excall(\"free_outer\", 0, [], []); A[3] = "
      "result;");
  for (int i = 0; i < 4 && !runtime.Saves().manager().HasCheckpoint(); ++i)
    runtime.DriveScripts();
  ASSERT_TRUE(runtime.Saves().manager().HasCheckpoint());
  ASSERT_TRUE(runtime.Saves().Save(0, false, false));
  for (int i = 0; i < 6; ++i)
    runtime.DriveScripts();
  ASSERT_FALSE(runtime.excall->allocated);
  ASSERT_TRUE(
      runtime.Saves().Load(runtime.GameplayMachine(), 0, false, false, false));
  for (int i = 0; i < 12; ++i)
    runtime.DriveScripts();
  EXPECT_FALSE(runtime.excall->allocated);
  EXPECT_FALSE(runtime.excall->releasing);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 0);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 3), 23);
}

}  // namespace
