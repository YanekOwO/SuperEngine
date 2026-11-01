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

#include "core/game_config.hpp"
#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/flow.hpp"
#include "libsiglus/bindings/frame_action.hpp"
#include "libsiglus/bindings/loader.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/intern_name.hpp"
#include "libsiglus/lexeme.hpp"
#include "libsiglus/recompiler.hpp"
#include "libsiglus/scene_entry.hpp"
#include "m6/compiler_pipeline.hpp"
#include "srbind/srbind.hpp"
#include "vm/exception.hpp"
#include "vm/function.hpp"
#include "vm/gc.hpp"
#include "vm/promise.hpp"
#include "vm/value.hpp"

#include <any>
#include <chrono>
#include <cstdint>
#include <initializer_list>
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

class SiglusSceneEntryTest : public ::testing::Test {
 protected:
  std::string pack_data = std::string(sizeof(Pack_hdr), '\0');
  xorkey_t key{};
  Archive archive{pack_data, key};
  sr::VM vm{std::make_shared<sr::GarbageCollector>()};
  Loader loader{archive, vm, false};
  sr::Module* module = nullptr;

  static void AppendInt(std::string& data, int32_t value) {
    data.append(reinterpret_cast<const char*>(&value), sizeof(value));
  }

  static void AppendElement(std::string& code,
                            std::initializer_list<int> elements) {
    code += static_cast<char>(ByteCode::Marker);
    for (int element : elements) {
      code += static_cast<char>(ByteCode::Push);
      AppendInt(code, static_cast<int32_t>(Type::Int));
      AppendInt(code, element);
    }
  }

  int AddCommandScene(std::string scene_name,
                      std::string command_name,
                      std::vector<Type> args = {},
                      bool included = false,
                      std::string body = {}) {
    const int scene_id = archive.raw_scene_data_.size();
    std::string code(5, '\0');
    code[0] = static_cast<char>(ByteCode::Return);
    const int32_t offset = code.size();
    for (std::size_t i = 0; i < args.size(); ++i) {
      code += static_cast<char>(ByteCode::Declare);
      AppendInt(code, static_cast<int32_t>(args[i]));
      AppendInt(code, i);
    }
    code += static_cast<char>(ByteCode::Arg);
    code += body;
    code += static_cast<char>(ByteCode::Return);
    code.append(4, '\0');
    code += static_cast<char>(ByteCode::End);
    Scene_hdr header{
        .header_size = sizeof(Scene_hdr),
        .scene_offset = sizeof(Scene_hdr),
        .scene_size = static_cast<int32_t>(code.size()),
        .zlabel_list_offset =
            static_cast<int32_t>(sizeof(Scene_hdr) + code.size()),
        .zlabel_cnt = 1,
    };
    const std::u16string name(command_name.begin(), command_name.end());
    if (!included) {
      header.cmdlist_offset = header.zlabel_list_offset + sizeof(int32_t);
      header.cmd_cnt = 1;
      header.cmd_nameidx_offset = header.cmdlist_offset + sizeof(int32_t);
      header.cmd_nameidx_cnt = 1;
      header.cmd_name_offset = header.cmd_nameidx_offset + 2 * sizeof(int32_t);
      header.cmd_name_cnt = name.size();
    }
    std::string scene(reinterpret_cast<const char*>(&header), sizeof(header));
    scene += code;
    AppendInt(scene, offset);
    if (!included) {
      AppendInt(scene, offset);
      AppendInt(scene, 0);
      AppendInt(scene, name.size());
      scene.append(reinterpret_cast<const char*>(name.data()),
                   name.size() * sizeof(char16_t));
    } else {
      archive.cmd_map_[command_name] = archive.cmd_.size();
      archive.cmd_.push_back(Command{.scene_id = scene_id,
                                     .offset = offset,
                                     .name = std::move(command_name)});
    }
    archive.raw_scene_data_.push_back(std::move(scene));
    archive.scene_names_.push_back(scene_name);
    archive.scn_map_[std::move(scene_name)] = scene_id;
    return scene_id;
  }

  void AddCallbackScene(std::vector<Type> args, std::string body = {}) {
    AddCommandScene("ui", "animate", std::move(args), true, std::move(body));
  }

  void SetUp() override {
    AddCommandScene("menu", "open");
    module = loader.Load(0);
  }
};

class SiglusCallMemoryTest : public SiglusSceneEntryTest {
 protected:
  SiglusMachine runtime;
  struct Observation {
    int integer;
    std::string string;
    std::size_t depth;
  };
  std::vector<Observation> observations;

