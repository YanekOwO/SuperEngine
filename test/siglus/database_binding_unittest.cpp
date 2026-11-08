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

#include "core/database.hpp"
#include "core/gameexe.hpp"
#include "database_test_support.hpp"
#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/loader.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/element_parser.hpp"
#include "libsiglus/recompiler.hpp"
#include "m6/compiler_pipeline.hpp"
#include "m6/vm_factory.hpp"
#include "siglus/fake_scene.hpp"
#include "vm/exception.hpp"
#include "vm/vm.hpp"

namespace siglus_test {
using namespace libsiglus;
using namespace libsiglus::elm;
using ::testing::HasSubstr;
namespace sr = serilang;

class SiglusDatabaseElementTest : public ::testing::Test {
 protected:
  ElementParser parser{{},
                       {},
                       {},
                       {},
                       {},
                       0,
                       [] { return 0; },
                       [](std::string warning) { ADD_FAILURE() << warning; }};
  AccessChain Parse(ElementCode element) { return parser.Parse(element); }
  AccessChain Call(int member,
                   std::vector<Value> args,
                   Type result = Type::Int) {
    ElementCode element{105, -1, 1, member};
    element.ForceBind(Invoke{0, std::move(args), result});
    return Parse(std::move(element));
  }
};

TEST_F(SiglusDatabaseElementTest, RootResolvesAsListReference) {
  // An unindexed root without a call remains a database-list reference.
  const auto chain = Parse({105});
  EXPECT_EQ(chain.GetType(), Type::DatabaseList);
  EXPECT_EQ(chain.ToDebugString(), "database");
}

TEST_F(SiglusDatabaseElementTest, IndexedRootResolvesAsDatabaseReference) {
  // A single index resolves a database element without invoking a method.
  const auto chain = Parse({105, -1, 1});
  EXPECT_EQ(chain.GetType(), Type::Database);
  EXPECT_EQ(chain.ToDebugString(), "database[int:1]");
}

TEST_F(SiglusDatabaseElementTest, ComputedIndexIsPreserved) {
  // A runtime integer temporary is retained as the index expression.
  ElementCode element{105, -1, 0};
  element.code.back() = Variable{Type::Int, 5};
  EXPECT_EQ(Parse(element).ToDebugString(), "database[t5]");
}

TEST_F(SiglusDatabaseElementTest, SizeResolvesAsIntegerCall) {
  // List member 1 is an implicit size call with an integer result.
  const auto chain = Parse({105, 1});
  EXPECT_EQ(chain.GetType(), Type::Int);
  EXPECT_EQ(chain.ToDebugString(), "database.size()");
}

TEST_F(SiglusDatabaseElementTest, TitleLookupKeepsArgumentsSeparateFromChain) {
  // The exact title element shape binds two arguments and returns a string.
  const auto chain = Call(1, {Integer{42}, Integer{8}}, Type::String);
  EXPECT_EQ(chain.GetType(), Type::String);
  EXPECT_EQ(chain.ToDebugString(), "database[int:1].get_str(int:42,int:8)");
}

TEST_F(SiglusDatabaseElementTest, AllSupportedMembersResolveTheirSignatures) {
  // Every member code maps to its declared callable with the supplied
  // arguments.
  for (const auto& [member, name] :
       std::vector<std::pair<int, std::string>>{{3, "check_item"},
                                                {4, "check_column"},
                                                {5, "find_num"},
                                                {6, "find_str"},
                                                {7, "find_str_real"}}) {
    std::vector<Value> args = {Integer{3}};
    if (member == 5)
      args.push_back(Integer{5});
    if (member >= 6)
      args.push_back(String{"value"});
    const auto chain = Call(member, std::move(args));
    EXPECT_EQ(chain.GetType(), Type::Int);
    EXPECT_THAT(chain.ToDebugString(), HasSubstr("." + name + "("));
  }
}

TEST_F(SiglusDatabaseElementTest, NumericLookupResolvesAsInteger) {
  // Numeric lookup member 0 binds two integer arguments.
  EXPECT_EQ(Call(0, {Integer{42}, Integer{3}}).GetType(), Type::Int);
}

TEST_F(SiglusDatabaseElementTest, ReservedMemberIsRejected) {
  // Database member 2 has no operation and must not yield a partial chain.
  EXPECT_THROW(Call(2, {}), std::runtime_error);
}

TEST_F(SiglusDatabaseElementTest, MissingIndexIsRejected) {
  // A dangling subscript marker fails before constructing a fallback index.
  EXPECT_THROW(Parse({105, -1}), std::runtime_error);
}

TEST_F(SiglusDatabaseElementTest, NonIntegerIndexIsRejected) {
  // A string cannot be used as a database index.
  ElementCode element{105, -1, 0};
  element.code.back() = String{"zero"};
  EXPECT_THROW(Parse(element), std::runtime_error);
}

TEST_F(SiglusDatabaseElementTest, TrailingMembersAreRejected) {
  // A member after an otherwise valid operation invalidates the entire chain.
  EXPECT_THROW(Parse({105, -1, 1, 0, 1}), std::runtime_error);
}

TEST_F(SiglusDatabaseElementTest, WrongArgumentCountIsRejected) {
  // Typed reads require both an item and a column argument.
  EXPECT_THROW(Call(0, {Integer{42}}), std::runtime_error);
}

TEST_F(SiglusDatabaseElementTest, WrongArgumentTypeIsRejected) {
  // String search requires text as its second argument.
  EXPECT_THROW(Call(6, {Integer{8}, Integer{42}}), std::runtime_error);
}

TEST_F(SiglusDatabaseElementTest, WrongResultTypeIsRejected) {
  // A string read cannot be compiled into an integer result temporary.
  EXPECT_THROW(Call(1, {Integer{42}, Integer{8}}), std::runtime_error);
}

TEST_F(SiglusDatabaseElementTest, UnsupportedListMemberIsRejected) {
  // Database lists expose size and indexing but no resize operation.
  EXPECT_THROW(Parse({105, 2}), std::runtime_error);
}

TEST_F(SiglusDatabaseElementTest, TaggedArgumentsAreRejected) {
  // The database API accepts positional arguments only.
  ElementCode element{105, -1, 1, 3};
  Invoke invoke{0, {Integer{42}}, Type::Int};
  invoke.named_arg.emplace_back(1, Integer{0});
  element.ForceBind(std::move(invoke));
  EXPECT_THROW(Parse(element), std::runtime_error);
}

class SiglusDatabaseBindingTest : public ::testing::Test {
 protected:
  DatabaseFiles files;
  DatabaseFixture fixture;
  SiglusRuntime runtime;
  Archive archive{std::string(sizeof(Pack_hdr), '\0'), xorkey_t{}};

