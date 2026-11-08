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
#include "libsiglus/siglus_runtime.hpp"
#include "m6/vm_factory.hpp"
#include "siglus/fake_scene.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

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
    Initialize(runtime.GameplayMachine());
    other = std::make_unique<SiglusMachine>(runtime);
    Initialize(*other);
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
};

TEST_F(SiglusPropertyStorageTest, ScalarsAreSharedAcrossIndependentHeaps) {
  // Updating scalar properties in either VM immediately updates the same game
  // state.
  auto& game = runtime.GameplayMachine();
  Run(game, R"(__globalprop[0] = 47; __globalprop[1] = "hello";)");
  Run(*other,
      "number = __globalprop[0]; text = __globalprop[1]; __globalprop[0] = "
      "59;");
  EXPECT_EQ(Global(*other, "number"), sr::Value(47));
  EXPECT_EQ(Global(*other, "text").Get<sr::String*>()->str_, "hello");
  Run(game, "number = __globalprop[0];");
  EXPECT_EQ(Global(game, "number"), sr::Value(59));
}

TEST_F(SiglusPropertyStorageTest, ComparisonResultsRemainSharedValues) {
  // A script comparison can be stored in an integer property and read by
  // another VM.
  Run(runtime.GameplayMachine(), "__globalprop[0] = 3 < 7;");
  Run(*other, "item = __globalprop[0];");
  EXPECT_EQ(Global(*other, "item"), sr::Value(true));
}

TEST_F(SiglusPropertyStorageTest, IntegerArrayResizeIsVisibleInAnotherHeap) {
  // A second VM reads a resized array without reapplying the declared default
  // size.
  auto& game = runtime.GameplayMachine();
  Run(game, "__globalprop[2].resize(5); __globalprop[2][4] = 71;");
  Run(*other, "size = __globalprop[2].size(); item = __globalprop[2][4];");
  EXPECT_EQ(Global(*other, "size"), sr::Value(5));
  EXPECT_EQ(Global(*other, "item"), sr::Value(71));
}

TEST_F(SiglusPropertyStorageTest, StringArrayMutationIsShared) {
  // String elements are stored in C++ while each VM receives its own string
  // wrapper.
  Run(runtime.GameplayMachine(), R"(__globalprop[3][1] = "shared";)");
  Run(*other, "item = __globalprop[3][1];");
  EXPECT_EQ(Global(*other, "item").Get<sr::String*>()->str_, "shared");
}

TEST_F(SiglusPropertyStorageTest, AssignedArrayRetainsItsAliasAcrossHeaps) {
  // Assign a script-created list; property mutations remain visible through its
  // original alias.
  auto& game = runtime.GameplayMachine();
  Run(game, "source = make_intlist(2); __globalprop[2] = source;");
  Run(*other, "__globalprop[2].resize(4); __globalprop[2][3] = 83;");
  Run(game, "size = source.size(); item = source[3];");
  EXPECT_EQ(Global(game, "size"), sr::Value(4));
  EXPECT_EQ(Global(game, "item"), sr::Value(83));
}

TEST_F(SiglusPropertyStorageTest,
       AssignedStringArraySurvivesSourceMachineDestruction) {
  // A temporary VM's string-list backing remains owned by the shared property.
  Run(*other,
      R"(source = make_strlist(2); source[1] = "retained"; __globalprop[3] = source;)");
  other.reset();
  auto& game = runtime.GameplayMachine();
  game.vm->CollectGarbage();
  Run(game, "item = __globalprop[3][1];");
  EXPECT_EQ(Global(game, "item").Get<sr::String*>()->str_, "retained");
}

TEST_F(SiglusPropertyStorageTest,
       PropertyArraySurvivesSourceMachineDestruction) {
  // A property assigned by a temporary VM keeps its list backing after that VM
  // is destroyed.
  Run(*other,
      "source = make_intlist(2); source[1] = 97; __globalprop[2] = source;");
  other.reset();
  auto& game = runtime.GameplayMachine();
  game.vm->CollectGarbage();
  Run(game, "item = __globalprop[2][1];");
  EXPECT_EQ(Global(game, "item"), sr::Value(97));
}

TEST_F(SiglusPropertyStorageTest,
       AssignedBankAliasSurvivesSourceMachineDestruction) {
  // A flag-bank alias retains the bank identity rather than the source VM
  // wrapper.
  Run(*other, "F[1] = 101; __globalprop[2] = F;");
  other.reset();
  auto& game = runtime.GameplayMachine();
  game.vm->CollectGarbage();
  Run(game, "__globalprop[2][1] = 103; item = F[1];");
  EXPECT_EQ(Global(game, "item"), sr::Value(103));
}

TEST_F(SiglusPropertyStorageTest, AssignedStackAliasUsesTheReceivingFiber) {
  // L aliases resolve through the invoking fiber's stack without retaining the
  // source fiber.
  Run(*other, "L[1] = 107; __globalprop[2] = L;");
  other.reset();
  auto& game = runtime.GameplayMachine();
  game.vm->CollectGarbage();
  Run(game, "L[1] = 109; item = __globalprop[2][1];");
  EXPECT_EQ(Global(game, "item"), sr::Value(109));
}

TEST_F(SiglusPropertyStorageTest, StoredBitViewSurvivesFacadeCollection) {
  // The retained bit view owns its C++ facade even after the temporary VM
  // wrapper is collected.
  auto& game = runtime.GameplayMachine();
  Run(game, "bits = __globalprop[2].b1;");
  game.vm->CollectGarbage();
  Run(game, "bits[33] = 1;");
  Run(*other, "item = __globalprop[2][1];");
  EXPECT_EQ(Global(*other, "item"), sr::Value(2));
}

TEST_F(SiglusPropertyStorageTest, ScenePropertiesAreSharedAfterModuleReload) {
  // Each loader compiles its own module, but the scene's property table stays
  // shared.
  binding::FakeScene scene{.scene_name = "properties", .command_name = "noop"};
  const int id = scene.AddTo(*runtime.archive);
  auto& bytes = runtime.archive->raw_scene_data_.at(id);
  auto* header = reinterpret_cast<Scene_hdr*>(bytes.data());
  header->prop_offset = bytes.size();
  header->prop_cnt = 1;
  const int values[] = {static_cast<int>(Type::Int), 0};
  bytes.append(reinterpret_cast<const char*>(values), sizeof(values));
  bytes.append(sizeof(Property), '\0');
  auto& game = runtime.GameplayMachine();
  auto* first = game.loader->Load(id);
  (*game.vm->globals_)["props"] = first->globals->at("__usrprop");
  Run(game, "props[0] = 113;");
  auto* second = other->loader->Load(id);
  (*other->vm->globals_)["props"] = second->globals->at("__usrprop");
  Run(*other, "item = props[0];");
  EXPECT_NE(first, second);
  EXPECT_EQ(Global(*other, "item"), sr::Value(113));
}

TEST_F(SiglusPropertyStorageTest, BindingAnotherMachineDoesNotResetProperties) {
  // Rebinding memory in a new VM retains values established before its
  // creation.
  auto& game = runtime.GameplayMachine();
  Run(game, "__globalprop[0] = 127;");
  other.reset();
  other = std::make_unique<SiglusMachine>(runtime);
  Initialize(*other);
  Run(*other, "item = __globalprop[0];");
  EXPECT_EQ(Global(*other, "item"), sr::Value(127));
}

}  // namespace siglus_test
