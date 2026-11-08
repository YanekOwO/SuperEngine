// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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
#include "core/game_config.hpp"
#include "core/input.hpp"
#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/flow.hpp"
#include "libsiglus/bindings/loader.hpp"
#include "libsiglus/bindings/memory.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/intern_name.hpp"
#include "libsiglus/lexeme.hpp"
#include "libsiglus/recompiler.hpp"
#include "libsiglus/scene_entry.hpp"
#include "log_capture.hpp"
#include "m6/compiler_pipeline.hpp"
#include "siglus/fake_scene.hpp"
#include "srbind/srbind.hpp"
#include "vm/exception.hpp"
#include "vm/function.hpp"
#include "vm/future.hpp"
#include "vm/gc.hpp"
#include "vm/promise.hpp"
#include "vm/transfer_value.hpp"
#include "vm/value.hpp"

#include <any>
#include <chrono>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace libsiglus::binding {
namespace sr = serilang;

static sr::Function* Dispatcher(sr::Value entry) {
  auto* bound = entry.Get_if<sr::BoundMethod>();
  return bound->method.Get_if<sr::Function>();
}

static Archive MakeEmptyArchive() {
  return Archive(std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
}

static ::testing::AssertionResult InitializeFlowRuntime(SiglusRuntime& runtime,
                                                        Archive& archive) {
  runtime.archive = std::shared_ptr<Archive>(&archive, [](Archive*) {});
  runtime.binding_names = {"0_memory", "flow"};
  runtime.GameplayMachine().vm =
      std::make_unique<sr::VM>(std::make_shared<sr::GarbageCollector>());
  runtime.GameplayMachine().loader =
      std::make_unique<Loader>(archive, *runtime.GameplayMachine().vm, false);
  for (const auto* name : {"0_memory", "flow"}) {
    const auto* bind = SiglusBindingRegistry::Find(name);
    if (bind == nullptr)
      return ::testing::AssertionFailure()
             << "Missing Siglus binding: " << name;
    (*bind)(runtime.GameplayMachine());
  }
  return ::testing::AssertionSuccess();
}

class SiglusSceneEntryTest : public ::testing::Test {
 protected:
  Archive archive = MakeEmptyArchive();
  sr::VM vm{std::make_shared<sr::GarbageCollector>()};
  Loader loader{archive, vm, false};
  sr::Module* mod = nullptr;

  void AddCallbackScene(std::vector<Type> args) {
    FakeScene{.scene_name = "ui",
              .command_name = "animate",
              .args = std::move(args),
              .included = true}
        .AddTo(archive);
  }

  void SetUp() override {
    FakeScene{.scene_name = "menu", .command_name = "open"}.AddTo(archive);
    mod = loader.Load(0);
  }
};

TEST_F(SiglusSceneEntryTest, EntriesShareOneDispatcher) {
  // Scene, label, and command adapters all retain the same execution function.
  auto* dispatcher = mod->globals->at("%%dispatch").Get_if<sr::Function>();
  EXPECT_EQ(Dispatcher(mod->globals->at("%%script")), dispatcher);
  EXPECT_EQ(Dispatcher(mod->globals->at(GetZlabelId(0))), dispatcher);
  EXPECT_EQ(Dispatcher(mod->globals->at(GetUsercmdId(5))), dispatcher);
}

TEST_F(SiglusSceneEntryTest, NamedCommandResolvesThroughCachedMetadata) {
  // Resolve a named command from a loaded scene; return its command adapter.
  auto expected = mod->globals->at(GetUsercmdId(5));
  ASSERT_NE(expected.Get_if<sr::BoundMethod>(), nullptr);
  EXPECT_EQ(ResolveSceneCommand(loader, "MENU", "open"), expected);
}

TEST_F(SiglusSceneEntryTest, IncludedCommandLoadsDeclaringSceneOnDemand) {
  // Resolve an included command before its owner is loaded; load and cache
  // the declaring scene and command metadata.
  AddCallbackScene({Type::FrameAction, Type::Object});
  EXPECT_FALSE(vm.builtins_->contains("%%siglus_module@SCENE_0001"));

  const auto& resolved = loader.ResolveUserCommand(*mod, "AnImAtE");

  EXPECT_TRUE(vm.builtins_->contains("%%siglus_module@SCENE_0001"));
  const auto* owner = loader.Load(1);
  EXPECT_EQ(resolved.function, owner->globals->at(GetUsercmdId(5)));
  EXPECT_EQ(Dispatcher(resolved.function)->globals, owner->globals);
  EXPECT_EQ(resolved.args,
            (std::vector<Type>{Type::FrameAction, Type::Object}));
  EXPECT_EQ(&loader.ResolveUserCommand(*mod, "animate"), &resolved);
}

TEST_F(SiglusSceneEntryTest, IncludedCommandUsesAlreadyLoadedDeclaringScene) {
  // Preload the declaring scene; included lookup must reuse its adapter.
  AddCallbackScene({Type::FrameAction, Type::Object});
  const auto* owner = loader.Load(1);
  const auto expected = owner->globals->at(GetUsercmdId(5));

  EXPECT_EQ(ResolveSceneCommand(loader, "menu", "animate"), expected);
  EXPECT_EQ(loader.Load(1), owner);
}

TEST_F(SiglusSceneEntryTest, IncludedCommandTakesPrecedenceOverLocalCommand) {
  // Declare an included command with a local name; lookup selects the
  // included command.
  FakeScene{.scene_name = "shared",
            .command_name = "open",
            .args = {Type::Int},
            .included = true}
      .AddTo(archive);
  const auto& resolved = loader.ResolveUserCommand(*mod, "OPEN");

  EXPECT_EQ(Dispatcher(resolved.function)->globals, loader.Load(1)->globals);
  EXPECT_EQ(resolved.args, std::vector<Type>{Type::Int});
  EXPECT_NE(resolved.function, mod->globals->at(GetUsercmdId(5)));
}

TEST_F(SiglusSceneEntryTest,
       InvalidIncludedTargetDoesNotFallBackToLocalCommand) {
  // Point an included command at a missing scene; lookup must fail rather
  // than invoke the local command of the same name.
  archive.cmd_map_["open"] = archive.cmd_.size();
  archive.cmd_.push_back(Command{.scene_id = 99, .offset = 5, .name = "open"});

  EXPECT_THROW(loader.ResolveUserCommand(*mod, "open"), sr::RuntimeError);
}

TEST_F(SiglusSceneEntryTest, LocalCommandNamesAreCaseInsensitive) {
  // Use mixed-case local names; lookup ignores case but stays within the
  // selected scene.
  const int scene =
      FakeScene{.scene_name = "other", .command_name = "LoCaL"}.AddTo(archive);
  const auto* owner = loader.Load(scene);
  const auto expected = owner->globals->at(GetUsercmdId(5));

  EXPECT_EQ(loader.ResolveUserCommand(*owner, "LOCAL").function, expected);
  EXPECT_EQ(ResolveSceneCommand(loader, "menu", "OpEn"),
            mod->globals->at(GetUsercmdId(5)));
  EXPECT_THROW(loader.ResolveUserCommand(*mod, "local"), sr::RuntimeError);
}

TEST_F(SiglusSceneEntryTest, FiberResolvesIncludedCommandFromNearestScene) {
  // Surround a scene frame with native frames; lookup finds its included
  // command while ignoring native frames.
  AddCallbackScene({Type::FrameAction, Type::Object});
  sr::Fiber fiber;
  fiber.frames.push_back({});  // Native/non-scene frames are ignored.
  fiber.frames.push_back(
      {.fn = mod->globals->at("%%dispatch").Get_if<sr::Function>()});
  fiber.frames.push_back({});

  const auto& resolved = loader.ResolveUserCommand(fiber, "ANIMATE");
  EXPECT_EQ(Dispatcher(resolved.function)->globals, loader.Load(1)->globals);
}

TEST_F(SiglusSceneEntryTest, FiberDoesNotSearchOlderScenesForLocalCommands) {
  // Stack two scene frames; local lookup must use only the nearest scene.
  const int other =
      FakeScene{.scene_name = "other", .command_name = "local"}.AddTo(archive);
  const auto* current = loader.Load(other);
  sr::Fiber fiber;
  fiber.frames.push_back(
      {.fn = mod->globals->at("%%dispatch").Get_if<sr::Function>()});
  fiber.frames.push_back(
      {.fn = current->globals->at("%%dispatch").Get_if<sr::Function>()});

  EXPECT_EQ(
      Dispatcher(loader.ResolveUserCommand(fiber, "LOCAL").function)->globals,
      current->globals);
  EXPECT_THROW(loader.ResolveUserCommand(fiber, "open"), sr::RuntimeError);
}

TEST_F(SiglusSceneEntryTest, IncludedCommandsStillRequireSceneContext) {
  // Resolve an included command without a loaded scene context; both
  // fiber and module lookup must reject the request.
  AddCallbackScene({Type::FrameAction, Type::Object});
  sr::Fiber fiber;
  EXPECT_THROW(loader.ResolveUserCommand(fiber, "animate"), sr::RuntimeError);
  sr::Module unloaded("unloaded");
  EXPECT_THROW(loader.ResolveUserCommand(unloaded, "animate"),
               sr::RuntimeError);
}

TEST_F(SiglusSceneEntryTest, MissingNamedCommandDoesNotFallBackToScript) {
  // Request a missing command in a valid scene; lookup must fail rather
  // than invoke the scene script.
  ASSERT_NE(mod->globals->at("%%dispatch").Get_if<sr::Function>(), nullptr);
  EXPECT_THROW(ResolveSceneCommand(loader, "menu", "missing"),
               sr::RuntimeError);
}

TEST_F(SiglusSceneEntryTest, ZlabelResolvesLabelAndFallsBackToScript) {
  // Resolve present and absent labels; return the label adapter or the
  // scene script fallback respectively.
  auto label = mod->globals->at(GetZlabelId(0));
  auto script = mod->globals->at("%%script");
  ASSERT_NE(label.Get_if<sr::BoundMethod>(), nullptr);
  ASSERT_NE(script.Get_if<sr::BoundMethod>(), nullptr);
  EXPECT_EQ(ResolveSceneEntry(loader, "MENU", 0), label);
  EXPECT_EQ(ResolveSceneEntry(loader, "menu", 77), script);
}

// ------------------------------------------------------------------------------

class SiglusCallMemoryTest : public ::testing::Test {
 protected:
  Archive archive = MakeEmptyArchive();
  SiglusRuntime runtime;
  struct Observation {
    int integer;
    std::string string;
    std::size_t depth;
    std::size_t frames = 0;
  };
  std::vector<Observation> observations;

  void SetUp() override {
    ASSERT_TRUE(InitializeFlowRuntime(runtime, archive));
    srbind::module_ bindings(runtime.GameplayMachine().vm->gc_.get(),
                             runtime.GameplayMachine().vm->globals_.get());
    bindings.def("observe", [this](sr::Fiber& fiber, int i, std::string s) {
      auto* memory = std::any_cast<MemoryHandle>(&fiber.usrdata);
      ASSERT_NE(memory, nullptr);
      observations.push_back(
          {i, std::move(s), memory->StackDepth(), fiber.frames.size()});
    });
  }

  void Run(std::string source) {
    m6::CompilerPipeline compiler(runtime.GameplayMachine().vm->gc_, false);
    compiler.compile(
        m6::SourceBuffer::Create(std::move(source), "<SiglusCallMemoryTest>"));
    ASSERT_TRUE(compiler.Ok()) << compiler.FormatErrors();
    runtime.GameplayMachine().vm->Evaluate(compiler.Get());
  }

  // Raw m6 calls adopt the caller-owned contract. Emit the guard inline so
  // these fixtures do not introduce a bank-management function frame.
  std::string BankedCall(std::string callee,
                         std::vector<std::string> args,
                         std::string result = "ignored",
                         std::string integers = "[]",
                         std::string strings = "[]") {
    const auto prefix = "__banked_" + std::to_string(next_call_++) + "_";
    std::string source = prefix + "callee = " + callee + ";";
    std::string call = prefix + "callee(";
    for (std::size_t i = 0; i < args.size(); ++i) {
      const auto name = prefix + "arg" + std::to_string(i);
      source += name + " = " + args[i] + ";";
      if (i)
        call += ",";
      call += name;
    }
    call += ")";
    return source + "__builtin_push_frame(" + integers + "," + strings +
           ");"
           "try { " +
           result + " = " + call + "; } catch (" + prefix +
           "error) {"
           "__builtin_pop_frame(); throw " +
           prefix +
           "error; }"
           "__builtin_pop_frame();";
  }
  int next_call_ = 0;

  std::shared_ptr<sr::Promise> InstallGate() {
    auto* gate = runtime.GameplayMachine().vm->gc_->Allocate<sr::Future>();
    (*runtime.GameplayMachine().vm->globals_)["gate"] = gate;
    return gate->promise;
  }

  static token::GetProperty BankValue(const char* name, Type type, int id) {
    return {.chain =
                elm::AccessChain{
                    .nodes = {elm::Node(type == Type::Int ? Type::IntList
                                                          : Type::StrList,
                                        elm::Member(name)),
                              elm::Node(type, elm::Subscript{Integer(0)})}},
            .dst = Variable{type, id}};
  }

  void InstallTokens(std::vector<token::Token_t> body, bool trace = false) {
    std::vector<Parser::ParsedToken> tokens;
    for (auto& token : body)
      tokens.push_back({static_cast<int>(tokens.size() + 1), std::move(token)});
    Recompiler compiler(runtime.GameplayMachine().vm->gc_);
    compiler.trace_enabled_ = trace;
    compiler.Compile(tokens);
    ASSERT_TRUE(compiler.Ok());
    auto* module = compiler.module_;
    (*runtime.GameplayMachine().vm->builtins_)["test_scene"] =
        sr::Value(module);
    sr::VM bootstrap(runtime.GameplayMachine().vm->gc_);
    bootstrap.gc_threshold_ = 0;
    bootstrap.globals_ = module->globals;
    bootstrap.Evaluate(compiler.GetCode());
    auto* dispatcher = module->globals->at("%%dispatch").Get_if<sr::Function>();
    (*runtime.GameplayMachine().vm->globals_)["entry"] =
        MakeSceneEntry(*runtime.GameplayMachine().vm->gc_, dispatcher,
                       compiler.GetMainLine(), 0);
  }

  static Variable Temp(int id) { return Variable{.type = Type::Int, .id = id}; }

  static token::GetProperty Argument(int id, int destination) {
    return token::GetProperty{
        .chain = elm::AccessChain{.root = elm::Root(Type::Int, elm::Arg{id})},
        .dst = Temp(destination)};
  }

  static token::Command UserCall(int scene, int value) {
    elm::Usrcmd command{.scene = scene, .entry = 5, .name = "probe"};
    command.arguments.arg = {Integer(value)};
    return token::Command{
        .chain = elm::AccessChain{.root = elm::Root(Type::String, command)},
        .dst = Variable{.type = Type::String, .id = 0}};
  }

  static token::Command FarCall(std::string scene, bool extended = false) {
    elm::Farcall call;
    call.scn_name = String(std::move(scene));
    call.zlabel = Integer(0);
    call.intargs = {Integer(7)};
    call.strargs = {String("farcall")};
    call.extended = extended;
    return token::Command{
        .chain = elm::AccessChain{.root = elm::Root(Type::String, call)},
        .dst = Variable{.type = Type::String, .id = 0}};
  }

  static token::Command SceneJump(Value scene, Value zlabel = Integer(0)) {
    return {.chain =
                elm::AccessChain{.root = elm::Root(
                                     Type::None, elm::Jump{std::move(scene),
                                                           std::move(zlabel)})},
            .dst = Variable{Type::None, 0}};
  }

  int AddJumpScene(std::string name, int destination) {
    FakeScene scene{.scene_name = name, .command_name = name};
    scene.AppendElement({4});
    scene.AppendByte(ByteCode::Push);
    scene.AppendInt(static_cast<int>(Type::Int));
    scene.AppendInt(destination);
    scene.AppendByte(ByteCode::Cmd);
    for (int value :
         {0, 1, static_cast<int>(Type::Int), 0, static_cast<int>(Type::None)})
      scene.AppendInt(value);
    return scene.AddTo(archive);
  }

  void AddFarcallProbe(std::string body) {
    // The scene calls a substitutable engine callable with a constant argument.
    FakeScene scene{.scene_name = "far", .command_name = "far"};
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
    scene.AddTo(archive);
    Run("fn probe(n) { " + body + " }");
    srbind::module_ math(*runtime.GameplayMachine().vm, "math");
    (*math.dict())["tostr"] =
        runtime.GameplayMachine().vm->globals_->at("probe");
  }

  void AddExtendedProbe(std::string body,
                        std::shared_ptr<sr::Promise> gate = {}) {
    AddFarcallProbe(body);
    runtime.configure_machine = [this, body = std::move(body),
                                 gate =
                                     std::move(gate)](SiglusMachine& machine) {
      srbind::module_ bindings(machine.vm->gc_.get(),
                               machine.vm->globals_.get());
      bindings.def("observe",
                   [this](sr::Fiber& fiber, int integer, std::string text) {
                     auto* memory = std::any_cast<MemoryHandle>(&fiber.usrdata);
                     ASSERT_NE(memory, nullptr);
                     observations.push_back(
                         {integer, std::move(text), memory->StackDepth()});
                   });
      if (gate) {
        auto* future = machine.vm->gc_->Allocate<sr::Future>();
        future->promise = gate;
        (*machine.vm->globals_)["gate"] = sr::Value(future);
      }
      Execute(*machine.vm, "fn probe(n) { " + body + " }");
      srbind::module_ math(*machine.vm, "math");
      (*math.dict())["tostr"] = machine.vm->globals_->at("probe");
    };
  }

  void StartExtended(const std::string& body) {
    Run("fn start_extended() { " + body + " }");
    runtime.GameplayMachine().vm->InvokeUntilBlocked(
        runtime.GameplayMachine().vm->globals_->at("start_extended"), {});
    DriveScripts();
  }

  void DriveScripts() {
    for (int iteration = 0; iteration < 4; ++iteration)
      runtime.DriveScripts();
  }

  sr::Value AddProbe(std::string name, std::string body) {
    // A real Siglus user command forwards its argument to an engine callable.
    // The test substitutes that callable to inspect the active L/K banks.
    FakeScene scene{
        .scene_name = name, .command_name = name, .args = {Type::Int}};
    scene.AppendElement({39, 1});  // math.tostr
    scene.AppendElement({83, 0x7d000000});
    scene.AppendByte(ByteCode::Property);
    scene.AppendByte(ByteCode::Cmd);
    for (int value :
         {0, 1, static_cast<int>(Type::Int), 0, static_cast<int>(Type::String)})
      scene.AppendInt(value);
    scene.AppendByte(ByteCode::Return);
    scene.AppendInt(1);
    scene.AppendInt(static_cast<int>(Type::String));
    const int scene_id = scene.AddTo(archive);

    Run("fn probe(n) { " + body + " }");
    srbind::module_ math(*runtime.GameplayMachine().vm, "math");
    (*math.dict())["tostr"] =
        runtime.GameplayMachine().vm->globals_->at("probe");
    auto command =
        ResolveSceneCommand(*runtime.GameplayMachine().loader, name, name);
    (*runtime.GameplayMachine().vm->globals_)["command"] = sr::Value(command);
    EXPECT_EQ(Dispatcher(command)->globals,
              runtime.GameplayMachine().loader->Load(scene_id)->globals);
    return command;
  }
};

TEST_F(SiglusCallMemoryTest, ConsecutiveCommandsStartFreshAndRestoreCaller) {
  // Explicit caller guards give each raw command fresh banks and restore root.
  AddProbe(
      "face",
      "observe(L[0], K[0]); L[0] = 6; K[0] = \"callee\"; return \"face\";");
  Run("L[0] = 42; K[0] = \"caller\";" +
      BankedCall("__builtin_usrcmd(0, 5, \"face\")", {"0"}, "first") +
      BankedCall("__builtin_usrcmd(0, 5, \"face\")", {"0"}, "second") +
      "observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 3);
  for (int i : {0, 1}) {
    EXPECT_EQ(observations[i].integer, 0);
    EXPECT_EQ(observations[i].string, "");
    EXPECT_EQ(observations[i].depth, 2);
  }
  EXPECT_EQ(observations[2].integer, 42);
  EXPECT_EQ(observations[2].string, "caller");
  EXPECT_EQ(observations[2].depth, 1);
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("first").Str(), "face");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("second").Str(), "face");
}

