
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
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
//
// -----------------------------------------------------------------------

#include <gtest/gtest.h>

#include "m6/compiler_pipeline.hpp"
#include "vm/disassembler.hpp"
#include "vm/list.hpp"
#include "vm/object.hpp"
#include "vm/vm.hpp"

#include <limits>
#include <memory>
#include <string>

namespace serilang_test {

class GotoTest : public ::testing::Test {
 protected:
  std::shared_ptr<serilang::GarbageCollector> gc =
      std::make_shared<serilang::GarbageCollector>();
  serilang::VM vm{gc};

  serilang::Code* Compile(std::string source) {
    m6::CompilerPipeline compiler(gc);
    compiler.compile(
        m6::SourceBuffer::Create(std::move(source), "<goto test>"));
    EXPECT_TRUE(compiler.Ok()) << compiler.FormatErrors();
    return compiler.Get();
  }

  void Run(std::string source) { vm.Evaluate(Compile(std::move(source))); }
};

using namespace serilang;

TEST_F(GotoTest, LiteralTargetJumpsForward) {
  // Skip a failing statement and reach the target assignment.
  Run("goto 3;\nthrow \"skipped\";\nresult = 42;");
  EXPECT_EQ(vm.globals_->at("result"), Value(42));
}

TEST_F(GotoTest, LiteralTargetJumpsBackward) {
  // Revisit the increment until the guard stops taking the backward jump.
  Run("n = 0;\nn += 1;\nif (n < 3) goto 2;");
  EXPECT_EQ(vm.globals_->at("n"), Value(3));
}

TEST_F(GotoTest, ComputedTargetJumpsForward) {
  // Evaluate a computed expression before transferring to the assignment.
  Run("target = 3;\ngoto target + 1;\nthrow \"skipped\";\nresult = 42;");
  EXPECT_EQ(vm.globals_->at("result"), Value(42));
}

TEST_F(GotoTest, ComputedTargetJumpsBackward) {
  // Resolve a variable target on each iteration and stop after three visits.
  Run("target = 3;\nn = 0;\nn += 1;\nif (n < 3) goto target;");
  EXPECT_EQ(vm.globals_->at("n"), Value(3));
}

TEST_F(GotoTest, FirstStatementOnLineIsTargeted) {
  // Both statements share a line; entry must execute the first assignment.
  Run("goto 3;\nthrow \"skipped\";\nresult = 6; result += 1;");
  EXPECT_EQ(vm.globals_->at("result"), Value(7));
}

TEST_F(GotoTest, BlankLineAdvancesToNextStatement) {
  // A blank target line resolves forward to the following assignment.
  Run("goto 3;\nthrow \"skipped\";\n\nresult = 42;");
  EXPECT_EQ(vm.globals_->at("result"), Value(42));
}

TEST_F(GotoTest, CommentsAdvanceToNextStatement) {
  // Comment-only target lines resolve past line and block comments.
  Run("goto 3;\nthrow \"skipped\";\n// comment\n/* block\ncomment */\nresult = "
      "42;");
  EXPECT_EQ(vm.globals_->at("result"), Value(42));
}

TEST_F(GotoTest, ContinuationLinesAreNotStatementStarts) {
  // Entering a multiline expression's continuation skips that statement.
  Run("goto 4;\nthrow \"skipped\";\nresult = (\n1 + 2);\nresult = 42;");
  EXPECT_EQ(vm.globals_->at("result"), Value(42));
}

TEST_F(GotoTest, StructuralLinesAdvanceToBodyStatement) {
  // A brace-only line resolves to the executable statement inside the block.
  Run("goto 3;\nthrow \"skipped\";\n{\nresult = 42;\n}");
  EXPECT_EQ(vm.globals_->at("result"), Value(42));
}

TEST_F(GotoTest, ForHeaderClausesAreNotIndependentStarts) {
  // A split header records the loop start, but not its initializer/increment.
  auto* code = Compile("for (\ni = 0;\ni < 1;\ni += 1) {\nresult = i;\n}");
  EXPECT_FALSE(code->line_offset.contains(2));
  EXPECT_FALSE(code->line_offset.contains(4));
}

TEST_F(GotoTest, FunctionUsesAbsolutePhysicalLines) {
  // The function jumps within its own mapping using the original source lines.
  Run("fn f() {\ngoto 4;\nthrow \"skipped\";\nreturn 42;\n}\nresult = f();");
  EXPECT_EQ(vm.globals_->at("result"), Value(42));
}

TEST_F(GotoTest, MethodUsesItsOwnLineMapping) {
  // A method skips a failure and returns from its own absolute source target.
  Run("class C { fn f(self) {\ngoto 4;\nthrow \"skipped\";\nreturn 42;\n} }\n"
      "result = C().f();");
  EXPECT_EQ(vm.globals_->at("result"), Value(42));
}

TEST_F(GotoTest, AccessorUsesItsOwnLineMapping) {
  // A property getter resolves its target within its separate accessor Code.
  Run("class C { property value { get {\ngoto 4;\nthrow \"skipped\";\n"
      "return 42;\n} } }\nresult = C().value;");
  EXPECT_EQ(vm.globals_->at("result"), Value(42));
}

TEST_F(GotoTest, ScopeDirectiveHasNoExecutableLineEntry) {
  // A compile-time global declaration cannot become a runtime jump target.
  auto* code = Compile("global result;\nresult = 42;");
  EXPECT_FALSE(code->line_offset.contains(1));
}

TEST_F(GotoTest, OuterCodeExcludesNestedFunctionBody) {
  // Function bodies belong to separate Code objects and are absent outside.
  auto* code = Compile("fn f() {\nreturn 42;\n}\nresult = f();");
  EXPECT_FALSE(code->line_offset.contains(2));
}

TEST_F(GotoTest, FunctionCannotJumpIntoCallerCode) {
  // An otherwise valid outer source line has no target in the function's Code.
  EXPECT_THROW(Run("fn f() { goto 3; }\nresult = f();\nresult = 42;"),
               UnhandledError);
}

TEST_F(GotoTest, NonintegerTargetsAreCatchable) {
  // Strings, booleans, and nil must fail rather than coerce to line numbers.
  for (const auto* target : {"\"3\"", "true", "nil"}) {
    Run(std::string("try { goto ") + target + "; } catch (e) { result = 42; }");
    EXPECT_EQ(vm.globals_->at("result"), Value(42));
    vm.globals_->erase("result");
  }
}

TEST_F(GotoTest, NonpositiveTargetsFailAtRuntime) {
  // Zero and negative targets compile, then fail validation when executed.
  for (const auto* target : {"0", "-1"})
    EXPECT_THROW(Run(std::string("goto ") + target + ";"), UnhandledError);
}

TEST_F(GotoTest, TrailingLinesHaveNoFollowingTarget) {
  // A trailing comment must not resolve to an implicit function return.
  EXPECT_THROW(Run("fn f() {\ngoto 3;\n// trailing\n}\nf();"), UnhandledError);
}

TEST_F(GotoTest, LargeTargetFailsAtRuntime) {
  // An extreme positive target must report failure without indexing storage.
  EXPECT_THROW(Run("goto 2147483647;"), UnhandledError);
}

TEST_F(GotoTest, EmptyMappingFailsAtRuntime) {
  // Handwritten bytecode without line metadata has no valid jump destination.
  auto* code = gc->Allocate<Code>();
  code->const_pool = {1};
  code->Append(Push{0});
  code->Append(Goto{});
  EXPECT_THROW(vm.Evaluate(code), UnhandledError);
}

TEST_F(GotoTest, OutOfBoundsMappingFailsAtRuntime) {
  // Corrupt metadata must not move the instruction pointer outside the Code.
  auto* code = gc->Allocate<Code>();
  code->const_pool = {1};
  code->Append(Push{0});
  code->Append(Goto{});
  code->line_offset[1] = std::numeric_limits<uint32_t>::max();
  EXPECT_THROW(vm.Evaluate(code), UnhandledError);
}

TEST_F(GotoTest, OtherOperandsSurviveTransfer) {
  // Leave an operand below the target; the destination returns that operand.
  auto* code = gc->Allocate<Code>();
  code->const_pool = {42, 5};
  code->Append(Push{0});
  code->Append(Push{1});
  code->Append(Goto{});
  code->line_offset[5] = code->code.size();
  code->Append(Return{});
  EXPECT_EQ(vm.Evaluate(code), Value(42));
}

TEST_F(GotoTest, FastLocalsSurviveTransfer) {
  // Initialize a local before jumping, then read it at the destination.
  auto* code = gc->Allocate<Code>();
  code->fast_locals.resize(1);
  code->const_pool = {42, 5};
  code->Append(Push{0});
  code->Append(StoreFast{0});
  code->Append(Push{1});
  code->Append(Goto{});
  code->line_offset[5] = code->code.size();
  code->Append(LoadFast{0});
  code->Append(Return{});
  EXPECT_EQ(vm.Evaluate(code), Value(42));
}

TEST_F(GotoTest, ExceptionHandlerSurvivesTransferOutOfTry) {
  // A raw transfer out of a try block retains its active handler.
  Run("try {\ngoto 5;\n} catch (e) { result = e; goto 6; }\n"
      "throw \"skipped\";\nthrow \"retained\";\nfinished = true;");
  EXPECT_EQ(vm.globals_->at("result").Str(), "retained");
}

TEST_F(GotoTest, TransferKeepsCurrentCallFrame) {
  // The destination inspects its frame; goto must not create a nested call.
  auto* code = gc->Allocate<Code>();
  auto* observer = gc->Allocate<NativeFunction>(
      "observe frame", [](VM&, Fiber& fiber, uint8_t, uint8_t) -> TempValue {
        EXPECT_EQ(fiber.frames.size(), 1);
        fiber.op_stack.back() = nil;
        return nil;
      });
  code->const_pool = {5, Value(observer)};
  code->Append(Push{0});
  code->Append(Goto{});
  code->line_offset[5] = code->code.size();
  code->Append(Push{1});
  code->Append(Call{});
  code->Append(Return{});
  vm.Evaluate(code);
}

TEST_F(GotoTest, InlineCallableValueCompletesNativeCallback) {
  // A native callable value completes inline and leaves no pending fiber.
  int calls = 0;
  auto* callback = gc->Allocate<NativeFunction>(
      "callback", [&](VM&, Fiber& fiber, uint8_t, uint8_t) -> TempValue {
        ++calls;
        fiber.op_stack.back() = nil;
        return nil;
      });
  vm.InvokeUntilBlocked(Value(callback), {});
  EXPECT_EQ(calls, 1);
  EXPECT_FALSE(vm.HasPendingWork());
}

TEST_F(GotoTest, BoundArgumentOverflowIsCatchable) {
  // Hidden bound arguments must not wrap the VM's argument counter.
  auto* function = gc->Allocate<Function>(Compile("result = 42;"));
  auto* bound =
      gc->Allocate<BoundMethod>(Value(function), std::vector<Value>(256, nil));
  EXPECT_THROW(vm.InvokeUntilBlocked(Value(bound), {}), RuntimeError);
}

TEST_F(GotoTest, GotoRequiresExpressionAndSemicolon) {
  // Missing syntax elements produce compiler errors, not executable chunks.
  for (const auto* source : {"goto;", "goto 1"}) {
    m6::CompilerPipeline compiler(gc);
    compiler.compile(m6::SourceBuffer::Create(source, "<invalid goto>"));
    EXPECT_FALSE(compiler.Ok());
  }
}

TEST_F(GotoTest, LiteralGotoHasStackBasedDisassembly) {
  // A literal target remains a push followed by the runtime jump instruction.
  const std::string dump = Disassembler().Dump(*Compile("goto 1;"));
  EXPECT_NE(dump.find("GOTO"), std::string::npos);
  EXPECT_NE(dump.find("PUSH"), std::string::npos);
}

}  // namespace serilang_test
