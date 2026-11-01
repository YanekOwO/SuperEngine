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
#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/flow.hpp"
#include "libsiglus/bindings/loader.hpp"
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
#include "vm/gc.hpp"
#include "vm/promise.hpp"
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

static ::testing::AssertionResult InitializeFlowRuntime(SiglusMachine& runtime,
                                                        Archive& archive) {
  runtime.vm =
      std::make_unique<sr::VM>(std::make_shared<sr::GarbageCollector>());
  runtime.loader = std::make_unique<Loader>(archive, *runtime.vm, false);
  for (const auto* name : {"0_memory", "flow"}) {
    const auto* bind = SiglusBindingRegistry::Find(name);
    if (bind == nullptr)
      return ::testing::AssertionFailure()
             << "Missing Siglus binding: " << name;
    (*bind)(runtime);
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
  SiglusMachine runtime;
  struct Observation {
    int integer;
    std::string string;
    std::size_t depth;
  };
  std::vector<Observation> observations;

  void SetUp() override {
    ASSERT_TRUE(InitializeFlowRuntime(runtime, archive));
    srbind::module_ bindings(runtime.vm->gc_.get(), runtime.vm->globals_.get());
    bindings.def("observe", [this](sr::Fiber& fiber, int i, std::string s) {
      auto* memory = std::any_cast<MemoryHandle>(&fiber.usrdata);
      ASSERT_NE(memory, nullptr);
      observations.push_back({i, std::move(s), memory->StackDepth()});
    });
  }

  void Run(std::string source) {
    m6::CompilerPipeline compiler(runtime.vm->gc_, false);
    compiler.compile(
        m6::SourceBuffer::Create(std::move(source), "<SiglusCallMemoryTest>"));
    ASSERT_TRUE(compiler.Ok()) << compiler.FormatErrors();
    runtime.vm->Evaluate(compiler.Get());
  }

  void InstallTokens(std::vector<token::Token_t> body, bool trace = false) {
    std::vector<Parser::ParsedToken> tokens;
    for (auto& token : body)
      tokens.push_back({static_cast<int>(tokens.size() + 1), std::move(token)});
    Recompiler compiler(runtime.vm->gc_);
    compiler.trace_enabled_ = trace;
    compiler.Compile(tokens);
    ASSERT_TRUE(compiler.Ok());
    auto* module = compiler.module_;
    (*runtime.vm->builtins_)["test_scene"] = sr::Value(module);
    sr::VM bootstrap(runtime.vm->gc_);
    bootstrap.gc_threshold_ = 0;
    bootstrap.globals_ = module->globals;
    bootstrap.Evaluate(compiler.GetCode());
    auto* dispatcher = module->globals->at("%%dispatch").Get_if<sr::Function>();
    (*runtime.vm->globals_)["entry"] =
        MakeSceneEntry(*runtime.vm->gc_, dispatcher, compiler.GetMainLine(), 0,
                       SceneCallKind::Scene);
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

  static token::Command FarCall(std::string scene) {
    elm::Farcall call;
    call.scn_name = String(std::move(scene));
    call.zlabel = Integer(0);
    call.intargs = {Integer(7)};
    call.strargs = {String("farcall")};
    return token::Command{
        .chain = elm::AccessChain{.root = elm::Root(Type::String, call)},
        .dst = Variable{.type = Type::String, .id = 0}};
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
    srbind::module_ math(*runtime.vm, "math");
    (*math.dict())["tostr"] = runtime.vm->globals_->at("probe");
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
    srbind::module_ math(*runtime.vm, "math");
    (*math.dict())["tostr"] = runtime.vm->globals_->at("probe");
    auto command = ResolveSceneCommand(*runtime.loader, name, name);
    (*runtime.vm->globals_)["command"] = sr::Value(command);
    EXPECT_EQ(Dispatcher(command)->globals,
              runtime.loader->Load(scene_id)->globals);
    return command;
  }
};

TEST_F(SiglusCallMemoryTest, ConsecutiveCommandsStartFreshAndRestoreCaller) {
  AddProbe("face", R"(
    observe(L[0], K[0]);
    L[0] = 6; K[0] = "callee";
    return "face";
  )");
  Run(R"(
    L[0] = 42; K[0] = "caller";
    first = __builtin_usrcmd(0, 5, "face")(0);
    second = __builtin_usrcmd(0, 5, "face")(0);
    observe(L[0], K[0]);
  )");

  ASSERT_EQ(observations.size(), 3);
  for (int i : {0, 1}) {
    EXPECT_EQ(observations[i].integer, 0);
    EXPECT_EQ(observations[i].string, "");
    EXPECT_EQ(observations[i].depth, 2);
  }
  EXPECT_EQ(observations[2].integer, 42);
  EXPECT_EQ(observations[2].string, "caller");
  EXPECT_EQ(observations[2].depth, 1);
  EXPECT_EQ(runtime.vm->globals_->at("first").Str(), "face");
  EXPECT_EQ(runtime.vm->globals_->at("second").Str(), "face");
}

TEST_F(SiglusCallMemoryTest, RecursiveCommandsRestoreEveryCaller) {
  AddProbe("recursive", R"(
    observe(L[0], K[0]);
    L[0] = n; K[0] = "level";
    if (n > 0) { command(n - 1); }
    observe(L[0], K[0]);
    return "done";
  )");
  Run("L[0] = 99; K[0] = \"root\"; result = command(2); observe(L[0], K[0]);");

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
  EXPECT_EQ(runtime.vm->globals_->at("result").Str(), "done");
}

TEST_F(SiglusCallMemoryTest, ExceptionRestoresCallerBeforeCatch) {
  AddProbe("failure", R"(
    L[0] = 6; K[0] = "callee";
    throw "failure";
  )");
  Run(R"(
    L[0] = 42; K[0] = "caller";
    try { command(0); } catch (e) { error = e; }
    observe(L[0], K[0]);
  )");

  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].integer, 42);
  EXPECT_EQ(observations[0].string, "caller");
  EXPECT_EQ(observations[0].depth, 1);
  EXPECT_EQ(runtime.vm->globals_->at("error").Str(), "failure");
}

