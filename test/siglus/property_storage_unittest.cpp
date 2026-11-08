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

#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/recompiler.hpp"
#include "libsiglus/scene_entry.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "m6/vm_factory.hpp"
#include "siglus/fake_scene.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <cstring>
#include <sstream>
#include <utility>

namespace siglus_test {
using namespace libsiglus;
namespace sr = serilang;

class SiglusPropertyStorageTest : public ::testing::Test {
 protected:
  SiglusRuntime runtime;
  std::unique_ptr<SiglusMachine> other;

  void SetUp() override {
    runtime.archive = std::make_shared<Archive>(
        std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
    runtime.archive->prop_ = {
        {.form = Type::Int, .size = 0, .name = "number"},
        {.form = Type::String, .size = 0, .name = "text"},
        {.form = Type::IntList, .size = 3, .name = "numbers"},
        {.form = Type::StrList, .size = 2, .name = "strings"}};
    AddScene("properties");
    AddScene("other_properties");
    runtime.properties.Initialize(*runtime.archive);
    Initialize(runtime.GameplayMachine());
    other = std::make_unique<SiglusMachine>(runtime);
    Initialize(*other);
  }

  void AddScene(const char* name) {
    binding::FakeScene scene{.scene_name = name, .command_name = "noop"};
    const int id = scene.AddTo(*runtime.archive);
    auto& bytes = runtime.archive->raw_scene_data_.at(id);
    Scene_hdr header;
    std::memcpy(&header, bytes.data(), sizeof(header));
    header.prop_offset = bytes.size();
    header.prop_cnt = runtime.archive->prop_.size();
    for (const auto& property : runtime.archive->prop_) {
      const int values[] = {static_cast<int>(property.form), property.size};
      bytes.append(reinterpret_cast<const char*>(values), sizeof(values));
    }
    bytes.append(sizeof(Property) * header.prop_cnt, '\0');
    std::memcpy(bytes.data(), &header, sizeof(header));
  }

  void Initialize(SiglusMachine& machine) {
    machine.vm = std::make_unique<sr::VM>(m6::VMFactory::Create());
    machine.loader =
        std::make_unique<binding::Loader>(*runtime.archive, *machine.vm, false);
    auto* bind = binding::SiglusBindingRegistry::Find("0_memory");
    ASSERT_NE(bind, nullptr);
    (*bind)(machine);
  }

  void Run(SiglusMachine& machine, std::string source) {
    binding::Execute(*machine.vm, std::move(source));
  }

  sr::Value Global(SiglusMachine& machine, const char* name) {
    return machine.vm->globals_->at(name);
  }

  static elm::AccessChain PropertyChain(int scene,
                                        int index,
                                        Type type = Type::Int) {
    return {.root = elm::Root(
                type, elm::Usrprop{
                          .scene = scene, .idx = index, .name = "property"})};
  }

  void CompileAndRun(std::vector<token::Token_t> body) {
    auto& game = runtime.GameplayMachine();
    Recompiler compiler(game.vm->gc_);
    compiler.SetSceneId(0);
    std::vector<Parser::ParsedToken> tokens;
    for (auto& token : body)
      tokens.push_back({static_cast<int>(tokens.size() + 1), std::move(token)});
    compiler.Compile(tokens);
    ASSERT_TRUE(compiler.Ok());
    auto* module = compiler.module_;
    (*game.vm->builtins_)["test_properties"] = module;
    sr::VM bootstrap(game.vm->gc_);
    bootstrap.gc_threshold_ = 0;
    bootstrap.globals_ = module->globals;
    bootstrap.Evaluate(compiler.GetCode());
    EXPECT_FALSE(module->globals->contains("__usrprop"));
    auto* dispatcher = module->globals->at("%%dispatch").Get<sr::Function*>();
    (*game.vm->globals_)["entry"] =
        MakeSceneEntry(*game.vm->gc_, dispatcher, compiler.GetMainLine(), 0);
    Run(game, "result = entry();");
  }

  void RecompiledReadWrite(int scene) {
    CompileAndRun(
        {token::Assign{.dst = PropertyChain(scene, 0), .src = Integer(137)},
         token::GetProperty{.chain = PropertyChain(scene, 0),
                            .dst = Variable{Type::Int, 0}},
         token::Return{.ret_vals = {Variable{Type::Int, 0}}}});
    EXPECT_EQ(Global(runtime.GameplayMachine(), "result"), sr::Value(137));
    EXPECT_EQ(std::get<int>(runtime.properties.Get(scene, 0).value), 137);
  }

  template <class Output, class Input>
  void CheckRoundTrip() {
    auto& storage = runtime.properties;
    const std::vector<PropertyStorage::Value> values = {
        std::monostate{},
        true,
        47,
        2.5,
        std::string("saved"),
        PropertyStorage::Integers{3, 5, 7, 9},
        PropertyStorage::Strings{"first", "second", "third"}};
    // Populate separate entries so the archive exercises every alternative.
    for (std::size_t i = 0; i < values.size(); ++i)
      storage.Get(static_cast<int>(i / 4) - 1, i % 4).value = values[i];
    storage.Get(1, 2).value = PropertyStorage::Integers{};
    storage.Get(1, 3).value = PropertyStorage::Strings{};
    std::stringstream stream;
    {
      Output archive(stream);
      archive << storage;
    }
    PropertyStorage restored;
    {
      Input archive(stream);
      archive >> restored;
    }
    for (std::size_t i = 0; i < values.size(); ++i)
      EXPECT_EQ(restored.Get(static_cast<int>(i / 4) - 1, i % 4).value,
                values[i]);
    EXPECT_TRUE(
        std::get<PropertyStorage::Integers>(restored.Get(1, 2).value).empty());
    EXPECT_TRUE(
        std::get<PropertyStorage::Strings>(restored.Get(1, 3).value).empty());
    EXPECT_EQ(restored.Get(-1, 2).initial_size, 3);
    EXPECT_EQ(restored.Get(1, 3).initial_size, 2);
  }
};

TEST_F(SiglusPropertyStorageTest, AllocatesSceneArraysBeforeModuleLoading) {
  // Initialize all scenes without loading modules; arrays already have their
  // declared lengths and zero values.
  const auto& storage = runtime.properties;
  EXPECT_EQ(std::get<PropertyStorage::Integers>(storage.Get(1, 2).value),
            (PropertyStorage::Integers{0, 0, 0}));
  EXPECT_EQ(std::get<PropertyStorage::Strings>(storage.Get(1, 3).value),
            (PropertyStorage::Strings{"", ""}));
}

TEST_F(SiglusPropertyStorageTest, ScalarsAreSharedAcrossIndependentHeaps) {
  // Update scalar properties in either VM; both observe the host's values.
  auto& game = runtime.GameplayMachine();
  Run(game,
      R"(__builtin_store_prop(-1, 0, 47); __builtin_store_prop(-1, 1, "hello");)");
  Run(*other, R"(number = __builtin_load_prop(-1, 0);
                 text = __builtin_load_prop(-1, 1);
                 __builtin_store_prop(-1, 0, 59);)");
  EXPECT_EQ(Global(*other, "number"), sr::Value(47));
  EXPECT_EQ(Global(*other, "text").Get<sr::String*>()->str_, "hello");
  Run(game, "number = __builtin_load_prop(-1, 0);");
  EXPECT_EQ(Global(game, "number"), sr::Value(59));
}

TEST_F(SiglusPropertyStorageTest, ComparisonResultsRemainSharedValues) {
  // Store a comparison result; another heap reads the same boolean.
  Run(runtime.GameplayMachine(), "__builtin_store_prop(-1, 0, 3 < 7);");
  Run(*other, "item = __builtin_load_prop(-1, 0);");
  EXPECT_EQ(Global(*other, "item"), sr::Value(true));
}

TEST_F(SiglusPropertyStorageTest, IntegerArrayResizeIsVisibleInAnotherHeap) {
  // Resize an array; loading it in another heap must preserve its new length.
  Run(runtime.GameplayMachine(),
      "__builtin_load_prop(-1, 2).resize(5); __builtin_load_prop(-1, 2)[4] = "
      "71;");
  Run(*other,
      "size = __builtin_load_prop(-1, 2).size(); item = "
      "__builtin_load_prop(-1, 2)[4];");
  EXPECT_EQ(Global(*other, "size"), sr::Value(5));
  EXPECT_EQ(Global(*other, "item"), sr::Value(71));
}

TEST_F(SiglusPropertyStorageTest, LoadingShrunkenArrayPreservesItsLength) {
  // Shrink below the declared length; a new wrapper must not reinitialize it.
  Run(runtime.GameplayMachine(), "__builtin_load_prop(-1, 2).resize(0);");
  Run(*other, "size = __builtin_load_prop(-1, 2).size();");
  EXPECT_EQ(Global(*other, "size"), sr::Value(0));
}

TEST_F(SiglusPropertyStorageTest, StringArrayMutationIsShared) {
  // Mutate a string element; the other heap receives its own string wrapper.
  Run(runtime.GameplayMachine(),
      R"(__builtin_load_prop(-1, 3)[1] = "shared";)");
  Run(*other, "item = __builtin_load_prop(-1, 3)[1];");
  EXPECT_EQ(Global(*other, "item").Get<sr::String*>()->str_, "shared");
}

TEST_F(SiglusPropertyStorageTest, AssignedArrayIsIndependentOfItsSource) {
  // Assign a standalone list; later mutations on either side stay independent.
  auto& game = runtime.GameplayMachine();
  Run(game,
      "source = make_intlist(2); source[1] = 83; __builtin_store_prop(-1, 2, "
      "source); source[1] = 97;");
  Run(*other,
      "__builtin_load_prop(-1, 2).resize(4); item = __builtin_load_prop(-1, "
      "2)[1];");
  Run(game, "size = source.size(); item = source[1];");
  EXPECT_EQ(Global(*other, "item"), sr::Value(83));
  EXPECT_EQ(Global(game, "size"), sr::Value(2));
  EXPECT_EQ(Global(game, "item"), sr::Value(97));
}

TEST_F(SiglusPropertyStorageTest,
       AssignedStringArraySurvivesSourceMachineDestruction) {
  // Copy a temporary VM's string list; destroying that VM retains the values.
  Run(*other, R"(source = make_strlist(2); source[1] = "retained";
                 __builtin_store_prop(-1, 3, source); source[1] = "changed";)");
  other.reset();
  auto& game = runtime.GameplayMachine();
  game.vm->CollectGarbage();
  Run(game, "item = __builtin_load_prop(-1, 3)[1];");
  EXPECT_EQ(Global(game, "item").Get<sr::String*>()->str_, "retained");
}

TEST_F(SiglusPropertyStorageTest,
       PropertyArraySurvivesSourceMachineDestruction) {
  // Copy a temporary VM's integer list; its values outlive the source heap.
  Run(*other,
      "source = make_intlist(2); source[1] = 97; __builtin_store_prop(-1, 2, "
      "source);");
  other.reset();
  auto& game = runtime.GameplayMachine();
  game.vm->CollectGarbage();
  Run(game, "item = __builtin_load_prop(-1, 2)[1];");
  EXPECT_EQ(Global(game, "item"), sr::Value(97));
}

TEST_F(SiglusPropertyStorageTest, AssignedBankIsASnapshot) {
  // Copy a flag bank; property writes must not mutate the bank.
  Run(*other, "F[1] = 101; __builtin_store_prop(-1, 2, F);");
  other.reset();
  auto& game = runtime.GameplayMachine();
  Run(game, "__builtin_load_prop(-1, 2)[1] = 103; item = F[1];");
  EXPECT_EQ(Global(game, "item"), sr::Value(101));
}

TEST_F(SiglusPropertyStorageTest, AssignedStackUsesTheSourceFiber) {
  // Snapshot L in the assigning fiber; another fiber's L cannot affect it.
  Run(*other, "L[1] = 107; __builtin_store_prop(-1, 2, L);");
  other.reset();
  auto& game = runtime.GameplayMachine();
  Run(game, "L[1] = 109; item = __builtin_load_prop(-1, 2)[1];");
  EXPECT_EQ(Global(game, "item"), sr::Value(107));
}

TEST_F(SiglusPropertyStorageTest, RetainedHandleFollowsPropertyReplacement) {
  // Retain a wrapper before replacing its property; it reads the new vector.
  auto& game = runtime.GameplayMachine();
  Run(game, "retained = __builtin_load_prop(-1, 2);");
  Run(*other,
      "source = make_intlist(1); source[0] = 127; __builtin_store_prop(-1, 2, "
      "source);");
  Run(game, "size = retained.size(); item = retained[0];");
  EXPECT_EQ(Global(game, "size"), sr::Value(1));
  EXPECT_EQ(Global(game, "item"), sr::Value(127));
}

TEST_F(SiglusPropertyStorageTest, RetainedHandleRejectsChangedPropertyType) {
  // Replace a list with a scalar; retained list access reports a script error.
  auto& game = runtime.GameplayMachine();
  Run(game, R"(retained = __builtin_load_prop(-1, 2);
                __builtin_store_prop(-1, 2, 1); caught = 0;
                try { retained.size(); } catch (error) { caught = 1; })");
  EXPECT_EQ(Global(game, "caught"), sr::Value(1));
}

TEST_F(SiglusPropertyStorageTest, InitRestoresDeclaredIntegerArraySize) {
  // Replace an array with a different-sized list; init uses its declaration.
  auto& game = runtime.GameplayMachine();
  Run(game, R"(__builtin_store_prop(-1, 2, make_intlist(1));
                list = __builtin_load_prop(-1, 2); list[0] = 7;
                list.init(); size = list.size(); item = list[0];)");
  EXPECT_EQ(Global(game, "size"), sr::Value(3));
  EXPECT_EQ(Global(game, "item"), sr::Value(0));
}

TEST_F(SiglusPropertyStorageTest, InitRestoresDeclaredStringArraySize) {
  // Resize a string array; init restores its declared size and empty elements.
  auto& game = runtime.GameplayMachine();
  Run(game, R"(list = __builtin_load_prop(-1, 3); list.resize(4);
                list[0] = "changed"; list.init();
                size = list.size(); item = list[0];)");
  EXPECT_EQ(Global(game, "size"), sr::Value(2));
  EXPECT_EQ(Global(game, "item").Get<sr::String*>()->str_, "");
}

TEST_F(SiglusPropertyStorageTest, StoredBitViewSurvivesFacadeCollection) {
  // Keep a bit view after its temporary wrapper is collected; writes persist.
  auto& game = runtime.GameplayMachine();
  Run(game, "bits = __builtin_load_prop(-1, 2).b1;");
  game.vm->CollectGarbage();
  Run(game, "bits[33] = 1;");
  Run(*other, "item = __builtin_load_prop(-1, 2)[1];");
  EXPECT_EQ(Global(*other, "item"), sr::Value(2));
}

TEST_F(SiglusPropertyStorageTest, ScenePropertiesAreSharedAfterModuleReload) {
  // Compile independent modules after a property write; neither resets it.
  auto& game = runtime.GameplayMachine();
  Run(game, "__builtin_store_prop(0, 0, 113);");
  auto* first = game.loader->Load(0);
  auto* second = other->loader->Load(0);
  Run(*other, "item = __builtin_load_prop(0, 0);");
  EXPECT_NE(first, second);
  EXPECT_FALSE(first->globals->contains("__usrprop"));
  EXPECT_FALSE(second->globals->contains("__usrprop"));
  EXPECT_EQ(Global(*other, "item"), sr::Value(113));
}

TEST_F(SiglusPropertyStorageTest, BindingAnotherMachineDoesNotResetProperties) {
  // Bind a replacement VM after a write; it observes the existing host state.
  auto& game = runtime.GameplayMachine();
  Run(game, "__builtin_store_prop(-1, 0, 127);");
  other.reset();
  other = std::make_unique<SiglusMachine>(runtime);
  Initialize(*other);
  Run(*other, "item = __builtin_load_prop(-1, 0);");
  EXPECT_EQ(Global(*other, "item"), sr::Value(127));
}

TEST_F(SiglusPropertyStorageTest, HelpersRetainClassesDuringCollection) {
  // Remove class globals; helper-created lists still work after collection.
  auto& game = runtime.GameplayMachine();
  game.vm->globals_->erase("__SiglusIntList");
  game.vm->globals_->erase("__SiglusStrList");
  game.vm->CollectGarbage();
  Run(game, R"(__builtin_load_prop(-1, 2)[0] = 17;
                __builtin_load_prop(-1, 3)[0] = "saved";)");
  EXPECT_EQ(std::get<PropertyStorage::Integers>(
                runtime.properties.Get(-1, 2).value)[0],
            17);
  EXPECT_EQ(std::get<PropertyStorage::Strings>(
                runtime.properties.Get(-1, 3).value)[0],
            "saved");
}

TEST_F(SiglusPropertyStorageTest, DoesNotPublishPropertyTableGlobals) {
  // Bind properties; only native helpers expose access to the stored values.
  auto& game = runtime.GameplayMachine();
  EXPECT_FALSE(game.vm->globals_->contains("__globalprop"));
  EXPECT_FALSE(game.vm->globals_->contains("__SiglusPropertyTable"));
}

TEST_F(SiglusPropertyStorageTest, InvalidPropertyAccessDoesNotCreateState) {
  // Invalid scene and property indices fail without inserting a table or slot.
  auto& storage = runtime.properties;
  EXPECT_THROW(storage.Get(99, 0), std::out_of_range);
  EXPECT_THROW(storage.Get(-2, 0), std::out_of_range);
  EXPECT_THROW(storage.Get(-1, -1), std::out_of_range);
  EXPECT_THROW(storage.Get(-1, 4), std::out_of_range);
  EXPECT_THROW(storage.Get(99, 0), std::out_of_range);
}

TEST_F(SiglusPropertyStorageTest, InvalidNativeAccessRaisesScriptError) {
  // Calling a helper with an unknown scene produces a catchable script error.
  auto& game = runtime.GameplayMachine();
  Run(game,
      "caught = 0; try { __builtin_load_prop(99, 0); } catch (error) { caught "
      "= 1; }");
  EXPECT_EQ(Global(game, "caught"), sr::Value(1));
}

TEST_F(SiglusPropertyStorageTest, UnsupportedStorePreservesPreviousValue) {
  // A VM list is unsupported; rejecting it leaves the destination untouched.
  auto& game = runtime.GameplayMachine();
  Run(game,
      "__builtin_store_prop(-1, 0, 19); caught = 0; try { "
      "__builtin_store_prop(-1, 0, [1]); } catch (error) { caught = 1; }");
  EXPECT_EQ(Global(game, "caught"), sr::Value(1));
  EXPECT_EQ(std::get<int>(runtime.properties.Get(-1, 0).value), 19);
}

TEST_F(SiglusPropertyStorageTest, RecompiledGlobalPropertyReadWrite) {
  // Compile global reads and writes; the helpers access the global table.
  RecompiledReadWrite(-1);
}

TEST_F(SiglusPropertyStorageTest, RecompiledSameScenePropertyReadWrite) {
  // Compile reads and writes in scene zero; they access its preallocated table.
  RecompiledReadWrite(0);
}

TEST_F(SiglusPropertyStorageTest, RecompiledOtherScenePropertyReadWrite) {
  // Compile access to an unloaded scene; no module load is needed for its data.
  RecompiledReadWrite(1);
}

TEST_F(SiglusPropertyStorageTest, RecompiledIndexedPropertyReadWrite) {
  // Compile chained list access; item writes modify the property-owned vector.
  auto chain = PropertyChain(1, 2, Type::IntList);
  chain.nodes.push_back(elm::Node(Type::Int, elm::Subscript{Integer(1)}));
  CompileAndRun(
      {token::Assign{.dst = chain, .src = Integer(139)},
       token::GetProperty{.chain = chain, .dst = Variable{Type::Int, 0}},
       token::Return{.ret_vals = {Variable{Type::Int, 0}}}});
  EXPECT_EQ(Global(runtime.GameplayMachine(), "result"), sr::Value(139));
  EXPECT_EQ(std::get<PropertyStorage::Integers>(
                runtime.properties.Get(1, 2).value)[1],
            139);
}

TEST_F(SiglusPropertyStorageTest, RecompiledStringPropertyReadWrite) {
  // Compile a string assignment and read; the result wraps the stored text.
  CompileAndRun({token::Assign{.dst = PropertyChain(0, 1, Type::String),
                               .src = String("compiled")},
                 token::GetProperty{.chain = PropertyChain(0, 1, Type::String),
                                    .dst = Variable{Type::String, 0}},
                 token::Return{.ret_vals = {Variable{Type::String, 0}}}});
  EXPECT_EQ(
      Global(runtime.GameplayMachine(), "result").Get<sr::String*>()->str_,
      "compiled");
}

TEST_F(SiglusPropertyStorageTest, RecompiledWholeArrayAssignmentCopiesValues) {
  // Compile an assignment between array properties; later source writes do
  // not affect the destination vector.
  runtime.properties.Get(-1, 2).value = PropertyStorage::Integers{163};
  CompileAndRun(
      {token::GetProperty{.chain = PropertyChain(-1, 2, Type::IntList),
                          .dst = Variable{Type::IntList, 0}},
       token::Assign{.dst = PropertyChain(1, 2, Type::IntList),
                     .src = Variable{Type::IntList, 0}},
       token::Return{}});
  std::get<PropertyStorage::Integers>(runtime.properties.Get(-1, 2).value)[0] =
      0;
  EXPECT_EQ(
      std::get<PropertyStorage::Integers>(runtime.properties.Get(1, 2).value),
      (PropertyStorage::Integers{163}));
}

TEST_F(SiglusPropertyStorageTest, TextSerializationRoundTrip) {
  // Archive all value types and sizes; restore them without an Archive or VM.
  CheckRoundTrip<boost::archive::text_oarchive,
                 boost::archive::text_iarchive>();
}

TEST_F(SiglusPropertyStorageTest, BinarySerializationRoundTrip) {
  // Binary archives preserve the same independent property state.
  CheckRoundTrip<boost::archive::binary_oarchive,
                 boost::archive::binary_iarchive>();
}

TEST_F(SiglusPropertyStorageTest, DeserializationReplacesExistingState) {
  // Restore an empty storage over initialized state; stale tables disappear.
  PropertyStorage empty;
  std::stringstream stream;
  {
    boost::archive::text_oarchive archive(stream);
    archive << empty;
  }
  {
    boost::archive::text_iarchive archive(stream);
    archive >> runtime.properties;
  }
  EXPECT_THROW(runtime.properties.Get(-1, 0), std::out_of_range);
  EXPECT_THROW(runtime.properties.Get(0, 0), std::out_of_range);
}

TEST_F(SiglusPropertyStorageTest, FailedDeserializationPreservesExistingState) {
  // Truncate a saved archive; failed loading leaves the destination unchanged.
  std::stringstream saved;
  {
    boost::archive::text_oarchive archive(saved);
    archive << runtime.properties;
  }
  runtime.properties.Get(-1, 0).value = 151;
  const auto bytes = saved.str();
  std::stringstream truncated(bytes.substr(0, bytes.size() / 2));
  EXPECT_THROW(
      {
        boost::archive::text_iarchive archive(truncated);
        archive >> runtime.properties;
      },
      boost::archive::archive_exception);
  EXPECT_EQ(std::get<int>(runtime.properties.Get(-1, 0).value), 151);
}

TEST_F(SiglusPropertyStorageTest, InitializationRejectsNegativeArraySize) {
  // Invalid array metadata cannot replace the previously initialized values.
  runtime.properties.Get(-1, 0).value = 157;
  runtime.archive->prop_[2].size = -1;
  EXPECT_THROW(runtime.properties.Initialize(*runtime.archive),
               std::invalid_argument);
  EXPECT_EQ(std::get<int>(runtime.properties.Get(-1, 0).value), 157);
}

TEST_F(SiglusPropertyStorageTest, RestoredPropertiesWorkInFreshMachine) {
  // Restore saved state and bind a fresh VM; list wrappers use restored data.
  auto& game = runtime.GameplayMachine();
  Run(game,
      "__builtin_load_prop(-1, 2).resize(1); __builtin_load_prop(-1, 2)[0] = "
      "149;");
  std::stringstream stream;
  {
    boost::archive::text_oarchive archive(stream);
    archive << runtime.properties;
  }
  runtime.properties = PropertyStorage{};
  {
    boost::archive::text_iarchive archive(stream);
    archive >> runtime.properties;
  }
  other.reset();
  other = std::make_unique<SiglusMachine>(runtime);
  Initialize(*other);
  Run(*other,
      "list = __builtin_load_prop(-1, 2); item = list[0]; size = list.size(); "
      "list.init(); initial = list.size();");
  EXPECT_EQ(Global(*other, "item"), sr::Value(149));
  EXPECT_EQ(Global(*other, "size"), sr::Value(1));
  EXPECT_EQ(Global(*other, "initial"), sr::Value(3));
}

}  // namespace siglus_test