TEST_F(SiglusCallMemoryTest, RecursiveCommandsRestoreEveryCaller) {
  // Every recursive m6 caller guards its own bank pair until its callee exits.
  AddProbe("recursive",
           "observe(L[0], K[0]); L[0] = n; K[0] = \"level\";"
           "if (n > 0) { " +
               BankedCall("command", {"n - 1"}) +
               " }"
               "observe(L[0], K[0]); return \"done\";");
  Run("L[0] = 99; K[0] = \"root\";" + BankedCall("command", {"2"}, "result") +
      "observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 7);
  for (int i : {0, 1, 2}) {
    EXPECT_EQ(observations[i].integer, 0);
    EXPECT_EQ(observations[i].string, "");
    EXPECT_EQ(observations[i].depth, i + 2);
  }
  for (int i : {3, 4, 5}) {
    EXPECT_EQ(observations[i].integer, i - 3);
    EXPECT_EQ(observations[i].string, "level");
    EXPECT_EQ(observations[i].depth, 7 - i);
  }
  EXPECT_EQ(observations[6].integer, 99);
  EXPECT_EQ(observations[6].string, "root");
  EXPECT_EQ(observations[6].depth, 1);
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result").Str(), "done");
}

TEST_F(SiglusCallMemoryTest, ExceptionRestoresCallerBeforeCatch) {
  // The explicit caller guard releases banks before an outer catch observes
  // them.
  AddProbe("failure", "L[0] = 6; K[0] = \"callee\"; throw \"failure\";");
  Run("L[0] = 42; K[0] = \"caller\"; try {" + BankedCall("command", {"0"}) +
      "} catch (e) { error = e; } observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].integer, 42);
  EXPECT_EQ(observations[0].string, "caller");
  EXPECT_EQ(observations[0].depth, 1);
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("error").Str(),
            "failure");
}

TEST_F(SiglusCallMemoryTest, DirectCallbackKeepsBanksUntilResumed) {
  auto command = AddProbe("callback", R"(
    observe(L[0], K[0]);
    L[0] = n; K[0] = "suspended";
    await gate;
    observe(L[0], K[0]);
    return "resumed";
  )");
  auto* gate = runtime.GameplayMachine().vm->gc_->Allocate<sr::Fiber>();
  (*runtime.GameplayMachine().vm->globals_)["gate"] = sr::Value(gate);
  runtime.GameplayMachine().vm->InvokeUntilBlocked(
      command, {sr::Value(17)}, MakeCallMemory(*runtime.memory));

  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].integer, 0);
  EXPECT_EQ(observations[0].string, "");
  EXPECT_EQ(observations[0].depth, 1);
  ASSERT_EQ(runtime.GameplayMachine().vm->fibres_.size(), 1);
  auto* callback = runtime.GameplayMachine().vm->fibres_.front();
  EXPECT_EQ(callback->state, sr::FiberState::Suspended);
  std::size_t completed_depth = 0;
  callback->completion_promise->AddWaker([&](const auto& result) {
    EXPECT_TRUE(result.has_value());
    completed_depth =
        std::any_cast<MemoryHandle>(&callback->usrdata)->StackDepth();
  });

  gate->completion_promise->Resolve(sr::nil);
  runtime.GameplayMachine().vm->Run();

  ASSERT_EQ(observations.size(), 2);
  EXPECT_EQ(observations[1].integer, 17);
  EXPECT_EQ(observations[1].string, "suspended");
  EXPECT_EQ(observations[1].depth, 1);
  EXPECT_EQ(completed_depth, 1);
}