TEST_F(SiglusCallMemoryTest, DirectCallbackKeepsBanksUntilResumed) {
  auto command = AddProbe("callback", R"(
    observe(L[0], K[0]);
    L[0] = n; K[0] = "suspended";
    await gate;
    observe(L[0], K[0]);
    return "resumed";
  )");
  auto* gate = runtime.vm->gc_->Allocate<sr::Fiber>();
  (*runtime.vm->globals_)["gate"] = sr::Value(gate);
  runtime.vm->InvokeUntilBlocked(command, {sr::Value(17)});

  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].integer, 0);
  EXPECT_EQ(observations[0].string, "");
  EXPECT_EQ(observations[0].depth, 2);
  ASSERT_EQ(runtime.vm->fibres_.size(), 1);
  auto* callback = runtime.vm->fibres_.front();
  EXPECT_EQ(callback->state, sr::FiberState::Suspended);
  std::size_t completed_depth = 0;
  callback->completion_promise->AddWaker([&](const auto& result) {
    EXPECT_TRUE(result.has_value());
    completed_depth =
        std::any_cast<MemoryHandle>(&callback->usrdata)->StackDepth();
  });

  gate->completion_promise->Resolve(sr::nil);
  runtime.vm->Run();

  ASSERT_EQ(observations.size(), 2);
  EXPECT_EQ(observations[1].integer, 17);
  EXPECT_EQ(observations[1].string, "suspended");
  EXPECT_EQ(observations[1].depth, 2);
  EXPECT_EQ(completed_depth, 1);
}

TEST_F(SiglusCallMemoryTest, FarcallBanksSurviveNestedUserCommand) {
  AddProbe("nested", "L[0] = 6; K[0] = \"callee\"; return \"done\";");
  // These are the same frame bindings used by recompiled farcalls.
  Run(R"(
    L[0] = 42; K[0] = "root";
    __builtin_push_frame([7], ["farcall"]);
    command(0);
    observe(L[0], K[0]);
    __builtin_pop_frame();
    observe(L[0], K[0]);
  )");

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
    (*runtime.vm->globals_)[name] =
        sr::Value(ResolveSceneCommand(*runtime.loader, name, name));
  }
  FakeScene{.scene_name = "empty", .command_name = "empty"}.AddTo(archive);
  (*runtime.vm->globals_)["empty"] =
      sr::Value(ResolveSceneCommand(*runtime.loader, "empty", "empty"));
  Run(R"(
    integer_result = integer(73);
    string_result = string("unchanged");
    empty_result = empty();
  )");

  EXPECT_EQ(runtime.vm->globals_->at("integer_result"), sr::Value(73));
  EXPECT_EQ(runtime.vm->globals_->at("string_result").Str(), "unchanged");
  EXPECT_EQ(runtime.vm->globals_->at("empty_result"), sr::nil);
}