  void SetUp() override {
    runtime.gameexe = std::make_shared<Gameexe>();
    runtime.gameexe->SetIntAt("DATABASE.CNT", 3);
    runtime.gameexe->SetStringAt("DATABASE.001", "sample");
    files.Write("dat/sample.dbs", fixture.File());
    runtime.databases = DatabaseList::Load(*runtime.gameexe, files.root);
    runtime.GameplayMachine().vm =
        std::make_unique<sr::VM>(m6::VMFactory::Create());
    runtime.GameplayMachine().loader = std::make_unique<binding::Loader>(
        archive, *runtime.GameplayMachine().vm, true);
    for (const auto* name : {"0_memory", "flow", "1_database", "3_script"}) {
      const auto* bind = binding::SiglusBindingRegistry::Find(name);
      ASSERT_NE(bind, nullptr);
      (*bind)(runtime.GameplayMachine());
    }
  }

  void Run(std::string source) {
    m6::CompilerPipeline compiler(runtime.GameplayMachine().vm->gc_);
    compiler.compile(m6::SourceBuffer::Create(std::move(source), "<database>"));
    ASSERT_TRUE(compiler.Ok()) << compiler.FormatErrors();
    runtime.GameplayMachine().vm->Evaluate(compiler.Get());
  }

  // Compile actual Siglus bytecode through the loader, rather than a hand-built
  // AST.
  void InstallRead(std::string name,
                   int member,
                   int item,
                   int column,
                   bool computed_index = false) {
    const Type result = member == 1 ? Type::String : Type::Int;
    binding::FakeScene scene{.scene_name = name,
                             .command_name = name,
                             .args = computed_index
                                         ? std::vector<Type>{Type::Int}
                                         : std::vector<Type>{}};
    if (computed_index) {
      scene.AppendElement({105, -1});
      scene.AppendElement({83, 0x7d000000});
      scene.AppendByte(ByteCode::Property);
      scene.AppendByte(ByteCode::Push);
      scene.AppendInt(static_cast<int>(Type::Int));
      scene.AppendInt(member);
    } else {
      scene.AppendElement({105, -1, 1, member});
    }
    for (int arg : {item, column}) {
      scene.AppendByte(ByteCode::Push);
      scene.AppendInt(static_cast<int>(Type::Int));
      scene.AppendInt(arg);
    }
    scene.AppendByte(ByteCode::Cmd);
    for (int value : {0, 2, static_cast<int>(Type::Int),
                      static_cast<int>(Type::Int), 0, static_cast<int>(result)})
      scene.AppendInt(value);
    scene.AppendByte(ByteCode::Return);
    scene.AppendInt(1);
    scene.AppendInt(static_cast<int>(result));
    scene.AddTo(archive);
    (*runtime.GameplayMachine().vm->globals_)[name] =
        runtime.GameplayMachine().loader->ResolveSceneCommand(name, name);
  }
};

TEST_F(SiglusDatabaseBindingTest,
       ExactTitleElementExecutesThroughParserAndRuntime) {
  // A serialized title lookup passes through parsing, compilation, and binding.
  InstallRead("title", 1, 42, 8);
  Run("result = title();");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result").Str(),
            "AbCde");
}