TEST_F(SiglusCallMemoryTest, FarcallBanksSurviveNestedUserCommand) {
  // A nested caller guard restores the explicit farcall banks before root.
  AddProbe("nested", "L[0] = 6; K[0] = \"callee\"; return \"done\";");
  Run("L[0] = 42; K[0] = \"root\"; __builtin_push_frame([7], [\"farcall\"]);" +
      BankedCall("command", {"0"}) +
      "observe(L[0], K[0]);"
      "__builtin_pop_frame(); observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 2);
  EXPECT_EQ(observations[0].integer, 7);
  EXPECT_EQ(observations[0].string, "farcall");
  EXPECT_EQ(observations[0].depth, 2);
  EXPECT_EQ(observations[1].integer, 42);
  EXPECT_EQ(observations[1].string, "root");
  EXPECT_EQ(observations[1].depth, 1);
}

TEST_F(SiglusCallMemoryTest, PreservesArgumentTypesAndReturnValues) {
  // Invoke commands returning integer, string, or no value; preserve each
  // result's type and value.
  for (const auto type : {Type::Int, Type::String}) {
    const std::string name = type == Type::Int ? "integer" : "string";
    FakeScene scene{.scene_name = name, .command_name = name, .args = {type}};
    scene.AppendElement({83, 0x7d000000});
    scene.AppendByte(ByteCode::Property);
    scene.AppendByte(ByteCode::Return);
    scene.AppendInt(1);
    scene.AppendInt(static_cast<int>(type));
    scene.AddTo(archive);
    (*runtime.GameplayMachine().vm->globals_)[name] = sr::Value(
        ResolveSceneCommand(*runtime.GameplayMachine().loader, name, name));
  }
  FakeScene{.scene_name = "empty", .command_name = "empty"}.AddTo(archive);
  (*runtime.GameplayMachine().vm->globals_)["empty"] = sr::Value(
      ResolveSceneCommand(*runtime.GameplayMachine().loader, "empty", "empty"));
  Run(R"(
    integer_result = integer(73);
    string_result = string("unchanged");
    empty_result = empty();
  )");

  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("integer_result"),
            sr::Value(73));
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("string_result").Str(),
            "unchanged");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("empty_result"),
            sr::nil);
}

TEST_F(SiglusCallMemoryTest, ZeroInstructionTokensRemainJumpTargets) {
  // Consecutive labels have token entries even when neither emits bytecode.
  InstallTokens({token::Goto{1}, token::Return{{Integer(0)}}, token::Label{0},
                 token::Label{1}, token::Return{{Integer(42)}}, token::Eof{}});
  auto* code =
      Dispatcher(runtime.GameplayMachine().vm->globals_->at("entry"))->chunk;
  EXPECT_EQ(code->line_offset.size(), 6);
  EXPECT_EQ(code->line_offset.at(3), code->line_offset.at(4));
  Run("result = entry();");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result"),
            sr::Value(42));
}

TEST_F(SiglusCallMemoryTest, TraceAnnotationsRemainAtTokenBoundaries) {
  // Trace instrumentation runs at the target token before the scene resumes.
  std::vector<std::string> annotations;
  srbind::module_ bindings(runtime.GameplayMachine().vm->gc_.get(),
                           runtime.GameplayMachine().vm->globals_.get());
  bindings.def(
      "__builtin_trace",
      [&](int scene, int line, std::vector<sr::Value> terms) {
        std::string text = std::format("[{}:{}] ", scene, line);
        for (const auto& value : terms)
          text += value.Str();
        annotations.push_back(std::move(text));
      },
      srbind::arg("scene"), srbind::arg("line"), srbind::vararg);
  InstallTokens({token::Goto{1}, token::Return{{Integer(0)}}, token::Label{0},
                 token::Label{1}, token::Return{{Integer(42)}}, token::Eof{}},
                true);
  Run("result = entry();");
  ASSERT_EQ(annotations.size(), 3);
  EXPECT_NE(annotations[1].find(":4]"), std::string::npos);
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result"),
            sr::Value(42));
}

TEST_F(SiglusCallMemoryTest, SceneFallthroughReturnsNil) {
  // EOF supplies normal termination through the dispatcher's shared epilogue.
  InstallTokens({token::Label{0}, token::Eof{}});
  Run("result = entry();");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result"), sr::nil);
}

TEST_F(SiglusCallMemoryTest, RecompiledGosubReturnsItsArgument) {
  // Enter a zero-instruction label and return the supplied argument.
  InstallTokens(
      {token::Gosub{.entry_id = 0, .args = {Integer(73)}, .dst = Temp(0)},
       token::Return{{Temp(0)}}, token::Label{0},
       token::Subroutine{.source_entry = 42, .args = {Type::Int}},
       Argument(0, 1), token::Return{{Temp(1)}}, token::Eof{}});
  Run("result = entry();");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result"),
            sr::Value(73));
}

TEST_F(SiglusCallMemoryTest, NestedGosubPreservesCallerLocals) {
  // A nested call overwrites its argument slot; its caller retains its own
  // slot.
  InstallTokens(
      {token::Gosub{.entry_id = 0, .args = {Integer(73)}, .dst = Temp(0)},
       token::Return{{Temp(0)}}, token::Label{0},
       token::Subroutine{.source_entry = 42, .args = {Type::Int}},
       token::Gosub{.entry_id = 1, .args = {Integer(99)}, .dst = Temp(1)},
       Argument(0, 2), token::Return{{Temp(2)}}, token::Label{1},
       token::Subroutine{.source_entry = 43, .args = {Type::Int}},
       Argument(0, 3), token::Return{{Temp(3)}}, token::Eof{}});
  Run("result = entry();");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result"),
            sr::Value(73));
}

TEST_F(SiglusCallMemoryTest, NestedGosubPreservesDeclaredLocal) {
  // A nested invocation gets its own declared-local slot and preserves its
  // caller.
  const elm::AccessChain local{.root = elm::Root(Type::Int, elm::Arg{1})};
  InstallTokens(
      {token::Gosub{.entry_id = 0, .args = {Integer(73)}, .dst = Temp(0)},
       token::Return{{Temp(0)}}, token::Label{0},
       token::Subroutine{.source_entry = 42, .args = {Type::Int}},
       token::LocalVar{.id = 1, .type = Type::Int, .size = 0},
       token::Assign{.dst = local, .src = Integer(42)},
       token::Gosub{.entry_id = 1, .args = {}, .dst = Temp(1)}, Argument(1, 2),
       token::Return{{Temp(2)}}, token::Label{1},
       token::Subroutine{.source_entry = 43},
       token::LocalVar{.id = 0, .type = Type::Int, .size = 0},
       token::LocalVar{.id = 1, .type = Type::Int, .size = 0},
       token::Assign{.dst = local, .src = Integer(99)}, token::Return{},
       token::Eof{}});
  Run("result = entry();");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result"),
            sr::Value(42));
}