TEST_F(SiglusCallMemoryTest, ZeroInstructionTokensRemainJumpTargets) {
  // Consecutive labels have token entries even when neither emits bytecode.
  InstallTokens({token::Goto{1}, token::Return{{Integer(0)}}, token::Label{0},
                 token::Label{1}, token::Return{{Integer(42)}}, token::Eof{}});
  auto* code = Dispatcher(runtime.vm->globals_->at("entry"))->chunk;
  EXPECT_EQ(code->line_offset.size(), 6);
  EXPECT_EQ(code->line_offset.at(3), code->line_offset.at(4));
  Run("result = entry();");
  EXPECT_EQ(runtime.vm->globals_->at("result"), sr::Value(42));
}

TEST_F(SiglusCallMemoryTest, TraceAnnotationsRemainAtTokenBoundaries) {
  // Trace instrumentation runs at the target token before the scene resumes.
  std::vector<std::string> annotations;
  srbind::module_ bindings(runtime.vm->gc_.get(), runtime.vm->globals_.get());
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
  EXPECT_EQ(runtime.vm->globals_->at("result"), sr::Value(42));
}

TEST_F(SiglusCallMemoryTest, SceneFallthroughReturnsNil) {
  // EOF supplies normal termination through the dispatcher's shared epilogue.
  InstallTokens({token::Label{0}, token::Eof{}});
  Run("result = entry();");
  EXPECT_EQ(runtime.vm->globals_->at("result"), sr::nil);
}

TEST_F(SiglusCallMemoryTest, RecompiledGosubReturnsItsArgument) {
  // Enter a zero-instruction label and return the supplied argument.
  InstallTokens(
      {token::Gosub{.entry_id = 0, .args = {Integer(73)}, .dst = Temp(0)},
       token::Return{{Temp(0)}}, token::Label{0},
       token::Subroutine{.source_entry = 42, .args = {Type::Int}},
       Argument(0, 1), token::Return{{Temp(1)}}, token::Eof{}});
  Run("result = entry();");
  EXPECT_EQ(runtime.vm->globals_->at("result"), sr::Value(73));
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
  EXPECT_EQ(runtime.vm->globals_->at("result"), sr::Value(73));
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
  EXPECT_EQ(runtime.vm->globals_->at("result"), sr::Value(42));
}

TEST_F(SiglusCallMemoryTest, MultipleReturnValuesRemainOrdered) {
  // A dispatcher return preserves the original multi-value list representation.
  InstallTokens({token::Return{{Integer(42), String("value")}}, token::Eof{}});
  Run("result = entry();");
  auto* values = runtime.vm->globals_->at("result").Get_if<sr::List>();
  ASSERT_NE(values, nullptr);
  ASSERT_EQ(values->items.size(), 2);
  EXPECT_EQ(values->items[0], sr::Value(42));
  EXPECT_EQ(values->items[1].Str(), "value");
}

TEST_F(SiglusCallMemoryTest, SuspendedGosubPreservesItsArgument) {
  // Suspend in a subroutine and retain its argument when its caller resumes.
  auto* gate = runtime.vm->gc_->Allocate<sr::Fiber>();
  (*runtime.vm->globals_)["gate"] = sr::Value(gate);
  srbind::module_ bindings(runtime.vm->gc_.get(), runtime.vm->globals_.get());
  bindings.def("__builtin_textout",
               [gate](int, int, std::string) { return sr::Value(gate); });
  InstallTokens(
      {token::Gosub{.entry_id = 0, .args = {Integer(73)}, .dst = Temp(0)},
       token::Return{{Temp(0)}}, token::Label{0},
       token::Subroutine{.source_entry = 42, .args = {Type::Int}},
       token::Textout{.kidoku = 0, .str = String("pause")}, Argument(0, 1),
       token::Return{{Temp(1)}}, token::Eof{}});
  Run("fn start() { global result; result = entry(); }");
  runtime.vm->InvokeUntilBlocked(runtime.vm->globals_->at("start"), {});
  ASSERT_EQ(runtime.vm->fibres_.size(), 1);
  EXPECT_EQ(runtime.vm->fibres_.front()->state, sr::FiberState::Suspended);
  gate->completion_promise->Resolve(sr::nil);
  runtime.vm->Run();
  EXPECT_EQ(runtime.vm->globals_->at("result"), sr::Value(73));
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
  auto* gate = runtime.vm->gc_->Allocate<sr::Fiber>();
  (*runtime.vm->globals_)["gate"] = sr::Value(gate);
  Run("fn start() { L[0] = 42; K[0] = \"root\"; entry(); observe(L[0], K[0]); "
      "}");
  auto start = runtime.vm->globals_->at("start");
  runtime.vm->InvokeUntilBlocked(start, {});
  ASSERT_EQ(observations.size(), 1);
  runtime.vm->CollectGarbage();
  gate->completion_promise->Resolve(sr::nil);
  runtime.vm->Run();
  ASSERT_EQ(observations.size(), 3);
  EXPECT_EQ(observations[1].integer, 7);
  EXPECT_EQ(observations[1].string, "farcall");
  EXPECT_EQ(observations[2].integer, 42);
  EXPECT_EQ(observations[2].depth, 1);
}