TEST_F(SiglusDatabaseBindingTest, NumericReadExecutesThroughParserAndRuntime) {
  // The numeric command returns a signed cell through the same complete
  // pipeline.
  InstallRead("number", 0, 42, 3);
  Run("result = number();");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result"),
            sr::Value(-17));
}

TEST_F(SiglusDatabaseBindingTest, FailedNumericReadProducesZeroResult) {
  // A missing item produces a fresh zero expression result after another read.
  InstallRead("present", 0, 42, 3);
  InstallRead("missing", 0, 123, 3);
  Run("previous = present(); result = missing();");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result"), sr::Value(0));
}

TEST_F(SiglusDatabaseBindingTest, FailedStringReadProducesEmptyResult) {
  // A missing item produces a fresh empty string after a successful read.
  InstallRead("present", 1, 42, 8);
  InstallRead("missing", 1, 123, 8);
  Run("previous = present(); result = missing();");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result").Str(), "");
}

TEST_F(SiglusDatabaseBindingTest, ComputedIndexExecutesAtRuntime) {
  // A command argument chooses the database slot dynamically.
  InstallRead("number", 0, 42, 3, true);
  Run("result = number(1);");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result"),
            sr::Value(-17));
}

TEST_F(SiglusDatabaseBindingTest, OutOfRangeComputedIndexRaisesEngineError) {
  // A runtime index beyond the fixed list is a checked interpreter error.
  InstallRead("number", 0, 42, 3, true);
  EXPECT_THROW(Run("result = number(3);"), std::exception);
}

TEST_F(SiglusDatabaseBindingTest, EmptySlotReadProducesDefaultResult) {
  // Dynamically selecting an unnamed database returns a valid typed default.
  InstallRead("number", 0, 42, 3, true);
  Run("result = number(0);");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result"), sr::Value(0));
}