TEST_F(SiglusCallMemoryTest, MultipleReturnValuesRemainOrdered) {
  // A dispatcher return preserves the original multi-value list representation.
  InstallTokens({token::Return{{Integer(42), String("value")}}, token::Eof{}});
  Run("result = entry();");
  auto* values =
      runtime.GameplayMachine().vm->globals_->at("result").Get_if<sr::List>();
  ASSERT_NE(values, nullptr);
  ASSERT_EQ(values->items.size(), 2);
  EXPECT_EQ(values->items[0], sr::Value(42));
  EXPECT_EQ(values->items[1].Str(), "value");
}

TEST_F(SiglusCallMemoryTest, SuspendedGosubPreservesItsArgument) {
  // Suspend in a subroutine and retain its argument when its caller resumes.
  auto* gate = runtime.GameplayMachine().vm->gc_->Allocate<sr::Fiber>();
  (*runtime.GameplayMachine().vm->globals_)["gate"] = sr::Value(gate);
  srbind::module_ bindings(runtime.GameplayMachine().vm->gc_.get(),
                           runtime.GameplayMachine().vm->globals_.get());
  bindings.def("__builtin_textout",
               [gate](int, int, std::string) { return sr::Value(gate); });
  InstallTokens(
      {token::Gosub{.entry_id = 0, .args = {Integer(73)}, .dst = Temp(0)},
       token::Return{{Temp(0)}}, token::Label{0},
       token::Subroutine{.source_entry = 42, .args = {Type::Int}},
       token::Textout{.kidoku = 0, .str = String("pause")}, Argument(0, 1),
       token::Return{{Temp(1)}}, token::Eof{}});
  Run("fn start() { global result; result = entry(); }");
  runtime.GameplayMachine().vm->InvokeUntilBlocked(
      runtime.GameplayMachine().vm->globals_->at("start"), {});
  ASSERT_EQ(runtime.GameplayMachine().vm->fibres_.size(), 1);
  EXPECT_EQ(runtime.GameplayMachine().vm->fibres_.front()->state,
            sr::FiberState::Suspended);
  gate->completion_promise->Resolve(sr::nil);
  runtime.GameplayMachine().vm->Run();
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result"),
            sr::Value(73));
}

TEST_F(SiglusCallMemoryTest, RawCommandUsesCallerBanks) {
  // An unguarded m6 invocation shares the caller's banks under the new
  // contract.
  AddProbe("raw", "observe(L[0], K[0]); L[0] = n; return n;");
  Run("L[0] = 42; K[0] = \"caller\"; command(7); observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 2);
  EXPECT_EQ(observations[0].integer, 42);
  EXPECT_EQ(observations[0].string, "caller");
  EXPECT_EQ(observations[0].depth, 1);
  EXPECT_EQ(observations[1].integer, 7);
  EXPECT_EQ(observations[1].depth, 1);
}

TEST_F(SiglusCallMemoryTest, RecompiledCommandAddsNoWrapperFrame) {
  // The root m6 frame, two dispatchers and probe account for every active
  // frame.
  AddProbe("probe", "observe(L[0], K[0]); return n;");
  InstallTokens({UserCall(0, 19), token::Return{{Temp(0)}}, token::Eof{}});
  Run("result = entry();");
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].frames, 4);
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result"),
            sr::Value(19));
}

TEST_F(SiglusCallMemoryTest, RecompiledCommandPassesCallerBankArgument) {
  // Read a caller bank into an argument before acquiring empty callee banks.
  AddProbe("probe", "observe(n, K[0]); return n;");
  auto call = UserCall(0, 0);
  std::get<elm::Usrcmd>(call.chain.root.var).arguments.arg = {Temp(0)};
  call.dst = Temp(1);
  InstallTokens({BankValue("L", Type::Int, 0), call, token::Return{{Temp(1)}},
                 token::Eof{}});
  Run("L[0] = 42; K[0] = \"caller\"; result = entry();");
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].integer, 42);
  EXPECT_EQ(observations[0].string, "");
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result"),
            sr::Value(42));
}

TEST_F(SiglusCallMemoryTest, RecompiledCommandRestoresBanksAfterException) {
  // A failing callee unwinds through the emitted caller guard before catching.
  AddProbe("probe", "L[0] = 99; K[0] = \"changed\"; throw \"failure\";");
  InstallTokens({UserCall(0, 19), token::Eof{}});
  Run("L[0] = 42; K[0] = \"caller\"; try { entry(); } catch (e) { error = e; }"
      "observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].integer, 42);
  EXPECT_EQ(observations[0].string, "caller");
  EXPECT_EQ(observations[0].depth, 1);
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("error").Str(),
            "failure");
}

TEST_F(SiglusCallMemoryTest, SuspendedCommandRetainsCallerGuard) {
  // The emitted guard retains callee banks through await and pops after return.
  AddProbe("probe",
           "observe(L[0], K[0]); L[0] = n; K[0] = \"held\";"
           "await gate; observe(L[0], K[0]); return \"done\";");
  auto gate = InstallGate();
  InstallTokens({UserCall(0, 19), token::Return{{Temp(0)}}, token::Eof{}});
  Run("fn start() { global result; L[0] = 42; K[0] = \"caller\";"
      "result = entry(); observe(L[0], K[0]); }");
  runtime.GameplayMachine().vm->InvokeUntilBlocked(
      runtime.GameplayMachine().vm->globals_->at("start"), {});
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].depth, 2);
  runtime.GameplayMachine().vm->CollectGarbage();
  gate->Resolve(sr::nil);
  runtime.GameplayMachine().vm->RunReadyUntilBlocked();
  ASSERT_EQ(observations.size(), 3);
  EXPECT_EQ(observations[1].integer, 19);
  EXPECT_EQ(observations[1].string, "held");
  EXPECT_EQ(observations[1].depth, 2);
  EXPECT_EQ(observations[2].integer, 42);
  EXPECT_EQ(observations[2].string, "caller");
  EXPECT_EQ(observations[2].depth, 1);
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result").Str(), "done");
}

TEST_F(SiglusCallMemoryTest, RejectedCommandAwaitRestoresCallerBanks) {
  // Rejection resumes inside a callee and reaches the caller's cleanup guard.
  AddProbe("probe", "L[0] = 99; await gate; return n;");
  auto gate = InstallGate();
  InstallTokens({UserCall(0, 19), token::Eof{}});
  Run("fn start() { global error; L[0] = 42; K[0] = \"caller\";"
      "try { entry(); } catch (e) { error = e; } observe(L[0], K[0]); }");
  runtime.GameplayMachine().vm->InvokeUntilBlocked(
      runtime.GameplayMachine().vm->globals_->at("start"), {});
  gate->Reject("rejected");
  runtime.GameplayMachine().vm->RunReadyUntilBlocked();
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].integer, 42);
  EXPECT_EQ(observations[0].string, "caller");
  EXPECT_EQ(observations[0].depth, 1);
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("error").Str(),
            "rejected");
}

TEST_F(SiglusCallMemoryTest, FarcallSeedsReadCallerBanks) {
  // Both typed seed lists are evaluated while the caller's banks are active.
  AddFarcallProbe("observe(L[0], K[0]); return n;");
  auto call = FarCall("far");
  auto& target = std::get<elm::Farcall>(call.chain.root.var);
  target.intargs = {Temp(0)};
  target.strargs = {Variable{Type::String, 1}};
  call.dst = Temp(2);
  InstallTokens({BankValue("L", Type::Int, 0), BankValue("K", Type::String, 1),
                 call, token::Eof{}});
  Run("L[0] = 42; K[0] = \"caller\"; entry();");
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].integer, 42);
  EXPECT_EQ(observations[0].string, "caller");
}

TEST_F(SiglusCallMemoryTest, BankedCommandPreservesReturnValue) {
  // Cleanup preserves nil, primitive and aggregate results on the operand
  // stack.
  for (const auto expression : {"nil", "73", "\"text\"", "[7, \"value\"]"}) {
    SCOPED_TRACE(expression);
    AddProbe("value" + std::to_string(archive.GetScenarioCount()),
             std::string("return ") + expression + ";");
    InstallTokens({UserCall(archive.GetScenarioCount() - 1, 0),
                   token::Return{{Temp(0)}}, token::Eof{}});
    Run(std::string("result = entry(); expected = ") + expression + ";");
    const auto& globals = *runtime.GameplayMachine().vm->globals_;
    EXPECT_EQ(sr::TransferValue::Capture(globals.at("result")),
              sr::TransferValue::Capture(globals.at("expected")));
  }
}

TEST_F(SiglusCallMemoryTest, RecursiveRecompiledCallsRestoreEveryBank) {
  // Recursive entry() calls execute the generated guard at every depth.
  AddProbe("recursive",
           "global remaining; observe(L[0], K[0]);"
           "L[0] = remaining; K[0] = \"level\";"
           "if (remaining > 0) { remaining = remaining - 1; entry(); }"
           "observe(L[0], K[0]); return n;");
  InstallTokens({UserCall(0, 19), token::Return{{Temp(0)}}, token::Eof{}});
  Run("remaining = 2; L[0] = 42; K[0] = \"root\"; entry(); observe(L[0], "
      "K[0]);");
  ASSERT_EQ(observations.size(), 7);
  for (int i : {0, 1, 2}) {
    EXPECT_EQ(observations[i].integer, 0);
    EXPECT_EQ(observations[i].depth, i + 2);
  }
  for (int i : {3, 4, 5}) {
    EXPECT_EQ(observations[i].integer, i - 3);
    EXPECT_EQ(observations[i].depth, 7 - i);
  }
  EXPECT_EQ(observations[6].integer, 42);
  EXPECT_EQ(observations[6].string, "root");
  EXPECT_EQ(observations[6].depth, 1);
}

TEST_F(SiglusCallMemoryTest, NestedRecompiledErrorsReleaseEveryBank) {
  // Failure in the innermost command traverses each generated cleanup handler.
  AddProbe("recursive",
           "global remaining; L[0] = 99;"
           "if (remaining > 0) { remaining = remaining - 1; entry(); }"
           "throw \"nested failure\";");
  InstallTokens({UserCall(0, 19), token::Eof{}});
  Run("remaining = 2; L[0] = 42; K[0] = \"root\";"
      "try { entry(); } catch (e) { error = e; } observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].integer, 42);
  EXPECT_EQ(observations[0].string, "root");
  EXPECT_EQ(observations[0].depth, 1);
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("error").Str(),
            "nested failure");
}

