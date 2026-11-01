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

#include "libsiglus/bindings/trace.hpp"
#include "libsiglus/bindings/loader.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/recompiler.hpp"
#include "log/core.hpp"
#include "log_capture.hpp"
#include "m6/compiler_pipeline.hpp"
#include "m6/vm_factory.hpp"
#include "siglus/fake_scene.hpp"
#include "srbind/srbind.hpp"
#include "vm/function.hpp"
#include "vm/future.hpp"
#include "vm/string.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace libsiglus::binding {
namespace sr = serilang;

class SiglusTraceTest : public ::testing::Test {
 protected:
  LogCapture output;
  SiglusMachine runtime;

  void SetUp() override {
    runtime.vm = std::make_unique<sr::VM>(m6::VMFactory::Create());
    const auto* binding = SiglusBindingRegistry::Find("trace");
    ASSERT_NE(binding, nullptr);
    (*binding)(runtime);
  }

  sr::Function* Compile(std::string source) {
    m6::CompilerPipeline compiler(runtime.vm->gc_);
    compiler.compile(m6::SourceBuffer::Create(std::move(source), "<trace>"));
    if (!compiler.Ok())
      throw std::runtime_error(compiler.FormatErrors());
    return runtime.vm->gc_->Allocate<sr::Function>(compiler.Get());
  }

  std::shared_ptr<sr::Promise> InstallGate() {
    auto* future = runtime.vm->gc_->Allocate<sr::Future>();
    (*runtime.vm->globals_)["gate"] = future;
    return future->promise;
  }

  bool GeneratesTrace(bool trace, bool debug = false) {
    Recompiler compiler(runtime.vm->gc_);
    compiler.trace_enabled_ = trace;
    compiler.is_debug_ = debug;
    compiler.Compile({Parser::ParsedToken{
        .line = 1, .token = token::Return{{Integer(42)}}}});
    EXPECT_TRUE(compiler.Ok());
    return std::ranges::any_of(compiler.GetCode()->const_pool,
                               [](const sr::Value& value) {
                                 const auto* name = value.Get_if<sr::String>();
                                 return name && name->str_ == "__builtin_trace";
                               });
  }
};

TEST_F(SiglusTraceTest, RecompilerGeneratesTraceWithoutDebug) {
  // Tracing alone inserts the trace builtin into compiled scene instructions.
  EXPECT_TRUE(GeneratesTrace(true));
}

TEST_F(SiglusTraceTest, RecompilerOmitsTraceByDefault) {
  // A scene compiled without tracing contains no trace builtin reference.
  EXPECT_FALSE(GeneratesTrace(false));
}

TEST_F(SiglusTraceTest, DebugDoesNotGenerateTracePrints) {
  // Debug instrumentation alone must not insert instruction print calls.
  EXPECT_FALSE(GeneratesTrace(false, true));
}