TEST_F(SiglusCallMemoryTest, CommandArgumentCountFailureDoesNotAcquireBanks) {
  // Missing command arguments fail before acquiring a bank frame.
  AddProbe("probe", "return n;");
  Run("try { command(); } catch (e) {} observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].depth, 1);
}

TEST_F(SiglusCallMemoryTest, InvalidFarcallArgumentsDoNotAcquireBanks) {
  // A bad integer bank argument fails without changing the caller's bank depth.
  AddFarcallProbe("return n;");
  Run("try { __builtin_farcall(\"far\", 0)([\"bad\"], []); } catch (e) {} "
      "observe(L[0], K[0]);");
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].depth, 1);
}

TEST_F(SiglusCallMemoryTest, StoredCommandSurvivesGarbageCollection) {
  // The scene module roots a borrowed adapter after its script alias is
  // removed.
  auto callback = AddProbe("stored", "observe(n, K[0]); return n;");
  runtime.vm->globals_->erase("command");
  runtime.vm->CollectGarbage();
  runtime.vm->InvokeUntilBlocked(callback, {sr::Value(17)});
  ASSERT_EQ(observations.size(), 1);
  EXPECT_EQ(observations[0].integer, 17);
}

// ------------------------------------------------------------------------------

class FrameActionIntegrationTest : public ::testing::Test {
 protected:
  Archive archive = MakeEmptyArchive();
  SiglusMachine runtime;
  Stage stage;
  LocalConfig config;
  std::shared_ptr<FrameActionManager> manager;
  FrameAction::Target target{1, 5};
  sr::Module* owner = nullptr;
  int calls = 0;

  void SetUp() override {
    ASSERT_TRUE(InitializeFlowRuntime(runtime, archive));
    FakeScene{.scene_name = "menu", .command_name = "open"}.AddTo(archive);
    FakeScene{.scene_name = "animation",
              .command_name = "animate",
              .args = {Type::FrameAction}}
        .AddTo(archive);
    owner = runtime.loader->Load(target.scene_id);
    manager = std::make_shared<FrameActionManager>(stage, config, 2);
    srbind::module_ bindings(runtime.vm->gc_.get(), runtime.vm->globals_.get());
    runtime.frame_action_manager = manager;
    runtime.counter_list = std::make_shared<CounterList>(0);
    for (const auto* name : {"1_counter", "1_frame_action"}) {
      const auto* bind = SiglusBindingRegistry::Find(name);
      ASSERT_NE(bind, nullptr);
      (*bind)(runtime);
    }
    bindings.def("replacement", [this](sr::Value) { ++calls; });
    manager->Start({manager->global_action(), {}}, -1, false, target, {});
  }

  void ReplaceEntry() {
    (*owner->globals)[GetUsercmdId(target.entry)] =
        runtime.vm->globals_->at("replacement");
  }

  void Dispatch() {
    manager->AdvanceAndDispatch(std::chrono::milliseconds(1),
                                std::chrono::milliseconds(1));
  }
};

TEST_F(FrameActionIntegrationTest, DispatchUsesCurrentEntry) {
  // Replace an entry after starting; each dispatch must fetch its new callable.
  ReplaceEntry();
  Dispatch();
  EXPECT_EQ(calls, 1);
  srbind::module_ bindings(runtime.vm->gc_.get(), runtime.vm->globals_.get());
  bindings.def("replacement", [this](sr::Value) { calls += 10; });
  ReplaceEntry();
  Dispatch();
  EXPECT_EQ(calls, 11);
}