TEST_F(SiglusDatabaseBindingTest, ListSizeIncludesEmptySlots) {
  // The global fixed-size list counts named and unnamed slots.
  Run(R"(result = database.size();)");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result"), sr::Value(3));
}

TEST_F(SiglusDatabaseBindingTest, ItemCheckExecutesThroughBinding) {
  // A database item check reports the stored item ID.
  Run(R"(result = database[1].check_item(42);)");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result"), sr::Value(1));
}

TEST_F(SiglusDatabaseBindingTest, ColumnCheckExecutesThroughBinding) {
  // A database column check reports the string type.
  Run(R"(result = database[1].check_column(8);)");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result"), sr::Value(2));
}

TEST_F(SiglusDatabaseBindingTest, NumericSearchExecutesThroughBinding) {
  // Numeric search returns the first matching stored item ID.
  Run(R"(result = database[1].find_num(3, 5);)");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result"), sr::Value(7));
}

TEST_F(SiglusDatabaseBindingTest, FoldedStringSearchExecutesThroughBinding) {
  // String search folds ASCII case in the native binding.
  Run(R"(result = database[1].find_str(8, "abcDE");)");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result"),
            sr::Value(42));
}

TEST_F(SiglusDatabaseBindingTest, ExactStringSearchExecutesThroughBinding) {
  // Exact string search returns its first matching item ID.
  Run(R"(result = database[1].find_str_real(8, "AbCde");)");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result"),
            sr::Value(42));
}

TEST_F(SiglusDatabaseBindingTest, SceneChangesKeepDatabaseContents) {
  // Two independently loaded scenes share the same immutable database state.
  InstallRead("first", 0, 42, 3);
  InstallRead("second", 1, 42, 8);
  Run("before = first(); result = second();");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result").Str(),
            "AbCde");
}

TEST_F(SiglusDatabaseBindingTest,
       RuntimeReinitializationKeepsDatabaseContents) {
  // Reset local runtime state after deleting the source; retain loaded data.
  InstallRead("title", 1, 42, 8);
  std::filesystem::remove(files.root / "dat/sample.dbs");
  for (auto& reset : runtime.reset_local_memory)
    reset();
  Run("result = title();");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result").Str(),
            "AbCde");
}

TEST_F(SiglusDatabaseBindingTest,
       UnexecutedUnsupportedCommandDoesNotBlockLoading) {
  // An unsupported command in an unused entry does not block scene bootstrap.
  binding::FakeScene scene{.scene_name = "unused", .command_name = "unused"};
  scene.AppendElement({3});
  scene.AppendByte(ByteCode::Cmd);
  for (int value : {0, 0, 0, static_cast<int>(Type::Int)})
    scene.AppendInt(value);
  const auto id = scene.AddTo(archive);
  EXPECT_NO_THROW(runtime.GameplayMachine().loader->Load(id));
}

TEST_F(SiglusDatabaseBindingTest,
       ExecutedUnsupportedCommandRaisesCheckedError) {
  // Executing an unresolved command raises an interpreter error before storing
  // a result.
  binding::FakeScene scene{.scene_name = "unsupported",
                           .command_name = "unsupported"};
  scene.AppendElement({3});
  scene.AppendByte(ByteCode::Cmd);
  for (int value : {0, 0, 0, static_cast<int>(Type::Int)})
    scene.AppendInt(value);
  scene.AddTo(archive);
  (*runtime.GameplayMachine().vm->globals_)["unsupported"] =
      runtime.GameplayMachine().loader->ResolveSceneCommand("unsupported",
                                                            "unsupported");
  try {
    Run("unsupported();");
    FAIL() << "expected checked execution error";
  } catch (const std::exception& e) {
    EXPECT_THAT(e.what(), HasSubstr("unsupported Siglus command"));
    EXPECT_THAT(e.what(), HasSubstr("int:3"));
  }
}

}  // namespace siglus_test