TEST_F(SiglusTraceTest, LoaderEnablesRuntimeSceneTracingWithoutDebug) {
  // Loading a traced scene emits its parsed location and operation at runtime.
  SetupLogging(Severity::Error, {.script = true});
  Archive archive(std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
  FakeScene{.scene_name = "test", .command_name = "callback"}.AddTo(archive);
  Loader loader(archive, *runtime.vm, false, true);
  (*SiglusBindingRegistry::Find("0_memory"))(runtime);
  auto* module = loader.Load(0);
  runtime.vm->InvokeUntilBlocked(module->globals->at("%%script"), {});
  EXPECT_EQ(output.Str(), "[0:1] ret ()\n");
}

TEST_F(SiglusTraceTest, ScriptTraceUsesUnifiedLocationAndContent) {
  // Ordinary fibers emit the location once, followed by concatenated terms.
  SetupLogging(Severity::Error, {.script = true});
  runtime.vm->InvokeUntilBlocked(
      *Compile(R"(__builtin_trace(7, 12, "value=", 42);)"), {});
  EXPECT_EQ(output.Str(), "[7:12] value=42\n");
}

TEST_F(SiglusTraceTest, ScriptSelectionSuppressesCallbackFirstInstruction) {
  // Marking occurs before callback execution, hiding even its first trace.
  SetupLogging(Severity::Error, {.script = true});
  InvokeFrameAction(*runtime.vm,
                    Compile(R"(__builtin_trace(7, 12, "callback");)"), {});
  EXPECT_TRUE(output.Str().empty());
}

TEST_F(SiglusTraceTest, FrameActionSelectionIncludesCallbackFirstInstruction) {
  // The first callback trace is categorized before any callback code executes.
  SetupLogging(Severity::Error, {.frame_action = true});
  InvokeFrameAction(*runtime.vm,
                    Compile(R"(__builtin_trace(7, 12, "callback");)"), {});
  EXPECT_EQ(output.Str(), "[7:12] callback\n");
}

TEST_F(SiglusTraceTest, FrameActionSelectionSuppressesOrdinaryScript) {
  // Callback-only output rejects unmarked ordinary script fibers.
  SetupLogging(Severity::Error, {.frame_action = true});
  runtime.vm->InvokeUntilBlocked(
      *Compile(R"(__builtin_trace(7, 12, "script");)"), {});
  EXPECT_TRUE(output.Str().empty());
}

TEST_F(SiglusTraceTest, AllSelectionIncludesScriptsAndCallbacks) {
  // Both entry paths share the same output format when both are selected.
  SetupLogging(Severity::Error, ParseTraceOptions("all"));
  auto* callback = Compile(R"(__builtin_trace(7, 12, "message");)");
  runtime.vm->InvokeUntilBlocked(*callback, {});
  InvokeFrameAction(*runtime.vm, callback, {});
  EXPECT_EQ(output.Str(), "[7:12] message\n[7:12] message\n");
}

TEST_F(SiglusTraceTest, CallbackCategorySurvivesSuspension) {
  // A marked callback resumes on the scheduler and remains callback output.
  SetupLogging(Severity::Error, {.frame_action = true});
  auto gate = InstallGate();
  InvokeFrameAction(
      *runtime.vm, Compile(R"(await gate; __builtin_trace(7, 12, "resumed");)"),
      {});
  gate->Resolve(sr::nil);
  runtime.vm->RunReadyUntilBlocked();
  EXPECT_EQ(output.Str(), "[7:12] resumed\n");
}

TEST_F(SiglusTraceTest, ScriptSelectionSuppressesResumedCallback) {
  // A callback does not leak into script output after its first suspension.
  SetupLogging(Severity::Error, {.script = true});
  auto gate = InstallGate();
  InvokeFrameAction(*runtime.vm,
                    Compile(R"(await gate; __builtin_trace(7, 12, "hidden");)"),
                    {});
  gate->Resolve(sr::nil);
  runtime.vm->RunReadyUntilBlocked();
  EXPECT_TRUE(output.Str().empty());
}

TEST_F(SiglusTraceTest, CallbackCategorySurvivesExplicitYield) {
  // Yield replaces the completion promise; the wrapper still marks the fiber.
  SetupLogging(Severity::Error, {.frame_action = true});
  runtime.vm->InvokeUntilBlocked(*Compile(R"(
    fn callback() { yield nil; __builtin_trace(7, 12, "resumed"); }
  )"),
                                 {});
  InvokeFrameAction(*runtime.vm, runtime.vm->globals_->at("callback"), {});
  auto* fiber = runtime.vm->fibres_.back();
  ASSERT_EQ(fiber->state, sr::FiberState::Suspended);
  fiber->state = sr::FiberState::Running;
  runtime.vm->scheduler_.PushTask(fiber);
  runtime.vm->RunReadyUntilBlocked();
  EXPECT_EQ(output.Str(), "[7:12] resumed\n");
}

TEST_F(SiglusTraceTest, NestedCallsKeepCallbackCategory) {
  // A normal call on the callback fiber retains the frame-action category.
  SetupLogging(Severity::Error, {.frame_action = true});
  InvokeFrameAction(*runtime.vm, Compile(R"(
    fn nested() { __builtin_trace(7, 12, "nested"); }
    nested();
  )"),
                    {});
  EXPECT_EQ(output.Str(), "[7:12] nested\n");
}

TEST_F(SiglusTraceTest, SpawnedChildrenRemainScriptWork) {
  // A separate child of a callback is unmarked and appears in script output.
  SetupLogging(Severity::Error, {.script = true});
  InvokeFrameAction(*runtime.vm, Compile(R"(
    fn child() { __builtin_trace(7, 13, "child"); }
    __builtin_trace(7, 12, "parent");
    spawn child();
  )"),
                    {});
  runtime.vm->RunReadyUntilBlocked();
  EXPECT_EQ(output.Str(), "[7:13] child\n");
}

TEST_F(SiglusTraceTest,
       TracedCallbacksPreserveArgumentsThroughGarbageCollection) {
  // The wrapper retains callable arguments while a callback explicitly
  // collects.
  SetupLogging(Severity::Error, {.frame_action = true});
  srbind::module_ bindings(runtime.vm->gc_.get(), runtime.vm->globals_.get());
  bindings.def("collect", [](sr::VM& vm) { vm.CollectGarbage(); });
  runtime.vm->InvokeUntilBlocked(*Compile(R"(
    fn callback(value) { collect(); __builtin_trace(7, 12, value); }
  )"),
                                 {});
  InvokeFrameAction(*runtime.vm, runtime.vm->globals_->at("callback"),
                    {runtime.vm->gc_->Allocate<sr::String>("retained")});
  EXPECT_EQ(output.Str(), "[7:12] retained\n");
}

TEST_F(SiglusTraceTest, InlineCallbackFailuresStillPropagate) {
  // An inline failure through the tagged wrapper must reach its native caller.
  SetupLogging(Severity::Error, {.frame_action = true});
  EXPECT_THROW(
      InvokeFrameAction(*runtime.vm, Compile(R"(throw "failure";)"), {}),
      sr::RuntimeError);
}

TEST_F(SiglusTraceTest, SuspendedCallbackFailuresRemainUnobserved) {
  // Classification installs no observer, so an asynchronous failure is
  // reported.
  SetupLogging(Severity::Error, {.frame_action = true});
  auto gate = InstallGate();
  InvokeFrameAction(*runtime.vm, Compile(R"(await gate; throw "failure";)"),
                    {});
  gate->Resolve(sr::nil);
  EXPECT_THROW(runtime.vm->RunReadyUntilBlocked(), sr::UnhandledError);
}

TEST_F(SiglusTraceTest, TracePreservesStringValueEscaping) {
  // Runtime string annotations retain their existing quoting and escaping.
  SetupLogging(Severity::Error, {.script = true});
  runtime.vm->InvokeUntilBlocked(*Compile(R"(
    __builtin_trace(7, 12, __builtin_dbgvalue("a\nb"));
  )"),
                                 {});
  EXPECT_EQ(output.Str(), "[7:12] \"a\\nb\"\n");
}

}  // namespace libsiglus::binding