TEST_F(SiglusCallMemoryTest, RecompiledUserCommandRestoresCallerBanks) {
  // A recompiled command receives a fresh bank and restores its caller on
  // return.
  AddProbe("probe", "observe(L[0], K[0]); L[0] = n; return n;");
  InstallTokens({UserCall(0, 19), token::Return{{Temp(0)}}, token::Eof{}});
  Run("L[0] = 42; K[0] = \"root\"; result = entry(); observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 2);
  EXPECT_EQ(observations[0].integer, 0);
  EXPECT_EQ(observations[1].integer, 42);
  EXPECT_EQ(observations[1].depth, 1);
}

TEST_F(SiglusCallMemoryTest, RecompiledFarcallSeedsItsBanks) {
  // Farcall arguments initialize the destination's L/K banks.
  AddFarcallProbe("observe(L[0], K[0]); return \"done\";");
  InstallTokens({FarCall("far"), token::Return{{Temp(0)}}, token::Eof{}});
  Run("result = entry();");
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].integer, 7);
  EXPECT_EQ(observations[0].string, "farcall");
  EXPECT_EQ(observations[0].depth, 2);
}

TEST_F(SiglusCallMemoryTest, RecompiledFarcallRestoresBanksAfterException) {
  // Throw from the destination and observe the original banks after catching.
  AddFarcallProbe("L[0] = 99; throw \"failure\";");
  InstallTokens({FarCall("far"), token::Return{{Temp(0)}}, token::Eof{}});
  Run("L[0] = 42; K[0] = \"root\"; try { entry(); } catch (e) {} "
      "observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].integer, 42);
  EXPECT_EQ(observations[0].string, "root");
  EXPECT_EQ(observations[0].depth, 1);
}

TEST_F(SiglusCallMemoryTest, SuspendedFarcallKeepsBanksUntilResumed) {
  // Suspend inside a real farcall; resume with its banks before restoring root.
  AddFarcallProbe(
      "observe(L[0], K[0]); await gate; "
      "observe(L[0], K[0]); return \"done\";");
  InstallTokens({FarCall("far"), token::Return{{Temp(0)}}, token::Eof{}});
  auto* gate = runtime.GameplayMachine().vm->gc_->Allocate<sr::Fiber>();
  (*runtime.GameplayMachine().vm->globals_)["gate"] = sr::Value(gate);
  Run("fn start() { L[0] = 42; K[0] = \"root\"; entry(); observe(L[0], K[0]); "
      "}");
  auto start = runtime.GameplayMachine().vm->globals_->at("start");
  runtime.GameplayMachine().vm->InvokeUntilBlocked(start, {});
  ASSERT_EQ(observations.size(), 1);
  runtime.GameplayMachine().vm->CollectGarbage();
  gate->completion_promise->Resolve(sr::nil);
  runtime.GameplayMachine().vm->Run();
  ASSERT_EQ(observations.size(), 3);
  EXPECT_EQ(observations[1].integer, 7);
  EXPECT_EQ(observations[1].string, "farcall");
  EXPECT_EQ(observations[2].integer, 42);
  EXPECT_EQ(observations[2].depth, 1);
}

TEST_F(SiglusCallMemoryTest, ExtendedCallRestoresBanksAndClearsInput) {
  // Return from an extended call; restore caller banks and clear pending input.
  runtime.script_input = std::make_shared<ScriptInput>();
  runtime.script_input->OnEvent(
      std::make_shared<Event>(KeyDown{KeyCode::SPACE}));
  EXPECT_TRUE(runtime.script_input->Get(ScriptInput::Source::Decide).on_down);
  AddExtendedProbe("observe(L[0], K[0]); return n;");
  InstallTokens({FarCall("far", true), token::Eof{}});
  StartExtended("L[0] = 42; K[0] = \"root\"; entry(); observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 2);
  EXPECT_EQ(observations[0].integer, 7);
  EXPECT_EQ(observations[0].string, "farcall");
  EXPECT_EQ(observations[1].integer, 42);
  EXPECT_EQ(observations[1].string, "root");
  EXPECT_EQ(observations[1].depth, 1);
  EXPECT_FALSE(runtime.script_input->Get(ScriptInput::Source::Decide).on_down);
}

TEST_F(SiglusCallMemoryTest, ExtendedStringResultOutlivesItsExecutionHeap) {
  // Destroy a child after return; its copied result remains rooted in gameplay.
  AddExtendedProbe("return \"child result\";");
  std::weak_ptr<sr::GarbageCollector> child_heap;
  auto configure = runtime.configure_machine;
  runtime.configure_machine = [configure, &child_heap](SiglusMachine& machine) {
    child_heap = machine.vm->gc_;
    configure(machine);
  };
  InstallTokens({FarCall("far", true), token::Return{{Temp(0)}}, token::Eof{}});
  StartExtended("global result; result = entry();");
  ASSERT_TRUE(child_heap.expired());
  runtime.GameplayMachine().vm->CollectGarbage();
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result").Str(),
            "child result");
}

TEST_F(SiglusCallMemoryTest, ExtendedFailureIsCopiedIntoTheCaller) {
  // A child exception becomes a caller-owned exception after the child exits.
  AddExtendedProbe("throw \"child failure\";");
  InstallTokens({FarCall("far", true), token::Eof{}});
  StartExtended("global error; try { entry(); } catch (e) { error = e; }");
  runtime.GameplayMachine().vm->CollectGarbage();
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("error").Str(),
            "child failure");
}

TEST_F(SiglusCallMemoryTest, ExtendedScopeRemainsActiveDuringSuspension) {
  // A suspended child retains EX scope until its independent machine returns.
  auto gate = std::make_shared<sr::Promise>();
  AddExtendedProbe("await gate; return n;", gate);
  InstallTokens({FarCall("far", true), token::Eof{}});
  StartExtended("entry();");
  EXPECT_TRUE(runtime.IsExcall());
  gate->Resolve(sr::nil);
  DriveScripts();
  EXPECT_FALSE(runtime.IsExcall());
}

TEST_F(SiglusCallMemoryTest, SceneJumpKeepsCallerBanksAndReturnsToCaller) {
  // A named jump replaces the dispatcher, skipping its remaining instructions.
  AddFarcallProbe("observe(L[0], K[0]); return \"destination\";");
  InstallTokens({SceneJump(String("FAR")), token::Command{}, token::Eof{}});
  Run("L[0] = 42; K[0] = \"caller\"; result = entry(); observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 2);
  EXPECT_EQ(observations[0].integer, 42);
  EXPECT_EQ(observations[1].integer, 42);
  EXPECT_EQ(observations[1].depth, 1);
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("result").Str(),
            "destination");
}

TEST_F(SiglusCallMemoryTest, DynamicJumpReadsSceneAndLabelLocals) {
  // Slot allocation retains both dynamic operands until the terminal transfer.
  AddFarcallProbe("observe(L[0], K[0]); return n;");
  auto* destination = runtime.GameplayMachine().loader->Load(0);
  (*destination->globals)[GetZlabelId(7)] =
      destination->globals->at(GetZlabelId(0));
  (*destination->globals)[GetZlabelId(0)] =
      destination->globals->at("%%script");
  InstallTokens({token::Duplicate{Integer(0), Temp(0)},
                 token::Duplicate{Integer(7), Temp(1)},
                 SceneJump(Temp(0), Temp(1)), token::Eof{}});
  Run("L[0] = 42; entry();");
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].integer, 42);
}

TEST_F(SiglusCallMemoryTest, RepeatedSceneJumpsKeepConstantFrameDepth) {
  // A chain of real numeric jumps replaces every abandoned scene frame.
  AddFarcallProbe("observe(L[0], K[0]); return n;");
  int destination = 0;
  for (int i = 0; i < 64; ++i)
    destination = AddJumpScene("jump_" + std::to_string(i), destination);
  InstallTokens({SceneJump(Integer(destination)), token::Eof{}});
  Run("L[0] = 42; entry();");
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].frames, 3);
}

TEST_F(SiglusCallMemoryTest, JumpRetainsFarcallBankGuard) {
  // A farcall's destination jumps onward; its caller still releases the seeds.
  AddFarcallProbe("observe(L[0], K[0]); L[0] = 99; return \"destination\";");
  AddJumpScene("source", 0);
  InstallTokens({FarCall("source"), token::Return{{Temp(0)}}, token::Eof{}});
  Run("L[0] = 42; K[0] = \"caller\"; result = entry(); observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 2);
  EXPECT_EQ(observations[0].integer, 7);
  EXPECT_EQ(observations[0].string, "farcall");
  EXPECT_EQ(observations[0].depth, 2);
  EXPECT_EQ(observations[1].integer, 42);
  EXPECT_EQ(observations[1].string, "caller");
  EXPECT_EQ(observations[1].depth, 1);
}

TEST_F(SiglusCallMemoryTest, JumpFailureReleasesFarcallBanks) {
  // An error after a transfer still reaches the farcall caller's cleanup guard.
  AddFarcallProbe("L[0] = 99; throw \"destination failure\";");
  AddJumpScene("source", 0);
  InstallTokens({FarCall("source"), token::Eof{}});
  Run("L[0] = 42; try { entry(); } catch (e) { error = e; }"
      "observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].integer, 42);
  EXPECT_EQ(observations[0].depth, 1);
  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("error").Str(),
            "destination failure");
}

TEST_F(SiglusCallMemoryTest, JumpLookupFailureReleasesFarcallBanks) {
  // Failed resolution happens before transfer and still unwinds caller banks.
  AddJumpScene("source", 999);
  InstallTokens({FarCall("source"), token::Eof{}});
  Run("L[0] = 42; try { entry(); } catch (e) {} observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].integer, 42);
  EXPECT_EQ(observations[0].depth, 1);
}

TEST_F(SiglusCallMemoryTest, SuspendedJumpRetainsFarcallBanks) {
  // A jumped-to scene retains its seeds through await, then releases them.
  AddFarcallProbe("observe(L[0], K[0]); await gate; return n;");
  auto gate = InstallGate();
  AddJumpScene("source", 0);
  InstallTokens({FarCall("source"), token::Eof{}});
  Run("fn start() { L[0] = 42; entry(); observe(L[0], K[0]); }");
  runtime.GameplayMachine().vm->InvokeUntilBlocked(
      runtime.GameplayMachine().vm->globals_->at("start"), {});
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].integer, 7);
  EXPECT_EQ(observations[0].depth, 2);
  runtime.GameplayMachine().vm->CollectGarbage();
  gate->Resolve(sr::nil);
  runtime.GameplayMachine().vm->RunReadyUntilBlocked();
  ASSERT_EQ(observations.size(), 2);
  EXPECT_EQ(observations[1].integer, 42);
  EXPECT_EQ(observations[1].depth, 1);
}

