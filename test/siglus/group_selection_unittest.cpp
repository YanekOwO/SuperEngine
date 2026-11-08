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

#include "core/group.hpp"
#include "core/input.hpp"
#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/element_parser.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "m6/compiler_pipeline.hpp"
#include "mock_clock.hpp"
#include "poller_helpers.hpp"
#include "siglus/execution_test_helpers.hpp"
#include "vm/exception.hpp"
#include "vm/function.hpp"

#include <gtest/gtest.h>

namespace {
using namespace libsiglus;
using namespace std::chrono_literals;
namespace sr = serilang;

class SiglusGroupSelectionTest : public ::testing::Test {
 protected:
  void Initialize(SiglusRuntime& runtime, MockClock*& clock) {
    runtime.archive = std::make_shared<Archive>(
        std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
    runtime.stage = std::make_unique<Stage>(1, 0, 1);
    runtime.input_event_listener = std::make_shared<InputListener>();
    runtime.script_input = std::make_shared<ScriptInput>();
    runtime.binding_names = {"0_memory", "1_group", "flow"};
    runtime.GameplayMachine().Initialize();
    auto timer = std::make_unique<MockClock>();
    clock = timer.get();
    runtime.GameplayMachine().vm->scheduler_ =
        sr::Scheduler(std::make_unique<sr::FakePoller>(), std::move(timer));
  }
  void SetUp() override {
    Initialize(source, source_clock);
    Initialize(target, target_clock);
  }
  void Run(const std::string& code) {
    auto& vm = *source.GameplayMachine().vm;
    m6::CompilerPipeline compiler(vm.gc_);
    compiler.compile(m6::SourceBuffer::Create(code, "<native operation test>"));
    if (!compiler.Ok())
      throw std::runtime_error(compiler.FormatErrors());
    auto* function = vm.gc_->Allocate<serilang::Function>(compiler.Get());
    vm.InvokeUntilBlocked(*function, {});
  }
  void Poll(SiglusRuntime& runtime, MockClock* clock) {
    clock->AdvanceTime(10ms);
    runtime.GameplayMachine().vm->scheduler_.DrainExpiredTimers();
    runtime.GameplayMachine().vm->RunReadyUntilBlocked();
  }
  auto GroupOf(SiglusRuntime& runtime) {
    return runtime.stage->groups.Get(kLayerFg, 0);
  }
  SiglusRuntime source, target;
  MockClock *source_clock, *target_clock;
};

TEST_F(SiglusGroupSelectionTest, SelectionWaitsForDecision) {
  // A blocking selection suspends its caller until the group reports a
  // decision.
  Run("g = Group(); result = await g.sel(); completed = 1;");
  EXPECT_FALSE(source.GameplayMachine().vm->globals_->contains("completed"));
  auto group = GroupOf(source);
  group->status = Group::Status::Disabled;
  group->result = Group::Result::Decided;
  group->decided_button_no = 12;
  Poll(source, source_clock);
  EXPECT_EQ(source.GameplayMachine().vm->globals_->at("result"), sr::Value(12));
}

TEST_F(SiglusGroupSelectionTest, CancellationReturnsMinusOne) {
  // A cancellable selection exposes the cancel policy and returns the legacy
  // result.
  Run("g = Group(); result = await g.sel_cancel(8);");
  auto group = GroupOf(source);
  EXPECT_TRUE(group->cancel_enabled);
  EXPECT_EQ(group->cancel_se, 8);
  group->status = Group::Status::Disabled;
  group->result = Group::Result::Canceled;
  group->decided_button_no = -1;
  Poll(source, source_clock);
  EXPECT_EQ(source.GameplayMachine().vm->globals_->at("result"), sr::Value(-1));
}

TEST_F(SiglusGroupSelectionTest, EntryConsumesExistingInput) {
  // A click already pending when selection opens cannot choose its first
  // button.
  source.input_event_listener->decide = {
      .on_down = true, .on_up = true, .down_up = true};
  Run("g = Group(); result = await g.sel();");
  EXPECT_FALSE(source.input_event_listener->decide.down_up);
  EXPECT_FALSE(source.input_event_listener->decide.on_up);
}

TEST_F(SiglusGroupSelectionTest, RestoredWaitUsesRestoredGroup) {
  // The saved fiber and native wait reconnect to the same restored stage group.
  Run("g = Group(); result = await g.sel();");
  ExecutionWriter writer;
  ExecutionReader reader;
  auto restored = serialization_test::RoundTripMachineResource(
      source.stage->groups, writer, reader, source.GameplayMachine(),
      target.GameplayMachine());
  target.stage->groups = std::move(restored.first);
  PrepareNativeWaits(target.GameplayMachine(), reader, restored.second);
  reader.FinishRestore();
  ActivateNativeWaits(target.GameplayMachine());
  auto group = GroupOf(target);
  group->status = Group::Status::Disabled;
  group->result = Group::Result::Decided;
  group->decided_button_no = 23;
  Poll(target, target_clock);
  EXPECT_EQ(target.GameplayMachine().vm->globals_->at("result"), sr::Value(23));
}

TEST_F(SiglusGroupSelectionTest, ExGroupUsesItsOwnStage) {
  // An EX-owned group completes its own waiter without changing the normal
  // stage's group.
  source.excall = std::make_unique<Excall>();
  source.excall->Alloc({.group_count = 1});
  Run("g = GroupArray(0, 1)[0]; result = await g.sel();");
  auto group = source.excall->stage.groups.Get(kLayerFg, 0);
  ASSERT_EQ(group->status, Group::Status::Waiting);
  group->decided_button_no = 19;
  group->status = Group::Status::Disabled;
  Poll(source, source_clock);
  EXPECT_EQ(source.GameplayMachine().vm->globals_->at("result"), sr::Value(19));
  EXPECT_FALSE(GroupOf(source)->decided_button_no);
}

TEST_F(SiglusGroupSelectionTest, FreeingExResourcesCancelsWait) {
  // A wait bound to EX resources releases its group when those resources die.
  source.excall = std::make_unique<Excall>();
  source.excall->Alloc({.group_count = 1});
  Run("g = GroupArray(0, 1)[0]; result = await g.sel();");
  auto group = source.excall->stage.groups.Get(kLayerFg, 0);
  source.FreeExcall(false);
  EXPECT_THROW(Poll(source, source_clock), serilang::UnhandledError);
  EXPECT_NE(group->status, Group::Status::Waiting);
}

TEST_F(SiglusGroupSelectionTest, ParserAwaitsIntegerResult) {
  // Both encoded group commands must call, await, and produce an integer.
  std::vector<Property> props;
  std::vector<Command> commands;
  std::vector<Type> args;
  elm::ElementParser parser(props, props, commands, commands, args, 0, {}, {});
  for (int operation : {0, 1}) {
    elm::ElementCode code{38, 6, -1, 0, operation};
    const auto chain = parser.Parse(code);
    ASSERT_FALSE(chain.nodes.empty());
    auto* call = std::get_if<elm::Call>(&chain.nodes.back().var);
    ASSERT_NE(call, nullptr);
    EXPECT_TRUE(call->await_result);
    EXPECT_EQ(chain.GetType(), Type::Int);
  }
}
}  // namespace