TEST_F(FrameActionIntegrationTest, DispatchUsesFrameActionTraceCategory) {
  // A manager-dispatched callback reaches the frame-action trace stream.
  LogCapture output;
  SetupLogging(Severity::Error, {.frame_action = true});
  (*SiglusBindingRegistry::Find("trace"))(runtime);
  Execute(*runtime.vm, R"(
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
  (*SiglusBindingRegistry::Find("trace"))(runtime);
  Execute(*runtime.vm, R"(
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
  EXPECT_EQ(runtime.loader->ResolveUserCommand(*owner, "animate").target,
            (FrameAction::Target{other, 5}));
  Dispatch();
  EXPECT_EQ(calls, 1);
}

TEST_F(FrameActionIntegrationTest, CallbackSurvivesGarbageCollection) {
  // Keep the replacement only in scene globals; collection must retain it.
  ReplaceEntry();
  runtime.vm->globals_->erase("replacement");
  runtime.vm->CollectGarbage();
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
  action->arguments = {7};
  action->GetCounter()->Set(23);
  owner->globals->erase(GetUsercmdId(target.entry));
  EXPECT_THROW(manager->End({action, {}}), sr::RuntimeError);
  EXPECT_FALSE(action->IsActive());
  EXPECT_FALSE(action->end_action);
  EXPECT_TRUE(action->arguments.empty());
  EXPECT_EQ(action->end_time, 0);
  EXPECT_EQ(action->GetCounter()->Get(), 0);
}

TEST_F(FrameActionIntegrationTest, DispatchConvertsGlobalArgumentsInOrder) {
  // Dispatch mixed arguments; the native action handle precedes typed values.
  srbind::module_ bindings(runtime.vm->gc_.get(), runtime.vm->globals_.get());
  bindings.def(
      "replacement", [this](sr::Value handle, int number, std::string text) {
        ++calls;
        const auto* instance = handle.Get_if<sr::NativeInstance>();
        ASSERT_NE(instance, nullptr);
        EXPECT_EQ(sr::Value(instance->klass),
                  runtime.vm->builtins_->at("%%siglus_frame_action_class"));
        EXPECT_EQ(number, 7);
        EXPECT_EQ(text, "argument");
        (*runtime.vm->globals_)["observed_action"] = handle;
      });
  ReplaceEntry();
  manager->global_action()->arguments = {7, std::string("argument")};

  Dispatch();

  ASSERT_EQ(calls, 1);
  Execute(*runtime.vm, "observed_action.counter.set(47);");
  EXPECT_EQ(manager->global_action()->counter->Get(), 47);
}

TEST_F(FrameActionIntegrationTest,
       DispatchUsesObjectFactoryInstalledAfterBinding) {
  // Install the owner factory after binding; place its result before arguments.
  manager->CancelGlobals();
  stage.objects.Resize(kLayerFg, 1);
  auto object = stage.objects.Get(kLayerFg, 0);
  int converted = 0;
  runtime.make_object = [&](ActionOwner owner) {
    ++converted;
    EXPECT_EQ(owner, object);
    return sr::Value(89);
  };
  srbind::module_ bindings(runtime.vm->gc_.get(), runtime.vm->globals_.get());
  bindings.def("replacement", [this](sr::Value handle, int owner, int number,
                                     std::string text) {
    ++calls;
    EXPECT_NE(handle.Get_if<sr::NativeInstance>(), nullptr);
    EXPECT_EQ(owner, 89);
    EXPECT_EQ(number, 7);
    EXPECT_EQ(text, "argument");
  });
  ReplaceEntry();
  manager->Start({object->GetFrameAction(), object}, -1, false, target,
                 {7, std::string("argument")});

  Dispatch();

  EXPECT_EQ(calls, 1);
  EXPECT_EQ(converted, 1);
}

TEST_F(FrameActionIntegrationTest,
       FinalCallbackExposesFinalStateThroughNativeHandle) {
  // End through the manager; the script handle exposes final flags and time.
  int end_action = -1;
  int time = -1;
  srbind::module_ bindings(runtime.vm->gc_.get(), runtime.vm->globals_.get());
  bindings.def("observe_frame", [&](int end, int counter) {
    ++calls;
    end_action = end;
    time = counter;
  });
  Execute(*runtime.vm, R"(
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
  runtime.vm->globals_->erase("frame_action");
  runtime.vm->globals_->erase("frame_action_ch");
  runtime.vm->CollectGarbage();
  (*runtime.vm->globals_)["retained_action"] =
      runtime.make_frame_action(ActionReference{manager->global_action(), {}});
  (*runtime.vm->globals_)["retained_list"] = runtime.make_frame_action_list({});

  Execute(*runtime.vm, R"(
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
  Execute(*runtime.vm, R"(
    caught = 0;
    try { frame_action_ch.resize(3); } catch (error) { caught = 1; }
  )");
  EXPECT_EQ(runtime.vm->globals_->at("caught"), sr::Value(1));
  EXPECT_EQ(manager->global_channels().size(), 2);
}

}  // namespace libsiglus::binding