TEST_F(SiglusCallMemoryTest, ExtendedCallCleansUpAfterException) {
  // A rejected EX result returns to the caller with its original bank stack.
  AddExtendedProbe("L[0] = 99; throw \"failure\";");
  InstallTokens({FarCall("far", true), token::Eof{}});
  StartExtended(
      "L[0] = 42; try { entry(); } catch (e) {} "
      "observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].integer, 42);
  EXPECT_EQ(observations[0].depth, 1);
}

TEST_F(SiglusCallMemoryTest, ExtendedCallRetainsBanksDuringSuspension) {
  // A suspended EX machine owns its banks while the caller remains parked.
  auto gate = std::make_shared<sr::Promise>();
  AddExtendedProbe("observe(L[0], K[0]); await gate; return n;", gate);
  InstallTokens({FarCall("far", true), token::Eof{}});
  StartExtended("L[0] = 42; entry(); observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].integer, 7);
  EXPECT_EQ(observations[0].depth, 1);
  gate->Resolve(sr::nil);
  DriveScripts();
  ASSERT_EQ(observations.size(), 2);
  EXPECT_EQ(observations[1].integer, 42);
  EXPECT_EQ(observations[1].depth, 1);
}

TEST_F(SiglusCallMemoryTest, CommandArgumentCountFailureDoesNotAcquireBanks) {
  // Missing command arguments fail before acquiring a bank frame.
  AddProbe("probe", "return n;");
  Run("try { command(); } catch (e) {} observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].depth, 1);
}

TEST_F(SiglusCallMemoryTest, InvalidFarcallArgumentsDoNotAcquireBanks) {
  // Invalid seeds fail during push, before installing the caller's pop guard.
  AddFarcallProbe("return n;");
  Run("try {" +
      BankedCall("__builtin_scene_entry(\"far\", 0)", {}, "ignored",
                 "[\"bad\"]") +
      "} catch (e) {} observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].depth, 1);
}

TEST_F(SiglusCallMemoryTest, StoredCommandSurvivesGarbageCollection) {
  // The scene module roots a borrowed adapter after its script alias is
  // removed.
  auto callback = AddProbe("stored", "observe(n, K[0]); return n;");
  runtime.GameplayMachine().vm->globals_->erase("command");
  runtime.GameplayMachine().vm->CollectGarbage();
  runtime.GameplayMachine().vm->InvokeUntilBlocked(
      callback, {sr::Value(17)}, MakeCallMemory(*runtime.memory));
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].integer, 17);
}

// ------------------------------------------------------------------------------

class FrameActionIntegrationTest : public ::testing::Test {
 protected:
  Archive archive = MakeEmptyArchive();
  SiglusRuntime runtime;
  Stage stage;
  LocalConfig config;
  std::shared_ptr<FrameActionManager> manager;
  CmdTarget target{1, 5};
  sr::Module* owner = nullptr;
  SiglusMachine* callback = nullptr;
  int calls = 0;

  void SetUp() override {
    ASSERT_TRUE(InitializeFlowRuntime(runtime, archive));
    FakeScene{.scene_name = "menu", .command_name = "open"}.AddTo(archive);
    FakeScene{.scene_name = "animation",
              .command_name = "animate",
              .args = {Type::FrameAction}}
        .AddTo(archive);
    manager = std::make_shared<FrameActionManager>(stage, config, 2);
    srbind::module_ bindings(runtime.GameplayMachine().vm->gc_.get(),
                             runtime.GameplayMachine().vm->globals_.get());
    runtime.frame_action_manager = manager;
    runtime.counter_list = std::make_shared<CounterList>(0);
    for (const auto* name : {"1_counter", "1_frame_action"}) {
      const auto* bind = SiglusBindingRegistry::Find(name);
      ASSERT_NE(bind, nullptr);
      (*bind)(runtime.GameplayMachine());
    }
    runtime.binding_names.insert(runtime.binding_names.end(),
                                 {"1_counter", "1_frame_action"});
    runtime.configure_machine = [this](SiglusMachine& machine) {
      callback = &machine;
      owner = machine.loader->Load(target.scene_id);
      srbind::module_ bindings(machine.vm->gc_.get(),
                               machine.vm->globals_.get());
      bindings.def("replacement", [this](sr::Value) { ++calls; });
      (*owner->globals)[GetUsercmdId(target.entry)] =
          machine.vm->globals_->at("replacement");
    };
    manager->Start({manager->global_action(), {}}, -1, false,
                   ScriptCall{.target = target});
    runtime.DispatchFrameAction(0, {manager->global_action(), {}},
                                ScriptCall{.target = target});
    ASSERT_NE(callback, nullptr);
    calls = 0;
  }

  void ReplaceEntry() {
    (*owner->globals)[GetUsercmdId(target.entry)] =
        callback->vm->globals_->at("replacement");
  }

  void SetCommandSignature(std::vector<Type> args) {
    target.scene_id = FakeScene{.scene_name = "typed_animation",
                                .command_name = "animate",
                                .args = std::move(args)}
                          .AddTo(archive);
    owner = callback->loader->Load(target.scene_id);
    manager->global_action()->call.target = target;
  }

  void Dispatch() {
    manager->AdvanceAndDispatch(std::chrono::milliseconds(1),
                                std::chrono::milliseconds(1));
  }
};

TEST_F(FrameActionIntegrationTest,
       SuspendedCallbackResumesWhileOrdinaryTimeIsStopped) {
  // Stop ordinary counters after a callback awaits a frame; its script
  // continuation still runs.
  (*SiglusBindingRegistry::Find("frame"))(*callback);
  srbind::module_ bindings(callback->vm->gc_.get(),
                           callback->vm->globals_.get());
  bindings.def("observe_continuation", [this] { ++calls; });
  Execute(*callback->vm, R"(
    fn replacement(action) { await frame(); observe_continuation(); }
  )");
  ReplaceEntry();
  Dispatch();
  manager->CancelGlobals();
  ASSERT_EQ(calls, 0);
  runtime.local_config.time_stop = true;
  runtime.local_config.counter_time_stop = true;
  runtime.local_config.frame_action_time_stop = true;
  runtime.local_config.stage_time_stop = true;
  runtime.Update(std::chrono::milliseconds(17), std::chrono::milliseconds(17));
  EXPECT_EQ(calls, 1);
}

TEST_F(FrameActionIntegrationTest,
       PendingCallbackKeepsHostAliveAfterGameplayCompletes) {
  // Finish gameplay before a callback awaits a frame; the host must keep
  // updating until that callback's continuation completes.
  auto& gameplay = runtime.GameplayMachine();
  Execute(*gameplay.vm, "fn gameplay_entry() {}");
  gameplay.vm->ScheduleExecute(gameplay.vm->globals_->at("gameplay_entry"));
  runtime.DriveScripts();
  ASSERT_FALSE(gameplay.vm->HasPendingWork());
  (*SiglusBindingRegistry::Find("frame"))(*callback);
  srbind::module_ bindings(callback->vm->gc_.get(),
                           callback->vm->globals_.get());
  bindings.def("observe_continuation", [this] { ++calls; });
  Execute(*callback->vm, R"(
    fn replacement(action) { await frame(); observe_continuation(); }
  )");
  ReplaceEntry();
  Dispatch();
  manager->CancelGlobals();

  EXPECT_TRUE(runtime.HasPendingScriptWork());
  runtime.Update(std::chrono::milliseconds(17), std::chrono::milliseconds(17));

  EXPECT_EQ(calls, 1);
  EXPECT_FALSE(runtime.HasPendingScriptWork());
}

TEST_F(FrameActionIntegrationTest, DispatchUsesItsOwnSceneModule) {
  // A gameplay-only replacement must not enter the callback's independent heap.
  auto& gameplay = runtime.GameplayMachine();
  srbind::module_ bindings(gameplay.vm->gc_.get(), gameplay.vm->globals_.get());
  bindings.def("other_replacement", [this](sr::Value) { calls += 1000; });
  (*gameplay.loader->Load(target.scene_id)
        ->globals)[GetUsercmdId(target.entry)] =
      gameplay.vm->globals_->at("other_replacement");
  Dispatch();
  EXPECT_EQ(calls, 1);
}

TEST_F(FrameActionIntegrationTest, DispatchUsesCurrentEntry) {
  // Replace an entry after starting; each dispatch must fetch its new callable.
  ReplaceEntry();
  Dispatch();
  EXPECT_EQ(calls, 1);
  srbind::module_ bindings(callback->vm->gc_.get(),
                           callback->vm->globals_.get());
  bindings.def("replacement", [this](sr::Value) { calls += 10; });
  ReplaceEntry();
  Dispatch();
  EXPECT_EQ(calls, 11);
}

TEST_F(FrameActionIntegrationTest, DispatchUsesFrameActionTraceCategory) {
  // A manager-dispatched callback reaches the frame-action trace stream.
  LogCapture output;
  SetupLogging(Severity::Error, {.frame_action = true});
  (*SiglusBindingRegistry::Find("trace"))(*callback);
  Execute(*callback->vm, R"(
    fn replacement(action) { __builtin_trace(1, 2, "dispatch"); }
  )");
  ReplaceEntry();
  Dispatch();
  EXPECT_EQ(output.Str(), "[1:2] dispatch\n");
}

TEST_F(FrameActionIntegrationTest, FinalCallbackUsesFrameActionTraceCategory) {
  // Explicit finalization uses the same callback category as regular dispatch.
  LogCapture output;
  SetupLogging(Severity::Error, {.frame_action = true});
  (*SiglusBindingRegistry::Find("trace"))(*callback);
  Execute(*callback->vm, R"(
    fn replacement(action) { __builtin_trace(1, 2, "final"); }
  )");
  ReplaceEntry();
  manager->End({manager->global_action(), {}});
  EXPECT_EQ(output.Str(), "[1:2] final\n");
}

TEST_F(FrameActionIntegrationTest, FinalizationUsesCurrentEntry) {
  // Replace a running action's entry; explicit end must invoke the replacement.
  ReplaceEntry();
  manager->End({manager->global_action(), {}});
  EXPECT_EQ(calls, 1);
}

TEST_F(FrameActionIntegrationTest, ExpirationUsesCurrentEntry) {
  // Expire an action with a replaced entry; regular and final calls both use
  // it.
  ReplaceEntry();
  manager->global_action()->end_time = 1;
  Dispatch();
  EXPECT_EQ(calls, 2);
  EXPECT_FALSE(manager->global_action()->IsActive());
}

TEST_F(FrameActionIntegrationTest, TargetRetainsSelectedCommand) {
  // Add an included override after start; dispatch must retain the selected
  // local entry even though name resolution now selects the override.
  const int other = FakeScene{.scene_name = "other_animation",
                              .command_name = "animate",
                              .args = {Type::FrameAction},
                              .included = true}
                        .AddTo(archive);
  ReplaceEntry();
  EXPECT_EQ(callback->loader->ResolveUserCommand(*owner, "animate").target,
            (CmdTarget{other, 5}));
  Dispatch();
  EXPECT_EQ(calls, 1);
}

TEST_F(FrameActionIntegrationTest, CallbackSurvivesGarbageCollection) {
  // Keep the replacement only in scene globals; collection must retain it.
  ReplaceEntry();
  callback->vm->globals_->erase("replacement");
  callback->vm->CollectGarbage();
  Dispatch();
  EXPECT_EQ(calls, 1);
}

TEST_F(FrameActionIntegrationTest, MissingDispatchEntryRaisesRuntimeError) {
  // Remove an active entry; dispatch reports failure and leaves the action
  // active.
  owner->globals->erase(GetUsercmdId(target.entry));
  EXPECT_THROW(Dispatch(), sr::RuntimeError);
  EXPECT_TRUE(manager->global_action()->IsActive());
}

TEST_F(FrameActionIntegrationTest, MissingFinalEntryResetsAction) {
  // Remove an active entry; final resolution failure must still reset its
  // state.
  auto action = manager->global_action();
  action->call.integers = {7};
  action->GetCounter()->Set(23);
  owner->globals->erase(GetUsercmdId(target.entry));
  EXPECT_THROW(manager->End({action, {}}), sr::RuntimeError);
  EXPECT_FALSE(action->IsActive());
  EXPECT_FALSE(action->end_action);
  EXPECT_TRUE(action->call.integers.empty());
  EXPECT_EQ(action->end_time, 0);
  EXPECT_EQ(action->GetCounter()->Get(), 0);
}

TEST_F(FrameActionIntegrationTest, DispatchConvertsGlobalArgumentsInOrder) {
  // Dispatch mixed arguments; the native action handle precedes typed values.
  SetCommandSignature({Type::FrameAction, Type::Int, Type::String});
  srbind::module_ bindings(callback->vm->gc_.get(),
                           callback->vm->globals_.get());
  bindings.def(
      "replacement", [this](sr::Value handle, int number, std::string text) {
        ++calls;
        const auto* instance = handle.Get_if<sr::NativeInstance>();
        ASSERT_NE(instance, nullptr);
        EXPECT_EQ(sr::Value(instance->klass),
                  callback->vm->builtins_->at("%%siglus_frame_action_class"));
        EXPECT_EQ(number, 7);
        EXPECT_EQ(text, "argument");
        (*callback->vm->globals_)["observed_action"] = handle;
      });
  ReplaceEntry();
  manager->global_action()->call.integers = {7};
  manager->global_action()->call.strings = {"argument"};

  Dispatch();

  ASSERT_EQ(calls, 1);
  Execute(*callback->vm, "observed_action.counter.set(47);");
  EXPECT_EQ(manager->global_action()->counter->Get(), 47);
}

TEST_F(FrameActionIntegrationTest,
       DispatchUsesObjectFactoryInstalledAfterBinding) {
  // Install the owner factory after binding; place its result before arguments.
  SetCommandSignature(
      {Type::FrameAction, Type::Object, Type::Int, Type::String});
  manager->CancelGlobals();
  stage.objects.Resize(kLayerFg, 1);
  auto object = stage.objects.Get(kLayerFg, 0);
  int converted = 0;
  callback->make_object = [&](ActionOwner owner) {
    ++converted;
    EXPECT_EQ(owner, object);
    return sr::Value(89);
  };
  srbind::module_ bindings(callback->vm->gc_.get(),
                           callback->vm->globals_.get());
  bindings.def("replacement", [this](sr::Value handle, int owner, int number,
                                     std::string text) {
    ++calls;
    EXPECT_NE(handle.Get_if<sr::NativeInstance>(), nullptr);
    EXPECT_EQ(owner, 89);
    EXPECT_EQ(number, 7);
    EXPECT_EQ(text, "argument");
  });
  ReplaceEntry();
  manager->Start({object->GetFrameAction(), object}, -1, false,
                 ScriptCall{.target = target,
                            .integers = {7},
                            .strings = {std::string("argument")}});

  Dispatch();

  EXPECT_EQ(calls, 1);
  EXPECT_EQ(converted, 1);
}

TEST_F(FrameActionIntegrationTest, DispatchReconstructsInterleavedArguments) {
  // Store typed vectors; dispatch restores string/int/string declaration order.
  SetCommandSignature(
      {Type::FrameAction, Type::String, Type::Int, Type::String});
  srbind::module_ bindings(callback->vm->gc_.get(),
                           callback->vm->globals_.get());
  bindings.def("replacement", [this](sr::Value, std::string first, int number,
                                     std::string last) {
    ++calls;
    EXPECT_EQ(first, "first");
    EXPECT_EQ(number, 7);
    EXPECT_EQ(last, "last");
  });
  ReplaceEntry();
  manager->global_action()->call.integers = {7};
  manager->global_action()->call.strings = {"first", "last"};

  Dispatch();

  EXPECT_EQ(calls, 1);
}

TEST_F(FrameActionIntegrationTest, StartSplitsArgumentsIntoTypedVectors) {
  // Start from a loaded scene; the binding captures values in each typed
  // vector.
  manager->CancelGlobals();
  SetCommandSignature(
      {Type::FrameAction, Type::String, Type::Int, Type::String});
  auto& gameplay = runtime.GameplayMachine();
  Execute(*gameplay.vm, R"(
    fn start(action) { action.start(-1, "animate", "first", 7, "last"); }
  )");
  auto* start = gameplay.vm->globals_->at("start").Get<sr::Function*>();
  start->globals = gameplay.loader->Load(target.scene_id)->globals;

  gameplay.vm->InvokeUntilBlocked(start,
                                  {gameplay.vm->globals_->at("frame_action")});

  const auto& action = *manager->global_action();
  EXPECT_TRUE(action.IsActive());
  EXPECT_EQ(std::get<CmdTarget>(action.call.target), target);
  EXPECT_EQ(action.call.integers, (std::vector<int>{7}));
  EXPECT_EQ(action.call.strings, (std::vector<std::string>{"first", "last"}));
}

TEST_F(FrameActionIntegrationTest, StartRejectsIncorrectArgumentTypes) {
  // Pass an integer to a string parameter; no frame action is installed.
  manager->CancelGlobals();
  SetCommandSignature({Type::FrameAction, Type::String});
  auto& gameplay = runtime.GameplayMachine();
  Execute(*gameplay.vm, R"(
    fn start(action) { action.start(-1, "animate", 7); }
  )");
  auto* start = gameplay.vm->globals_->at("start").Get<sr::Function*>();
  start->globals = gameplay.loader->Load(target.scene_id)->globals;

  EXPECT_THROW(gameplay.vm->InvokeUntilBlocked(
                   start, {gameplay.vm->globals_->at("frame_action")}),
               sr::RuntimeError);
  EXPECT_FALSE(manager->global_action()->IsActive());
}

TEST_F(FrameActionIntegrationTest, DispatchRejectsMissingTypedArguments) {
  // A string parameter has no captured value; the callback must not execute.
  SetCommandSignature({Type::FrameAction, Type::String});
  ReplaceEntry();

  EXPECT_THROW(Dispatch(), sr::RuntimeError);

  EXPECT_EQ(calls, 0);
  EXPECT_TRUE(manager->global_action()->IsActive());
}

TEST_F(FrameActionIntegrationTest, DispatchRejectsUnusedTypedArguments) {
  // Capture an extra string for a parameterless command; do not invoke it.
  manager->global_action()->call.strings = {"extra"};

  EXPECT_THROW(Dispatch(), sr::RuntimeError);

  EXPECT_EQ(calls, 0);
}

TEST_F(FrameActionIntegrationTest, DispatchRejectsIncompatibleSignature) {
  // A command lacking the action-handle prefix must not be invoked.
  SetCommandSignature({Type::Int});
  ReplaceEntry();

  EXPECT_THROW(Dispatch(), sr::RuntimeError);

  EXPECT_EQ(calls, 0);
}

TEST_F(FrameActionIntegrationTest, DispatchRejectsUnsupportedParameterType) {
  // Extra object parameters cannot be restored from typed integer/string data.
  SetCommandSignature({Type::FrameAction, Type::Object});
  ReplaceEntry();

  EXPECT_THROW(Dispatch(), sr::RuntimeError);

  EXPECT_EQ(calls, 0);
}

TEST_F(FrameActionIntegrationTest, DispatchRejectsSceneEntryTarget) {
  // Frame actions require command metadata; reject a scene-entry target.
  manager->global_action()->call.target = SceneTarget{target.scene_id, 0};

  EXPECT_THROW(Dispatch(), sr::RuntimeError);

  EXPECT_EQ(calls, 0);
}

TEST_F(FrameActionIntegrationTest,
       FinalCallbackExposesFinalStateThroughNativeHandle) {
  // End through the manager; the script handle exposes final flags and time.
  int end_action = -1;
  int time = -1;
  srbind::module_ bindings(callback->vm->gc_.get(),
                           callback->vm->globals_.get());
  bindings.def("observe_frame", [&](int end, int counter) {
    ++calls;
    end_action = end;
    time = counter;
  });
  Execute(*callback->vm, R"(
    fn replacement(action) {
      observe_frame(action.is_end_action(), action.counter.get());
    }
  )");
  ReplaceEntry();
  manager->global_action()->end_time = 17;

  manager->End({manager->global_action(), {}});

  EXPECT_EQ(calls, 1);
  EXPECT_EQ(end_action, 1);
  EXPECT_EQ(time, 17);
  EXPECT_FALSE(manager->global_action()->end_action);
}

TEST_F(FrameActionIntegrationTest, NativeFactoriesSurviveGarbageCollection) {
  // Remove global wrappers before collection; factories must retain class
  // roots.
  callback->vm->globals_->erase("frame_action");
  callback->vm->globals_->erase("frame_action_ch");
  callback->vm->CollectGarbage();
  (*callback->vm->globals_)["retained_action"] = callback->make_frame_action(
      ActionReference{manager->global_action(), {}});
  (*callback->vm->globals_)["retained_list"] =
      callback->make_frame_action_list({});

  Execute(*callback->vm, R"(
    retained_action.counter.set(23);
    retained_list[1].counter.set(47);
  )");

  EXPECT_EQ(manager->global_action()->counter->Get(), 23);
  ASSERT_NE(manager->global_channels()[1], nullptr);
  EXPECT_EQ(manager->global_channels()[1]->counter->Get(), 47);
}

TEST_F(FrameActionIntegrationTest, GlobalChannelListRemainsFixedSize) {
  // Resize the bound global list; reject the request and retain its channel
  // count.
  Execute(*callback->vm, R"(
    caught = 0;
    try { frame_action_ch.resize(3); } catch (error) { caught = 1; }
  )");
  EXPECT_EQ(callback->vm->globals_->at("caught"), sr::Value(1));
  EXPECT_EQ(manager->global_channels().size(), 2);
}

class SiglusRestartTest : public ::testing::Test {
 protected:
  SiglusRuntime runtime;
  std::shared_ptr<sr::Promise> gate = std::make_shared<sr::Promise>();
  std::vector<int> observations;
  std::weak_ptr<sr::GarbageCollector> callback_heap;
  CmdTarget menu{1, 5};
  CmdTarget callback{2, 5};
  int restart_delay_millis = 0;

  void SetUp() override {
    runtime.archive = std::make_shared<Archive>(MakeEmptyArchive());
    runtime.archive->prop_ = {
        {.form = Type::Int, .size = 0, .name = "retained"}};
    FakeScene{.scene_name = "title", .command_name = "title"}.AddTo(
        *runtime.archive);
    FakeScene{.scene_name = "menu", .command_name = "menu"}.AddTo(
        *runtime.archive);
    FakeScene{.scene_name = "callback",
              .command_name = "callback",
              .args = {Type::FrameAction}}
        .AddTo(*runtime.archive);
    runtime.stage = std::make_unique<Stage>();
    runtime.counter_list = std::make_shared<CounterList>(0);
    runtime.frame_action_manager = std::make_shared<FrameActionManager>(
        *runtime.stage, runtime.local_config, 0);
    runtime.binding_names = {"0_memory", "flow", "1_counter", "1_frame_action"};
    runtime.configure_machine = [this](SiglusMachine& machine) {
      if (machine.callback_owner >= 0)
        callback_heap = machine.vm->gc_;
      srbind::module_ bindings(machine.vm->gc_.get(),
                               machine.vm->globals_.get());
      bindings.def("observe",
                   [this](int value) { observations.push_back(value); });
      bindings.def("request_restart", [this] {
        runtime.QueueReturnToMenu("title", 0, false, restart_delay_millis);
      });
      auto* future = machine.vm->gc_->Allocate<sr::Future>();
      future->promise = gate;
      (*machine.vm->globals_)["gate"] = sr::Value(future);
      Execute(*machine.vm, R"(
        fn restart_entry() { request_restart(); observe(1); }
        fn menu_entry() { await gate; observe(2); }
        fn callback_entry(action) { await gate; observe(3); }
        fn callback_restart_entry(action) { request_restart(); observe(3); }
        fn title_entry(descriptor) { observe(4); }
      )");
      auto* title = machine.loader->Load(0);
      const auto* original =
          title->globals->at(GetZlabelId(0)).Get<sr::BoundMethod*>();
      (*title->globals)[GetZlabelId(0)] =
          machine.vm->gc_->Allocate<sr::BoundMethod>(
              machine.vm->globals_->at("title_entry"),
              original->additional_args);
      (*machine.loader->Load(menu.scene_id)
            ->globals)[GetUsercmdId(menu.entry)] =
          machine.vm->globals_->at("menu_entry");
      (*machine.loader->Load(callback.scene_id)
            ->globals)[GetUsercmdId(callback.entry)] =
          machine.vm->globals_->at("callback_entry");
    };
    runtime.GameplayMachine().Initialize();
  }

  void ScheduleRestart(SiglusMachine& machine) {
    machine.vm->ScheduleExecute(machine.vm->globals_->at("restart_entry"));
  }
};

TEST_F(SiglusRestartTest, RestartReplacesTheGameplayVmAtTheHostSafePoint) {
  // Queue a restart from running script code; keep its heap alive until the
  // next host drive, then execute the title entry in a fresh gameplay VM.
  const std::weak_ptr old_heap = runtime.GameplayMachine().vm->gc_;
  ScheduleRestart(runtime.GameplayMachine());

  runtime.DriveScripts();

  EXPECT_FALSE(old_heap.expired());
  EXPECT_TRUE(observations.empty());
  runtime.DriveScripts();
  EXPECT_TRUE(old_heap.expired());
  EXPECT_EQ(observations, std::vector<int>{4});
}

TEST_F(SiglusRestartTest, RestartPreservesSharedProperties) {
  // Store a property before restarting; its value belongs to the host and is
  // readable through the replacement VM's freshly bound property facade.
  Execute(*runtime.GameplayMachine().vm, "__globalprop[0] = 71;");
  ScheduleRestart(runtime.GameplayMachine());
  runtime.DriveScripts();
  runtime.DriveScripts();

  Execute(*runtime.GameplayMachine().vm, "retained = __globalprop[0];");

  EXPECT_EQ(runtime.GameplayMachine().vm->globals_->at("retained"),
            sr::Value(71));
}

TEST_F(SiglusRestartTest, DelayedRestartPausesTheRequestingCallback) {
  // A callback queues return-to-menu with a fade deadline; frame updates must
  // leave its post-command continuation paused until the host replaces it.
  restart_delay_millis = 60000;
  const auto configure = runtime.configure_machine;
  runtime.configure_machine = [this, configure](SiglusMachine& machine) {
    configure(machine);
    (*machine.loader->Load(callback.scene_id)
          ->globals)[GetUsercmdId(callback.entry)] =
        machine.vm->globals_->at("callback_restart_entry");
  };
  runtime.DispatchFrameAction(
      0, {runtime.frame_action_manager->global_action(), {}},
      ScriptCall{.target = callback});
  ASSERT_TRUE(observations.empty());

  runtime.Update(std::chrono::milliseconds(17), std::chrono::milliseconds(17));

  EXPECT_TRUE(observations.empty());
}

TEST_F(SiglusRestartTest, NestedCallbackFreeRetainsTheOuterExecutingMachine) {
  // An EX callback enters a normal callback that frees EX resources; the EX
  // VM must survive its native call and be destroyed at the next host boundary.
  runtime.InitializeExcall();
  runtime.excall->Alloc({.flag_count = 1000,
                         .counter_count = 16,
                         .frame_action_count = 4,
                         .object_count = 256,
                         .effect_count = 0,
                         .group_count = 16,
                         .quake_count = 16,
                         .window_count = 1});
  std::weak_ptr<sr::GarbageCollector> outer_heap;
  const auto configure = runtime.configure_machine;
  runtime.configure_machine = [this, configure,
                               &outer_heap](SiglusMachine& machine) {
    configure(machine);
    srbind::module_ bindings(machine.vm->gc_.get(), machine.vm->globals_.get());
    if (machine.callback_owner == 1) {
      outer_heap = machine.vm->gc_;
      bindings.def("nested_callback", [this, &outer_heap](sr::Value) {
        observations.push_back(1);
        runtime.DispatchFrameAction(
            0, {runtime.frame_action_manager->global_action(), {}},
            ScriptCall{.target = callback});
        EXPECT_FALSE(outer_heap.expired());
        observations.push_back(3);
      });
    } else {
      bindings.def("nested_callback", [this](sr::Value) {
        observations.push_back(2);
        runtime.FreeExcall(false);
      });
    }
    (*machine.loader->Load(callback.scene_id)
          ->globals)[GetUsercmdId(callback.entry)] =
        machine.vm->globals_->at("nested_callback");
  };

  runtime.DispatchFrameAction(1, {runtime.excall->actions->global_action(), {}},
                              ScriptCall{.target = callback});

  EXPECT_EQ(observations, (std::vector<int>{1, 2, 3}));
  runtime.Update(std::chrono::milliseconds(0), std::chrono::milliseconds(0));
  EXPECT_TRUE(outer_heap.expired());
}

TEST_F(SiglusRestartTest, RestartDiscardsSuspendedExcallAndCallbackMachines) {
  // Park an EX entry and callback on a C++ gate, then restart from the EX VM.
  // Their heaps must disappear and resolving the gate must not resume them.
  const std::weak_ptr old_gameplay = runtime.GameplayMachine().vm->gc_;
  runtime.DispatchFrameAction(
      0, {runtime.frame_action_manager->global_action(), {}},
      ScriptCall{.target = callback});
  ASSERT_FALSE(callback_heap.expired());
  ASSERT_TRUE(runtime.OpenExcall(menu));
  runtime.DriveScripts();
  const std::weak_ptr old_excall = runtime.ActiveMachine().vm->gc_;
  ScheduleRestart(runtime.ActiveMachine());
  runtime.DriveScripts();
  EXPECT_FALSE(old_excall.expired());

  runtime.DriveScripts();
  gate->Resolve(sr::nil);
  runtime.DriveScripts();

  EXPECT_TRUE(old_gameplay.expired());
  EXPECT_TRUE(old_excall.expired());
  EXPECT_TRUE(callback_heap.expired());
  EXPECT_FALSE(runtime.IsExcall());
  EXPECT_EQ(observations, std::vector<int>{4});
}

}  // namespace libsiglus::binding
