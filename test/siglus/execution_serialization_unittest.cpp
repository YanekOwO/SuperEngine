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

#include "libsiglus/execution_serialization.hpp"
#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/counter.hpp"
#include "libsiglus/bindings/memory.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "siglus/execution_test_helpers.hpp"
#include "siglus/fake_scene.hpp"
#include "srbind/srbind.hpp"

#include <gtest/gtest.h>

namespace siglus_test {
namespace sr = serilang;
using namespace libsiglus;

class SiglusExecutionSerializationTest : public ::testing::Test {
 protected:
  SiglusRuntime source, target;

  void Initialize(SiglusRuntime& runtime) {
    runtime.archive = std::make_shared<Archive>(
        std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
    runtime.counter_list = std::make_shared<CounterList>(2);
    runtime.binding_names = {"0_memory", "0_savepoint", "1_counter", "flow"};
    runtime.GameplayMachine().Initialize();
    auto& machine = runtime.GameplayMachine();
    srbind::module_ module(machine.vm->gc_.get(), machine.vm->globals_.get());
    module.def("pause_execution", [](sr::VM& vm) {
      vm.RequestHostYield();
      return 0;
    });
    RememberBindingSymbols(machine);
  }

  void SetUp() override {
    Initialize(source);
    Initialize(target);
  }

  void Run(SiglusRuntime& runtime, std::string text) {
    binding::Execute(*runtime.GameplayMachine().vm, std::move(text));
  }

  sr::Value Global(SiglusRuntime& runtime, const char* name) {
    return runtime.GameplayMachine().vm->globals_->at(name);
  }

  void InitializePropertyLists() {
    source.archive->prop_ = {
        {.form = Type::IntList, .size = 3, .name = "numbers"},
        {.form = Type::StrList, .size = 2, .name = "strings"}};
    source.InitializeProperties();
  }

  void RoundTripProperties() {
    ExecutionWriter writer;
    ExecutionReader reader;
    auto restored = serialization_test::RoundTripMachineResource(
        source.properties, writer, reader, source.GameplayMachine(),
        target.GameplayMachine());
    target.properties = std::move(restored.first);
    PrepareNativeWaits(target.GameplayMachine(), reader, restored.second);
    reader.FinishRestore();
  }

  void RoundTrip(bool loaded_result = false) {
    ExecutionWriter writer;
    auto& original = source.GameplayMachine();
    const auto bytes = serialization_test::WriteMachine(
        writer, original,
        loaded_result ? original.vm->fibres_.front() : nullptr);
    ExecutionReader reader;
    auto waits = serialization_test::ReadMachine(
        reader, target.GameplayMachine(), bytes);
    PrepareNativeWaits(target.GameplayMachine(), reader, waits);
    reader.FinishRestore();
  }
};

TEST_F(SiglusExecutionSerializationTest, ResumesAfterTheCheckpointCall) {
  // The native call yields before assignment; only the restored result is one.
  Run(source, "value = pause_execution(); result = value + 7;");
  RoundTrip(true);
  source.GameplayMachine().vm->RunReadyUntilBlocked();
  target.GameplayMachine().vm->RunReadyUntilBlocked();
  EXPECT_EQ(Global(source, "result"), sr::Value(7));
  EXPECT_EQ(Global(target, "result"), sr::Value(8));
}

TEST_F(SiglusExecutionSerializationTest, RetainsAliasedOwnedListStorage) {
  // Both names reference the same native list after the fresh VM is prepared.
  Run(source,
      "items = make_intlist(3); alias = items; items[0] = 7; "
      "pause_execution(); items[0] = 12; result = alias[0];");
  RoundTrip();
  target.GameplayMachine().vm->RunReadyUntilBlocked();
  EXPECT_EQ(Global(target, "result"), sr::Value(12));
}

TEST_F(SiglusExecutionSerializationTest, ReconnectsBankAliasToRestoredMemory) {
  // An alias reconnects to the target bank rather than a private value copy.
  Run(source, "bank = A; pause_execution(); bank[4] = 91; result = A[4];");
  RoundTrip();
  target.GameplayMachine().vm->RunReadyUntilBlocked();
  EXPECT_EQ(Global(target, "result"), sr::Value(91));
  EXPECT_EQ(target.memory->Read(IntBank::A, 4), 91);
}

TEST_F(SiglusExecutionSerializationTest, RejectsReachableBitView) {
  // Retain a bit view across a checkpoint; unsupported native state must fail.
  Run(source, "items = make_intlist(1); bits = items.b1; pause_execution();");
  ExecutionWriter writer;
  try {
    serialization_test::WriteMachine(writer, source.GameplayMachine());
    FAIL() << "A reachable bit view must not be serialized";
  } catch (const std::runtime_error& error) {
    EXPECT_STREQ(error.what(), "native class has no serializer");
  }
}

TEST_F(SiglusExecutionSerializationTest, ConsumedBitViewDoesNotPreventSaving) {
  // An immediate bit write leaves only its list reachable at the checkpoint.
  Run(source,
      "items = make_intlist(1); items.b4[0] = 7; pause_execution(); "
      "result = items[0];");
  RoundTrip();
  target.GameplayMachine().vm->RunReadyUntilBlocked();
  EXPECT_EQ(Global(target, "result"), sr::Value(7));
}

TEST_F(SiglusExecutionSerializationTest, RetainsAliasedStringListStorage) {
  // Distinct native wrappers own one cell; loading must retain that alias.
  Run(source, "first = make_strlist(2); first[0] = \"saved\";");
  auto& machine = source.GameplayMachine();
  auto cell = Global(source, "first")
                  .Get<sr::NativeInstance*>()
                  ->GetForeign<binding::StrListHandle>()
                  ->cell;
  (*machine.vm->globals_)["second"] =
      machine.class_registry.MakeInst<binding::StrListHandle>(cell);
  Run(source, "pause_execution(); first[0] = \"loaded\"; result = second[0];");
  RoundTrip();
  target.GameplayMachine().vm->RunReadyUntilBlocked();
  EXPECT_EQ(Global(target, "result"), "loaded");
  EXPECT_EQ(std::get<PropertyStorage::Strings>(cell->value)[0], "saved");
}

TEST_F(SiglusExecutionSerializationTest, PropertyHandlesShareRestoredCell) {
  // Independently loaded property handles and the runtime root share one cell.
  InitializePropertyLists();
  Run(source,
      "first = __builtin_load_prop(-1, 0); "
      "second = __builtin_load_prop(-1, 0); first[0] = 7; "
      "pause_execution(); second[0] = 19; result = first[0];");
  RoundTripProperties();
  auto first = Global(target, "first")
                   .Get<sr::NativeInstance*>()
                   ->GetForeign<binding::IntListHandle>()
                   ->cell;
  auto second = Global(target, "second")
                    .Get<sr::NativeInstance*>()
                    ->GetForeign<binding::IntListHandle>()
                    ->cell;
  EXPECT_EQ(first, second);
  EXPECT_EQ(first, target.properties.GetCell(-1, 0));
  EXPECT_NE(first, source.properties.GetCell(-1, 0));
  target.GameplayMachine().vm->RunReadyUntilBlocked();
  EXPECT_EQ(Global(target, "result"), sr::Value(19));
  EXPECT_EQ(std::get<PropertyStorage::Integers>(
                source.properties.Get(-1, 0).value)[0],
            7);
}

TEST_F(SiglusExecutionSerializationTest,
       RestoredPropertyHandleFollowsAssignment) {
  // Assignment after load updates the shared cell while retaining its size
  // metadata.
  InitializePropertyLists();
  Run(source,
      "retained = __builtin_load_prop(-1, 1); pause_execution(); "
      "replacement = make_strlist(1); replacement[0] = \"new\"; "
      "__builtin_store_prop(-1, 1, replacement); result = retained[0]; "
      "retained.init(); size = retained.size();");
  RoundTripProperties();
  target.GameplayMachine().vm->RunReadyUntilBlocked();
  EXPECT_EQ(Global(target, "result"), "new");
  EXPECT_EQ(Global(target, "size"), sr::Value(2));
}

TEST_F(SiglusExecutionSerializationTest,
       RestoresHandleWithChangedPropertyType) {
  // A stale list handle remains an access error even when the cell is
  // serialized.
  InitializePropertyLists();
  Run(source,
      "retained = __builtin_load_prop(-1, 0); "
      "__builtin_store_prop(-1, 0, 7); pause_execution(); caught = 0; "
      "try { retained.size(); } catch (error) { caught = 1; }");
  RoundTripProperties();
  target.GameplayMachine().vm->RunReadyUntilBlocked();
  EXPECT_EQ(Global(target, "caught"), sr::Value(1));
}

TEST_F(SiglusExecutionSerializationTest,
       PropertyCellIsSharedAcrossMachineGraphs) {
  // Two VM heaps save references to one property; both restored graphs alias
  // it.
  InitializePropertyLists();
  SiglusMachine source_other(source), target_other(target);
  source_other.Initialize();
  target_other.Initialize();
  Run(source, "retained = __builtin_load_prop(-1, 0);");
  binding::Execute(*source_other.vm, "retained = __builtin_load_prop(-1, 0);");
  std::stringstream bytes;
  ExecutionWriter writer;
  {
    boost::archive::binary_oarchive archive(bytes);
    SerializationContext::Init(archive, MakeSerializationContext(source));
    archive << source.properties;
    writer.WriteMachine(archive, source.GameplayMachine());
    writer.WriteMachine(archive, source_other);
  }
  ExecutionReader reader;
  {
    boost::archive::binary_iarchive archive(bytes);
    SerializationContext::Init(archive, MakeSerializationContext(target));
    archive >> target.properties;
    reader.ReadMachine(archive, target.GameplayMachine());
    reader.ReadMachine(archive, target_other);
  }
  reader.FinishRestore();
  Run(target, "retained[1] = 37;");
  binding::Execute(*target_other.vm, "result = retained[1];");
  EXPECT_EQ(target_other.vm->globals_->at("result"), sr::Value(37));
  EXPECT_EQ(std::get<PropertyStorage::Integers>(
                target.properties.Get(-1, 0).value)[1],
            37);
}

TEST_F(SiglusExecutionSerializationTest, BankAliasSurvivesGlobalReplacement) {
  // Overwriting a bank global cannot turn an existing bank alias into a copy.
  Run(source, "bank = S; S = nil; pause_execution(); bank[1] = \"loaded\";");
  RoundTrip();
  target.GameplayMachine().vm->RunReadyUntilBlocked();
  EXPECT_EQ(target.memory->Read(StrBank::S, 1), "loaded");
}

TEST_F(SiglusExecutionSerializationTest, RestoresStackBanksForTheFiber) {
  // Fiber-local L/K banks are copied independently of shared game memory.
  Run(source, "pause_execution(); result = L[1]; text = K[1];");
  auto* fiber = source.GameplayMachine().vm->fibres_.front();
  auto memory = source.memory->CreateHandle();
  memory.PushStackFrame(3, 3);
  memory.Write(IntBank::L, 1, 43);
  memory.Write(StrBank::K, 1, "saved");
  fiber->usrdata = memory;
  RoundTrip();
  target.GameplayMachine().vm->RunReadyUntilBlocked();
  EXPECT_EQ(Global(target, "result"), sr::Value(43));
  EXPECT_EQ(Global(target, "text"), "saved");
}

TEST_F(SiglusExecutionSerializationTest,
       ResumesThroughTheSavedBankCleanupGuard) {
  // A checkpoint retains its bank stack and returns through the existing guard.
  Run(source,
      "__builtin_push_frame([41],[\"callee\"]); "
      "try { loaded = pause_execution(); result = L[0] + loaded; } "
      "catch (error) { __builtin_pop_frame(); throw error; } "
      "__builtin_pop_frame(); A[1] = L[0];");
  RoundTrip(true);
  target.GameplayMachine().vm->RunReadyUntilBlocked();
  EXPECT_EQ(Global(target, "result"), sr::Value(42));
  EXPECT_EQ(target.memory->Read(IntBank::A, 1), 0);
}

TEST_F(SiglusExecutionSerializationTest,
       RetainsHostOwnedPendingPromiseIdentity) {
  // A pending capture-like promise is rooted by its producer, not a script
  // Value.
  auto pending = std::make_shared<sr::Promise>();
  source.GameplayMachine().vm->TrackPendingPromise(pending);
  ExecutionWriter writer;
  const auto bytes =
      serialization_test::WriteMachine(writer, source.GameplayMachine());
  const auto id = writer.PromiseId(pending);
  ExecutionReader reader;
  auto waits =
      serialization_test::ReadMachine(reader, target.GameplayMachine(), bytes);
  PrepareNativeWaits(target.GameplayMachine(), reader, waits);
  EXPECT_EQ(reader.Promise(id)->status, sr::Promise::Status::Pending);
  EXPECT_EQ(target.GameplayMachine().vm->CountPendingPromises(), 1u);
}

TEST_F(SiglusExecutionSerializationTest, RejectsChangedSceneConstants) {
  // Recompiled scene constants must match the scene used by the continuation.
  binding::FakeScene scene{.scene_name = "checkpoint",
                           .command_name = "checkpoint"};
  scene.AppendElement({36});
  scene.AppendByte(ByteCode::Cmd);
  for (int value : {0, 0, 0, static_cast<int>(Type::Int)})
    scene.AppendInt(value);
  scene.AppendByte(ByteCode::Push);
  scene.AppendInt(static_cast<int>(Type::Int));
  constexpr int constant = 0x76543210;
  scene.AppendInt(constant);
  scene.AppendByte(ByteCode::Return);
  scene.AppendInt(1);
  scene.AppendInt(static_cast<int>(Type::Int));
  const int id = scene.AddTo(*source.archive);
  auto& machine = source.GameplayMachine();
  machine.vm->ScheduleExecute(
      machine.loader->ResolveSceneEntry("checkpoint", 0));
  machine.vm->RunReadyUntilBlocked();
  ExecutionWriter writer;
  const auto bytes = serialization_test::WriteMachine(writer, machine);
  target.archive = source.archive;
  target.GameplayMachine().Initialize();
  srbind::module_ module(target.GameplayMachine().vm->gc_.get(),
                         target.GameplayMachine().vm->globals_.get());
  module.def("pause_execution", [](sr::VM& vm) {
    vm.RequestHostYield();
    return 0;
  });
  RememberBindingSymbols(target.GameplayMachine());
  auto& raw = target.archive->raw_scene_data_.at(id);
  const std::string needle(reinterpret_cast<const char*>(&constant),
                           sizeof(constant));
  const auto offset = raw.find(needle);
  ASSERT_NE(offset, std::string::npos);
  raw[offset] ^= 1;
  ExecutionReader reader;
  EXPECT_THROW(
      serialization_test::ReadMachine(reader, target.GameplayMachine(), bytes),
      std::runtime_error);
}

TEST_F(SiglusExecutionSerializationTest,
       ReusesFreshBindingSingletonForBorrowedState) {
  // Wrapper-only singletons reconnect to the fresh binding's native context.
  struct Singleton {
    int context;
  };
  auto install = [](SiglusRuntime& runtime, int context) {
    auto& machine = runtime.GameplayMachine();
    srbind::module_ module(machine.vm->gc_.get(), machine.vm->globals_.get());
    srbind::class_<Singleton> klass(module, "TestSingleton", false);
    klass.inst("singleton", Singleton{context});
    RememberBindingSymbols(machine);
  };
  install(source, 12);
  install(target, 35);
  auto* fresh = Global(target, "singleton").Get<sr::NativeInstance*>();
  Run(source, "alias = singleton; pause_execution();");
  RoundTrip();
  EXPECT_EQ(Global(target, "alias").Get<sr::NativeInstance*>(), fresh);
  EXPECT_EQ(fresh->GetForeign<Singleton>()->context, 35);
}

TEST_F(SiglusExecutionSerializationTest,
       RetainsCompletedEntryAfterFiberRemoval) {
  // The host's entry promise remains serializable after its fiber was swept.
  auto completion = std::make_shared<sr::Promise>();
  completion->Resolve(43);
  source.GameplayMachine().entry_completion = completion;
  ASSERT_TRUE(source.GameplayMachine().vm->fibres_.empty());
  RoundTrip();
  auto restored = target.GameplayMachine().entry_completion;
  ASSERT_NE(restored, nullptr);
  ASSERT_TRUE(restored->HasResult());
  EXPECT_EQ(restored->result->value(), sr::Value(43));
}

TEST_F(SiglusExecutionSerializationTest, RejectsTruncatedContinuation) {
  // A shortened archive is rejected before any continuation can run.
  Run(source, "pause_execution();");
  ExecutionWriter writer;
  auto bytes =
      serialization_test::WriteMachine(writer, source.GameplayMachine());
  bytes.resize(bytes.size() / 2);
  ExecutionReader reader;
  EXPECT_THROW(
      serialization_test::ReadMachine(reader, target.GameplayMachine(), bytes),
      std::exception);
}

TEST_F(SiglusExecutionSerializationTest,
       NativeHandlesAndWaitShareTheRuntimeResource) {
  // Two independently created handles and a producer share the root counter.
  Run(source, "first = counter[0]; second = counter[0]; pause_execution();");
  auto root = source.counter_list->GetHandle(0);
  auto& original = source.GameplayMachine();
  lop::MakePollingWaitFuture(
      *original.vm, [] { return false; }, false, nullptr,
      std::chrono::milliseconds(5), {},
      DescribePollingWait(original.WaitScope(),
                          {.kind = PollingWaitDescription::Kind::Counter,
                           .counter = root,
                           .value = 10}));
  ExecutionWriter writer;
  ExecutionReader reader;
  auto loaded = serialization_test::RoundTripMachineResource(
      root, writer, reader, original, target.GameplayMachine());
  const auto first = Global(target, "first")
                         .Get<sr::NativeInstance*>()
                         ->GetForeign<binding::CounterHandle>();
  const auto second = Global(target, "second")
                          .Get<sr::NativeInstance*>()
                          ->GetForeign<binding::CounterHandle>();
  ASSERT_EQ(loaded.second.size(), 1u);
  const auto& wait =
      std::get<PollingWaitDescription>(loaded.second.front().value);
  EXPECT_EQ(first->counter_, loaded.first);
  EXPECT_EQ(second->counter_, loaded.first);
  EXPECT_EQ(wait.counter, loaded.first);
  EXPECT_NE(loaded.first, root);
}

TEST_F(SiglusExecutionSerializationTest, RejectsMissingNativeDeserializer) {
  // A target binding without its payload loader must reject saved instances.
  Run(source, "items = make_intlist(1); pause_execution();");
  ExecutionWriter writer;
  auto bytes =
      serialization_test::WriteMachine(writer, source.GameplayMachine());
  target.GameplayMachine()
      .class_registry.GetClass<binding::IntListHandle>()
      .native_class()
      ->load = {};
  ExecutionReader reader;
  EXPECT_THROW(
      serialization_test::ReadMachine(reader, target.GameplayMachine(), bytes),
      std::runtime_error);
}

TEST_F(SiglusExecutionSerializationTest, RejectsUndefinedPromiseState) {
  // Host links cannot commit a placeholder promise whose state was never saved.
  ExecutionReader reader;
  reader.Promise(1);
  EXPECT_THROW(reader.FinishRestore(), std::runtime_error);
}

}  // namespace siglus_test
