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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "siglus/fake_scene.hpp"
#include "vm/value.hpp"

#include <memory>
#include <string>

namespace siglus_test {
using namespace libsiglus;
namespace sr = serilang;
using ::testing::ElementsAre;

class SiglusSelpointTest : public ::testing::Test {
 protected:
  SiglusRuntime runtime;

  void SetUp() override {
    runtime.archive = std::make_shared<Archive>(
        std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
    runtime.binding_names = {"0_memory", "flow", "selpoint"};
    runtime.selection_points = SelectionPoint(3);
    runtime.GameplayMachine().Initialize();
  }

  void Run(const std::string& source) {
    binding::Execute(*runtime.GameplayMachine().vm, source);
  }

  sr::Value Global(const char* name) {
    return runtime.GameplayMachine().vm->globals_->at(name);
  }

  // Exercise serialized commands through the actual loader and recompiler.
  int AddElementScene(int element) {
    binding::FakeScene scene{.scene_name = "point", .command_name = "point"};
    scene.AppendElement({element});
    scene.AppendByte(ByteCode::Cmd);
    for (int value :
         {0, 0, 0, static_cast<int>(element == 110 ? Type::Int : Type::None)})
      scene.AppendInt(value);
    if (element == 110) {
      scene.AppendByte(ByteCode::Return);
      scene.AppendInt(1);
      scene.AppendInt(static_cast<int>(Type::Int));
    }
    return scene.AddTo(*runtime.archive);
  }

  sr::Value RunElement(int element) {
    AddElementScene(element);
    auto& machine = runtime.GameplayMachine();
    (*machine.vm->globals_)["entry"] =
        machine.loader->ResolveSceneCommand("point", "point");
    Run("result = entry();");
    return Global("result");
  }
};

TEST_F(SiglusSelpointTest, NoSnapshotIsAdvertisedBeforeCaptureExists) {
  // Stubbed savepoint capture cannot make marking or stocking usable history.
  Run("savepoint(); selpoint(); stack_selpoint(); drop_selpoint(); "
      "available = check_selpoint();");
  EXPECT_EQ(Global("available"), sr::Value(0));
}

TEST_F(SiglusSelpointTest, NativeCommandsShareTheHistoryOperations) {
  // Recording a supplied snapshot enables the query; clearing disables it.
  runtime.selection_points.SetCurrentSnapshot("point");
  Run("marked = selpoint(); before = check_selpoint(); "
      "cleared = clear_selpoint(); after = check_selpoint();");
  EXPECT_EQ(Global("marked"), sr::nil);
  EXPECT_EQ(Global("cleared"), sr::nil);
  EXPECT_EQ(Global("before"), sr::Value(1));
  EXPECT_EQ(Global("after"), sr::Value(0));
}

TEST_F(SiglusSelpointTest, NativeStockIsConsumedByDrop) {
  // Stock commands are void, and two drops append only the stocked source.
  runtime.selection_points.SetCurrentSnapshot("stocked");
  Run("stocked = stack_selpoint();");
  runtime.selection_points.SetCurrentSnapshot("later");
  Run("dropped = drop_selpoint(); drop_selpoint();");
  EXPECT_EQ(Global("stocked"), sr::nil);
  EXPECT_EQ(Global("dropped"), sr::nil);
  EXPECT_THAT(runtime.selection_points.history(), ElementsAre("stocked"));
}

TEST_F(SiglusSelpointTest, RecompiledSelpointRecordsTheCurrentSnapshot) {
  // Element one must reach the native marker through a compiled scene.
  runtime.selection_points.SetCurrentSnapshot("point");
  EXPECT_EQ(RunElement(1), sr::nil);
  EXPECT_THAT(runtime.selection_points.history(), ElementsAre("point"));
}

TEST_F(SiglusSelpointTest, RecompiledCheckSelpointReturnsAnInteger) {
  // A compiled existence query returns the integer one for populated history.
  runtime.selection_points.SetCurrentSnapshot("point");
  runtime.selection_points.Mark();
  EXPECT_EQ(RunElement(110), sr::Value(1));
}

TEST_F(SiglusSelpointTest, RecompiledClearSelpointRemovesHistory) {
  // Element 111 clears an existing point without requiring script arguments.
  runtime.selection_points.SetCurrentSnapshot("point");
  runtime.selection_points.Mark();
  EXPECT_EQ(RunElement(111), sr::nil);
  EXPECT_FALSE(runtime.selection_points.HasPoint());
}

TEST_F(SiglusSelpointTest, RecompiledStackSelpointCopiesTheCurrentSnapshot) {
  // The compiled stock command saves data that survives a source replacement.
  runtime.selection_points.SetCurrentSnapshot("stocked");
  EXPECT_EQ(RunElement(149), sr::nil);
  runtime.selection_points.SetCurrentSnapshot("later");
  runtime.selection_points.Drop();
  EXPECT_THAT(runtime.selection_points.history(), ElementsAre("stocked"));
}

TEST_F(SiglusSelpointTest, RecompiledDropSelpointConsumesStock) {
  // Element 150 appends the stock exactly once through the compiled path.
  runtime.selection_points.SetCurrentSnapshot("stocked");
  runtime.selection_points.Stack();
  EXPECT_EQ(RunElement(150), sr::nil);
  runtime.selection_points.Drop();
  EXPECT_THAT(runtime.selection_points.history(), ElementsAre("stocked"));
}

TEST_F(SiglusSelpointTest, NewMachineBindingsRetainSharedHistory) {
  // Initializing a second VM preserves existing points and shares mutations.
  runtime.selection_points.SetCurrentSnapshot("point");
  Run("selpoint();");
  SiglusMachine other(runtime);
  other.Initialize();
  binding::Execute(*other.vm, "observed = check_selpoint(); clear_selpoint();");
  EXPECT_EQ(other.vm->globals_->at("observed"), sr::Value(1));
  Run("available = check_selpoint();");
  EXPECT_EQ(Global("available"), sr::Value(0));
}

TEST_F(SiglusSelpointTest, CallbackBindingsUseTheSameHistory) {
  // Both callback scopes see existing history and write to that shared state.
  runtime.selection_points.SetCurrentSnapshot("point");
  Run("selpoint();");
  for (int owner : {0, 1}) {
    SiglusMachine callback(runtime, owner);
    callback.Initialize();
    binding::Execute(*callback.vm, "observed = check_selpoint(); selpoint();");
    EXPECT_EQ(callback.vm->globals_->at("observed"), sr::Value(1));
  }
  EXPECT_THAT(runtime.selection_points.history(),
              ElementsAre("point", "point", "point"));
}

TEST_F(SiglusSelpointTest, ExcallEntryAndReturnPreserveSnapshots) {
  // An EX scene records into existing history while source and stock survive.
  runtime.selection_points.SetCurrentSnapshot("previous");
  Run("selpoint(); stack_selpoint();");
  runtime.selection_points.SetCurrentSnapshot("current");
  const int scene = AddElementScene(1);
  ASSERT_TRUE(runtime.OpenExcall(ScriptCall{.target = SceneTarget{scene, 0}}));
  runtime.DriveScripts();
  EXPECT_FALSE(runtime.IsExcall());
  Run("drop_selpoint();");
  EXPECT_THAT(runtime.selection_points.history(),
              ElementsAre("previous", "current", "previous"));
}

TEST_F(SiglusSelpointTest, ReleasingExcallResourcesPreservesSnapshots) {
  // Freeing allocated EX resources must not clear shared history or stock.
  runtime.InitializeExcall();
  runtime.excall->Alloc({});
  runtime.selection_points.SetCurrentSnapshot("point");
  Run("selpoint(); stack_selpoint();");
  runtime.FreeExcall();
  Run("drop_selpoint(); selpoint();");
  EXPECT_THAT(runtime.selection_points.history(),
              ElementsAre("point", "point", "point"));
}

TEST_F(SiglusSelpointTest, LocalResetClearsAllSnapshots) {
  // A full local reset clears history, the source, and the temporary stock.
  runtime.selection_points.SetCurrentSnapshot("point");
  Run("selpoint(); stack_selpoint();");
  runtime.ResetLocalMemory();
  Run("selpoint(); drop_selpoint(); available = check_selpoint();");
  EXPECT_EQ(Global("available"), sr::Value(0));
}

}  // namespace siglus_test
