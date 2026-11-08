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

#include "m6/compiler_pipeline.hpp"
#include "m6/source_buffer.hpp"
#include "srbind/module.hpp"
#include "vm/dict.hpp"
#include "vm/future.hpp"
#include "vm/list.hpp"
#include "vm/serialization.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <sstream>

namespace {
using namespace serilang;

class VMSerializationTest : public ::testing::Test {
 protected:
  VM source{std::make_shared<GarbageCollector>()};
  VM target{std::make_shared<GarbageCollector>()};
  SerializationSession saving, loading;

  void SetUp() override {
    for (auto* vm : {&source, &target}) {
      srbind::module_ module(vm->gc_.get(), vm->globals_.get());
      module.def("pause", [](VM& vm) {
        vm.RequestHostYield();
        return 0;
      });
      RememberBindingSymbols(*vm);
    }
  }
  void Run(VM& vm, std::string source) {
    m6::CompilerPipeline compiler(vm.gc_, false);
    compiler.compile(
        m6::SourceBuffer::Create(std::move(source), "<serialization test>"));
    if (!compiler.Ok())
      throw std::runtime_error(compiler.FormatErrors());
    vm.Evaluate(compiler.Get());
  }
  std::string Save() {
    std::ostringstream stream(std::ios::binary);
    boost::archive::binary_oarchive archive(stream);
    ObjectWriter writer(archive, source, saving);
    source.Save(writer);
    writer.Finish();
    return stream.str();
  }
  void Load(const std::string& bytes) {
    std::istringstream stream(bytes, std::ios::binary);
    boost::archive::binary_iarchive archive(stream);
    ObjectReader reader(archive, target, loading);
    target.Load(reader);
    reader.Finish();
    loading.FinishRestore();
  }
  void RoundTrip() { Load(Save()); }
  Value Global(const char* name) { return target.globals_->at(name); }
};

TEST_F(VMSerializationTest, PreservesCyclicLists) {
  // A self-referencing list must resolve to the same restored node.
  auto* list = source.gc_->Allocate<List>();
  list->items.push_back(list);
  (*source.globals_)["list"] = list;
  RoundTrip();
  auto* restored = Global("list").Get<List*>();
  EXPECT_EQ(restored->items.front().Get<List*>(), restored);
}

TEST_F(VMSerializationTest, PreservesAliasesAcrossContainers) {
  // A dictionary and global share one list; loading must preserve that alias.
  auto* list = source.gc_->Allocate<List>();
  auto* dict = source.gc_->Allocate<Dict>();
  dict->map.emplace(1, list);
  (*source.globals_)["list"] = list;
  (*source.globals_)["dict"] = dict;
  RoundTrip();
  EXPECT_EQ(Global("dict").Get<Dict*>()->map.at(1), Global("list"));
}

TEST_F(VMSerializationTest, PreservesSharedFunctionNamespaces) {
  // Functions and a module retain one mutable namespace after loading.
  auto* code = source.gc_->Allocate<Code>();
  code->Append(Return{});
  auto* module = source.gc_->Allocate<Module>("shared");
  auto* first = source.gc_->Allocate<Function>(code);
  auto* second = source.gc_->Allocate<Function>(code);
  first->globals = second->globals = module->globals;
  (*module->globals)["first"] = first;
  (*source.globals_)["module"] = module;
  (*source.globals_)["second"] = second;
  RoundTrip();
  auto* restored = Global("module").Get<Module*>();
  EXPECT_EQ(Global("second").Get<Function*>()->globals, restored->globals);
  EXPECT_EQ(restored->globals->at("first").Get<Function*>()->globals,
            restored->globals);
}

TEST_F(VMSerializationTest, RestoresDynamicClassProperties) {
  // A script-created class and its property remain executable after loading.
  Run(source,
      "class Box { property value { get { return self.stored; } set { "
      "self.stored = value; } } } box = Box(); box.value = 42; pause(); result "
      "= box.value;");
  RoundTrip();
  target.RunReadyUntilBlocked();
  EXPECT_EQ(Global("result"), Value(42));
}

TEST_F(VMSerializationTest, RestoresStoredListMethod) {
  // A stored list method must mutate its restored receiver.
  Run(source,
      "items = []; append = items.append; pause(); append(9); result = "
      "items[0];");
  RoundTrip();
  target.RunReadyUntilBlocked();
  EXPECT_EQ(Global("result"), Value(9));
}

TEST_F(VMSerializationTest, RestoresStoredDictionaryMethod) {
  // A stored dictionary method retains its receiver and operation.
  Run(source, "items = {1: 7}; get = items.get; pause(); result = get(1);");
  RoundTrip();
  target.RunReadyUntilBlocked();
  EXPECT_EQ(Global("result"), Value(7));
}

TEST_F(VMSerializationTest, RestoresStoredStringMethod) {
  // A stored string method returns the same result after loading.
  Run(source,
      "text = \"hello\"; length = text.len; pause(); result = length();");
  RoundTrip();
  target.RunReadyUntilBlocked();
  EXPECT_EQ(Global("result"), Value(5));
}

TEST_F(VMSerializationTest, ResumesThroughExceptionHandler) {
  // A suspended try block must still route a later throw to its saved handler.
  Run(source, "try { pause(); throw 17; } catch (error) { result = error; }");
  RoundTrip();
  target.RunReadyUntilBlocked();
  EXPECT_EQ(Global("result"), Value(17));
}

TEST_F(VMSerializationTest, PreservesAwaitedPromiseIdentity) {
  // Two futures share a pending promise; resolving it wakes the saved fiber.
  auto* first = source.gc_->Allocate<Future>();
  auto* second = source.gc_->Allocate<Future>();
  second->promise = first->promise;
  (*source.globals_)["first"] = first;
  (*source.globals_)["second"] = second;
  auto* code = source.gc_->Allocate<Code>();
  code->const_pool = {first};
  code->Append(Push{0});
  code->Append(Await{});
  code->Append(Return{});
  source.AddFiber(code);
  source.RunReadyUntilBlocked();
  RoundTrip();
  auto promise = Global("first").Get<Future*>()->promise;
  EXPECT_EQ(promise, Global("second").Get<Future*>()->promise);
  promise->Resolve(23);
  EXPECT_EQ(target.RunReadyUntilBlocked(), Value(23));
}

TEST_F(VMSerializationTest, PreservesSchedulerOrder) {
  // A microtask runs before two FIFO tasks after restoring the ready queues.
  Run(source,
      "order = []; fn first() { order.append(1); } fn second() { "
      "order.append(2); } fn micro() { order.append(3); }");
  auto* first = source.ScheduleExecute(source.globals_->at("first"));
  auto* second = source.ScheduleExecute(source.globals_->at("second"));
  auto* micro = source.ScheduleExecute(source.globals_->at("micro"));
  source.scheduler_.RestoreReadyTasks({{first, second}, {micro}});
  RoundTrip();
  target.RunReadyUntilBlocked();
  EXPECT_EQ(Global("order").Get<List*>()->items, (std::vector<Value>{3, 1, 2}));
}

TEST_F(VMSerializationTest, RejectsMissingBindingSymbol) {
  // A saved native definition requires the corresponding target binding.
  Run(source, "pause();");
  auto bytes = Save();
  target.binding_symbols_.clear();
  EXPECT_THROW(Load(bytes), std::exception);
}

TEST_F(VMSerializationTest, RejectsInvalidInstructionPosition) {
  // An instruction pointer in the middle of an operand cannot be resumed.
  Run(source, "pause(); result = 2;");
  source.fibres_.front()->frames.front().ip = 2;
  EXPECT_THROW(RoundTrip(), std::runtime_error);
}

TEST_F(VMSerializationTest, RejectsTruncatedGraph) {
  // Truncation must fail before any restored script is executed.
  Run(source, "pause();");
  auto bytes = Save();
  bytes.resize(bytes.size() / 2);
  EXPECT_THROW(Load(bytes), std::exception);
}

TEST_F(VMSerializationTest, RestoredGraphSurvivesCollection) {
  // Collection retains the restored cyclic graph and its queued continuation.
  Run(source, "items = []; items.append(items); pause(); result = items[0];");
  RoundTrip();
  target.CollectGarbage();
  target.RunReadyUntilBlocked();
  EXPECT_EQ(Global("result"), Global("items"));
}

struct NativePayload {
  int value;
  static inline int destroyed = 0;
  explicit NativePayload(int v) : value(v) {}
  ~NativePayload() { ++destroyed; }
};

TEST_F(VMSerializationTest, BindingOwnsPayloadSerialization) {
  // An opaque native payload round-trips solely through its registered hooks.
  for (auto* vm : {&source, &target}) {
    srbind::module_ module(vm->gc_.get(), vm->globals_.get());
    srbind::class_<NativePayload> klass(module, "Payload");
    klass.def_serialization(
        [](const NativePayload& p, ObjectWriter& out) { out.Put(p.value); },
        [](ObjectReader& in) {
          return std::make_unique<NativePayload>(in.Get<int>());
        });
    if (vm == &source)
      (*vm->globals_)["payload"] = klass.make_inst(41);
    RememberBindingSymbols(*vm);
  }
  RoundTrip();
  EXPECT_EQ(Global("payload")
                .Get<NativeInstance*>()
                ->GetForeign<NativePayload>()
                ->value,
            41);
}

TEST_F(VMSerializationTest, NativeLoaderUsesDestinationContext) {
  // The load callback captures target services rather than the source binding.
  int source_context = 1, target_context = 2;
  for (auto* vm : {&source, &target}) {
    srbind::module_ module(vm->gc_.get(), vm->globals_.get());
    srbind::class_<NativePayload> klass(module, "Payload");
    int* context = vm == &source ? &source_context : &target_context;
    klass.def_serialization([](const NativePayload&, ObjectWriter&) {},
                            [context](ObjectReader&) {
                              return std::make_unique<NativePayload>(*context);
                            });
    if (vm == &source)
      (*vm->globals_)["payload"] = klass.make_inst(1);
    RememberBindingSymbols(*vm);
  }
  RoundTrip();
  EXPECT_EQ(Global("payload")
                .Get<NativeInstance*>()
                ->GetForeign<NativePayload>()
                ->value,
            2);
}

TEST_F(VMSerializationTest, FailedNativeLoadReleasesCollectionGuard) {
  // A throwing payload constructor rejects the graph and restores GC operation.
  for (auto* vm : {&source, &target}) {
    srbind::module_ module(vm->gc_.get(), vm->globals_.get());
    srbind::class_<NativePayload> klass(module, "Payload");
    klass.def_serialization(
        [](const NativePayload&, ObjectWriter&) {},
        [](ObjectReader&) -> std::unique_ptr<NativePayload> {
          throw std::runtime_error("constructor failed");
        });
    if (vm == &source)
      (*vm->globals_)["payload"] = klass.make_inst(1);
    RememberBindingSymbols(*vm);
  }
  EXPECT_THROW(RoundTrip(), std::runtime_error);
  EXPECT_FALSE(target.gc_->CollectionDeferred());
}

TEST_F(VMSerializationTest, CollectionReleasesUnreachableNativePayload) {
  // Removing the restored payload's last root lets the collector finalize it.
  for (auto* vm : {&source, &target}) {
    srbind::module_ module(vm->gc_.get(), vm->globals_.get());
    srbind::class_<NativePayload> klass(module, "Payload");
    klass.def_serialization(
        [](const NativePayload& payload, ObjectWriter& writer) {
          writer.Put(payload.value);
        },
        [](ObjectReader& reader) {
          return std::make_unique<NativePayload>(reader.Get<int>());
        });
    if (vm == &source)
      (*vm->globals_)["payload"] = klass.make_inst(7);
    RememberBindingSymbols(*vm);
  }
  RoundTrip();
  int before = NativePayload::destroyed;
  target.globals_->erase("payload");
  target.CollectGarbage();
  EXPECT_EQ(NativePayload::destroyed, before + 1);
}

TEST_F(VMSerializationTest, NativeLoaderDefersCollectionUntilGraphIsLinked) {
  // Collection requested inside a loader cannot sweep the partially read graph.
  for (auto* vm : {&source, &target}) {
    srbind::module_ module(vm->gc_.get(), vm->globals_.get());
    srbind::class_<NativePayload> klass(module, "Payload");
    klass.def_serialization(
        [](const NativePayload& payload, ObjectWriter& writer) {
          writer.Put(payload.value);
        },
        [](ObjectReader& reader) {
          EXPECT_TRUE(reader.vm.gc_->CollectionDeferred());
          reader.vm.CollectGarbage();
          return std::make_unique<NativePayload>(reader.Get<int>());
        });
    if (vm == &source)
      (*vm->globals_)["payload"] = klass.make_inst(19);
    RememberBindingSymbols(*vm);
  }
  RoundTrip();
  target.CollectGarbage();
  EXPECT_EQ(Global("payload")
                .Get<NativeInstance*>()
                ->GetForeign<NativePayload>()
                ->value,
            19);
}

TEST_F(VMSerializationTest, ValidatesContinuationAfterCyclicCodeIsComplete) {
  // A code constant points back to its own fiber; validation waits for
  // hydration.
  auto* code = source.gc_->Allocate<Code>();
  code->Append(Push{0});
  code->Append(Return{});
  auto* fiber = source.AddFiber(code);
  code->const_pool.push_back(fiber);
  (*source.globals_)["code"] = code;
  RoundTrip();
  EXPECT_EQ(Global("code").Get<Code*>()->const_pool.front().Get<Fiber*>(),
            target.fibres_.front());
}

}  // namespace