  void SetUp() override {
    runtime.vm =
        std::make_unique<sr::VM>(std::make_shared<sr::GarbageCollector>());
    runtime.loader = std::make_unique<Loader>(archive, *runtime.vm, false);
    for (const auto* name : {"0_memory", "flow"}) {
      const auto* bind = SiglusBindingRegistry::Find(name);
      ASSERT_NE(bind, nullptr);
      (*bind)(runtime);
    }
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

  void InstallTokens(std::vector<token::Token_t> body, bool debug = false) {
    std::vector<Parser::ParsedToken> tokens;
    for (auto& token : body)
      tokens.push_back({static_cast<int>(tokens.size() + 1), std::move(token)});
    Recompiler compiler(runtime.vm->gc_);
    compiler.is_debug_ = debug;
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
    std::string code;
    AppendElement(code, {39, 1});
    code += static_cast<char>(ByteCode::Push);
    AppendInt(code, static_cast<int>(Type::Int));
    AppendInt(code, 0);
    code += static_cast<char>(ByteCode::Cmd);
    for (int value :
         {0, 1, static_cast<int>(Type::Int), 0, static_cast<int>(Type::String)})
      AppendInt(code, value);
    code += static_cast<char>(ByteCode::Return);
    AppendInt(code, 1);
    AppendInt(code, static_cast<int>(Type::String));
    AddCommandScene("far", "far", {}, false, std::move(code));
    Run("fn probe(n) { " + body + " }");
    srbind::module_ math(*runtime.vm, "math");
    (*math.dict())["tostr"] = runtime.vm->globals_->at("probe");
  }

  sr::Value AddProbe(std::string name, std::string body) {
    // A real Siglus user command forwards its argument to an engine callable.
    // The test substitutes that callable to inspect the active L/K banks.
    std::string code;
    AppendElement(code, {39, 1});  // math.tostr
    AppendElement(code, {83, 0x7d000000});
    code += static_cast<char>(ByteCode::Property);
    code += static_cast<char>(ByteCode::Cmd);
    for (int value :
         {0, 1, static_cast<int>(Type::Int), 0, static_cast<int>(Type::String)})
      AppendInt(code, value);
    code += static_cast<char>(ByteCode::Return);
    AppendInt(code, 1);
    AppendInt(code, static_cast<int>(Type::String));
    const int scene = AddCommandScene(name, name, {Type::Int}, false, code);

    Run("fn probe(n) { " + body + " }");
    srbind::module_ math(*runtime.vm, "math");
    (*math.dict())["tostr"] = runtime.vm->globals_->at("probe");
    auto command = ResolveSceneCommand(*runtime.loader, name, name);
    (*runtime.vm->globals_)["command"] = sr::Value(command);
    EXPECT_EQ(Dispatcher(command)->globals,
              runtime.loader->Load(scene)->globals);
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
  for (const auto type : {Type::Int, Type::String}) {
    const std::string name = type == Type::Int ? "integer" : "string";
    std::string body;
    AppendElement(body, {83, 0x7d000000});
    body += static_cast<char>(ByteCode::Property);
    body += static_cast<char>(ByteCode::Return);
    AppendInt(body, 1);
    AppendInt(body, static_cast<int>(type));
    AddCommandScene(name, name, {type}, false, body);
    (*runtime.vm->globals_)[name] =
        sr::Value(ResolveSceneCommand(*runtime.loader, name, name));
  }
  AddCommandScene("empty", "empty");
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

TEST_F(SiglusCallMemoryTest, DebugAnnotationsRemainAtTokenBoundaries) {
  // Debug instrumentation runs at the target token before the scene resumes.
  std::vector<std::string> annotations;
  srbind::module_ bindings(runtime.vm->gc_.get(), runtime.vm->globals_.get());
  bindings.def(
      "__builtin_dbgprint",
      [&](std::vector<sr::Value> terms) {
        std::string text;
        for (const auto& value : terms)
          text += value.Str();
        annotations.push_back(std::move(text));
      },
      srbind::vararg);
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

TEST_F(SiglusSceneEntryTest, EntriesShareOneDispatcher) {
  // Scene, label, and command adapters all retain the same execution function.
  auto* dispatcher = module->globals->at("%%dispatch").Get_if<sr::Function>();
  EXPECT_EQ(Dispatcher(module->globals->at("%%script")), dispatcher);
  EXPECT_EQ(Dispatcher(module->globals->at(GetZlabelId(0))), dispatcher);
  EXPECT_EQ(Dispatcher(module->globals->at(GetUsercmdId(5))), dispatcher);
}

TEST_F(SiglusSceneEntryTest, NamedCommandResolvesThroughCachedMetadata) {
  auto expected = module->globals->at(GetUsercmdId(5));
  ASSERT_NE(expected.Get_if<sr::BoundMethod>(), nullptr);
  EXPECT_EQ(ResolveSceneCommand(loader, "MENU", "open"), expected);
}

TEST_F(SiglusSceneEntryTest, IncludedCommandLoadsDeclaringSceneOnDemand) {
  AddCallbackScene({Type::FrameAction, Type::Object});
  EXPECT_FALSE(vm.builtins_->contains("%%siglus_module@SCENE_0001"));

  const auto& resolved = loader.ResolveUserCommand(*module, "AnImAtE");

  EXPECT_TRUE(vm.builtins_->contains("%%siglus_module@SCENE_0001"));
  const auto* owner = loader.Load(1);
  EXPECT_EQ(resolved.function, owner->globals->at(GetUsercmdId(5)));
  EXPECT_EQ(Dispatcher(resolved.function)->globals, owner->globals);
  EXPECT_EQ(resolved.args,
            (std::vector<Type>{Type::FrameAction, Type::Object}));
  EXPECT_EQ(&loader.ResolveUserCommand(*module, "animate"), &resolved);
}

TEST_F(SiglusSceneEntryTest, IncludedCommandUsesAlreadyLoadedDeclaringScene) {
  AddCallbackScene({Type::FrameAction, Type::Object});
  const auto* owner = loader.Load(1);
  const auto expected = owner->globals->at(GetUsercmdId(5));

  EXPECT_EQ(ResolveSceneCommand(loader, "menu", "animate"), expected);
  EXPECT_EQ(loader.Load(1), owner);
}

TEST_F(SiglusSceneEntryTest, IncludedCommandTakesPrecedenceOverLocalCommand) {
  AddCommandScene("shared", "open", {Type::Int}, true);
  const auto& resolved = loader.ResolveUserCommand(*module, "OPEN");

  EXPECT_EQ(Dispatcher(resolved.function)->globals, loader.Load(1)->globals);
  EXPECT_EQ(resolved.args, std::vector<Type>{Type::Int});
  EXPECT_NE(resolved.function, module->globals->at(GetUsercmdId(5)));
}

TEST_F(SiglusSceneEntryTest,
       InvalidIncludedTargetDoesNotFallBackToLocalCommand) {
  archive.cmd_map_["open"] = archive.cmd_.size();
  archive.cmd_.push_back(Command{.scene_id = 99, .offset = 5, .name = "open"});

  EXPECT_THROW(loader.ResolveUserCommand(*module, "open"), sr::RuntimeError);
}

TEST_F(SiglusSceneEntryTest, LocalCommandNamesAreCaseInsensitive) {
  const int scene = AddCommandScene("other", "LoCaL");
  const auto* owner = loader.Load(scene);
  const auto expected = owner->globals->at(GetUsercmdId(5));

  EXPECT_EQ(loader.ResolveUserCommand(*owner, "LOCAL").function, expected);
  EXPECT_EQ(ResolveSceneCommand(loader, "menu", "OpEn"),
            module->globals->at(GetUsercmdId(5)));
  EXPECT_THROW(loader.ResolveUserCommand(*module, "local"), sr::RuntimeError);
}

TEST_F(SiglusSceneEntryTest, FiberResolvesIncludedCommandFromNearestScene) {
  AddCallbackScene({Type::FrameAction, Type::Object});
  sr::Fiber fiber;
  fiber.frames.push_back({});  // Native/non-scene frames are ignored.
  fiber.frames.push_back(
      {.fn = module->globals->at("%%dispatch").Get_if<sr::Function>()});
  fiber.frames.push_back({});

  const auto& resolved = loader.ResolveUserCommand(fiber, "ANIMATE");
  EXPECT_EQ(Dispatcher(resolved.function)->globals, loader.Load(1)->globals);
}

TEST_F(SiglusSceneEntryTest, FiberDoesNotSearchOlderScenesForLocalCommands) {
  const int other = AddCommandScene("other", "local");
  const auto* current = loader.Load(other);
  sr::Fiber fiber;
  fiber.frames.push_back(
      {.fn = module->globals->at("%%dispatch").Get_if<sr::Function>()});
  fiber.frames.push_back(
      {.fn = current->globals->at("%%dispatch").Get_if<sr::Function>()});

  EXPECT_EQ(
      Dispatcher(loader.ResolveUserCommand(fiber, "LOCAL").function)->globals,
      current->globals);
  EXPECT_THROW(loader.ResolveUserCommand(fiber, "open"), sr::RuntimeError);
}

TEST_F(SiglusSceneEntryTest, IncludedCommandsStillRequireSceneContext) {
  AddCallbackScene({Type::FrameAction, Type::Object});
  sr::Fiber fiber;
  EXPECT_THROW(loader.ResolveUserCommand(fiber, "animate"), sr::RuntimeError);
  sr::Module unloaded("unloaded");
  EXPECT_THROW(loader.ResolveUserCommand(unloaded, "animate"),
               sr::RuntimeError);
}

TEST_F(SiglusSceneEntryTest, IncludedFrameActionReceivesItsCounterAndOwner) {
  // The included callback assigns its own counter to its owner's rotation:
  //   arg_1.rotate_z = arg_0.counter.get();
  std::string body;
  AppendElement(body, {83, 0x7d000001, 17});
  AppendElement(body, {83, 0x7d000000, 0, 1});
  body += static_cast<char>(ByteCode::Cmd);
  for (int value : {0, 0, 0, static_cast<int>(Type::Int)})
    AppendInt(body, value);
  body += static_cast<char>(ByteCode::Assign);
  for (int value :
       {static_cast<int>(Type::Int), static_cast<int>(Type::Int), 1})
    AppendInt(body, value);
  AddCallbackScene({Type::FrameAction, Type::Object}, std::move(body));

  for (const std::string method : {"start", "start_real"}) {
    SCOPED_TRACE(method);
    SiglusMachine runtime;
    runtime.vm =
        std::make_unique<sr::VM>(std::make_shared<sr::GarbageCollector>());
    runtime.loader = std::make_unique<Loader>(archive, *runtime.vm, false);
    runtime.stage = std::make_unique<Stage>(1);
    runtime.counter_list = std::make_shared<CounterList>(0);
    runtime.frame_action_manager = std::make_shared<FrameActionManager>(
        *runtime.vm, *runtime.stage, runtime.local_config, 0);
    const auto* bind_memory = SiglusBindingRegistry::Find("0_memory");
    const auto* bind_counter = SiglusBindingRegistry::Find("1_counter");
    const auto* bind_actions = SiglusBindingRegistry::Find("1_frame_action");
    ASSERT_NE(bind_memory, nullptr);
    ASSERT_NE(bind_counter, nullptr);
    ASSERT_NE(bind_actions, nullptr);
    (*bind_memory)(runtime);
    (*bind_counter)(runtime);
    (*bind_actions)(runtime);

    // Only the object's rotation is needed; no renderer or GUI is required.
    srbind::module_ bindings(runtime.vm->gc_.get(), runtime.vm->globals_.get());
    srbind::class_<ActionOwner> owner_class(bindings, "TestOwner");
    owner_class.prop(
        "rotate_z", [](ActionOwner* o) { return (*o)->Param().rotation(); },
        [](ActionOwner* o, int value) { (*o)->Param().SetRotation(value); });
    auto& manager = *runtime.frame_action_manager;
    manager.SetObjectFactory([owner_class](ActionOwner owner) mutable {
      return sr::Value(owner_class.make_inst(std::move(owner)));
    });
    auto object = std::make_shared<GraphicsObject>();
    runtime.stage->objects[kLayerBg][0] = object;
    auto action = object->GetFrameAction();
    (*runtime.vm->globals_)["action"] =
        manager.MakeActionValue({action, object});

    m6::CompilerPipeline compiler(runtime.vm->gc_, false);
    compiler.compile(m6::SourceBuffer::Create(
        "fn start_action(){ action." + method + "(-1, \"AnImAtE\"); }",
        "<IncludedFrameActionTest>"));
    ASSERT_TRUE(compiler.Ok()) << compiler.FormatErrors();
    runtime.vm->Evaluate(compiler.Get());
    auto* start =
        runtime.vm->globals_->at("start_action").Get_if<sr::Function>();
    ASSERT_NE(start, nullptr);
    auto* caller = runtime.loader->Load(0);
    start->globals = caller->globals;
    (*caller->globals)["action"] = runtime.vm->globals_->at("action");
    runtime.vm->ScheduleExecute(start);
    runtime.vm->Run();

    ASSERT_TRUE(action->IsActive());
    EXPECT_EQ(Dispatcher(action->function)->globals,
              runtime.loader->Load(1)->globals);
    EXPECT_EQ(action->end_time, -1);
    manager.AdvanceAndDispatch(std::chrono::milliseconds(3),
                               std::chrono::milliseconds(96));
    EXPECT_EQ(object->Param().rotation(), method == "start" ? 96 : 3);
    manager.AdvanceAndDispatch(std::chrono::milliseconds(2),
                               std::chrono::milliseconds(64));
    EXPECT_EQ(object->Param().rotation(), method == "start" ? 160 : 5);
  }
}

TEST_F(SiglusSceneEntryTest, ConfiguredButtonFrameActionUsesTypedSceneCommand) {
  AddCallbackScene({Type::FrameAction, Type::Object});
  Stage stage;
  LocalConfig config;
  FrameActionManager manager(vm, stage, config, 0);
  auto object = std::make_shared<GraphicsObject>();
  manager.StartConfiguredButton(
      loader, object,
      MwndConfig::CallTarget{.scene = "UI", .command = "animate"});
  const auto action = object->GetFrameAction();
  EXPECT_TRUE(action->IsActive());
  EXPECT_EQ(action->end_time, -1);
  action->GetCounter()->Advance(std::chrono::milliseconds(32),
                                std::chrono::milliseconds(1));
  EXPECT_EQ(action->GetCounter()->Get(), 32);
  EXPECT_TRUE(action->arguments.empty());
}

TEST_F(SiglusSceneEntryTest, ConfiguredButtonFrameActionRejectsWrongSignature) {
  AddCallbackScene({Type::Int, Type::Object});
  Stage stage;
  LocalConfig config;
  FrameActionManager manager(vm, stage, config, 0);
  auto object = std::make_shared<GraphicsObject>();
  EXPECT_THROW(manager.StartConfiguredButton(
                   loader, object,
                   MwndConfig::CallTarget{.scene = "ui", .command = "animate"}),
               sr::RuntimeError);
  EXPECT_FALSE(object->GetFrameAction()->IsActive());
}

TEST_F(SiglusSceneEntryTest, FrameActionClocksBorrowLiveLocalConfiguration) {
  Stage stage(1);
  LocalConfig config;
  FrameActionManager manager(vm, stage, config, 0);
  auto global_counter = manager.global_action()->GetCounter();
  global_counter->StartGame();
  auto object = std::make_shared<GraphicsObject>();
  stage.objects[kLayerFg][0] = object;
  auto object_counter = object->GetFrameAction()->GetCounter();
  object_counter->StartGame();

  config.frame_action_time_stop = true;
  manager.AdvanceAndDispatch(std::chrono::milliseconds(10),
                             std::chrono::milliseconds(10));
  EXPECT_EQ(global_counter->Get(), 0);
  EXPECT_EQ(object_counter->Get(), 10);

  config.frame_action_time_stop = false;
  config.stage_time_stop = true;
  manager.AdvanceAndDispatch(std::chrono::milliseconds(10),
                             std::chrono::milliseconds(10));
  EXPECT_EQ(global_counter->Get(), 10);
  EXPECT_EQ(object_counter->Get(), 10);

  config = LocalConfig{};
  manager.AdvanceAndDispatch(std::chrono::milliseconds(5),
                             std::chrono::milliseconds(160));
  EXPECT_EQ(global_counter->Get(), 170);
  EXPECT_EQ(object_counter->Get(), 170);
}

TEST_F(SiglusSceneEntryTest, MissingNamedCommandDoesNotFallBackToScript) {
  ASSERT_NE(module->globals->at("%%dispatch").Get_if<sr::Function>(), nullptr);
  EXPECT_THROW(ResolveSceneCommand(loader, "menu", "missing"),
               sr::RuntimeError);
}

TEST_F(SiglusSceneEntryTest, ZlabelResolvesLabelAndFallsBackToScript) {
  auto label = module->globals->at(GetZlabelId(0));
  auto script = module->globals->at("%%script");
  ASSERT_NE(label.Get_if<sr::BoundMethod>(), nullptr);
  ASSERT_NE(script.Get_if<sr::BoundMethod>(), nullptr);
  EXPECT_EQ(ResolveSceneEntry(loader, "MENU", 0), label);
  EXPECT_EQ(ResolveSceneEntry(loader, "menu", 77), script);
}

}  // namespace libsiglus::binding
