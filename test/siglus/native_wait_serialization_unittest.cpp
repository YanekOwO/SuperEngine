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

#include "libsiglus/native_wait_serialization.hpp"
#include "core/counter.hpp"
#include "headless_resource_host.hpp"
#include "libsiglus/archive.hpp"
#include "libsiglus/execution_serialization.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "mock_clock.hpp"
#include "poller_helpers.hpp"
#include "siglus/execution_test_helpers.hpp"
#include "systems/sound_system.hpp"
#include "vm/future.hpp"
#include "vm/vm.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <memory>

namespace libsiglus {
namespace {
using namespace std::chrono_literals;
namespace sr = serilang;

class SiglusNativeWaitSerializationTest : public ::testing::Test {
 protected:
  void Initialize(SiglusRuntime& runtime, MockClock*& clock) {
    runtime.archive = std::make_shared<Archive>(
        std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
    runtime.binding_names = {"0_memory", "flow"};
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

  sr::Value MakeWait(const std::shared_ptr<Counter>& counter) {
    auto& machine = source.GameplayMachine();
    return lop::MakePollingWaitFuture(
        *machine.vm, [counter] { return counter->Get() >= 10; }, false, nullptr,
        5ms, {},
        DescribePollingWait(machine.WaitScope(),
                            {.kind = PollingWaitDescription::Kind::Counter,
                             .counter = counter,
                             .value = 10}));
  }

  void RestoreWait(sr::Value future,
                   const std::shared_ptr<Counter>& counter,
                   bool activate) {
    ExecutionWriter writer;
    const auto promise = writer.PromiseId(sr::get_promise(future));
    const auto bytes = CaptureNativeWaits(source.GameplayMachine(), writer);
    const auto loaded = serialization_test::RoundTripSnapshot(
        std::make_pair(counter, bytes), source, target);
    ExecutionReader reader;
    PrepareNativeWaits(target.GameplayMachine(), reader, loaded.second);
    restored_counter = loaded.first;
    restored_promise = reader.Promise(promise);
    if (activate)
      ActivateNativeWaits(target.GameplayMachine());
  }

  void PollTarget() {
    target_clock->AdvanceTime(5ms);
    target.GameplayMachine().vm->scheduler_.DrainExpiredTimers();
  }

  SiglusRuntime source, target;
  MockClock *source_clock = nullptr, *target_clock = nullptr;
  std::shared_ptr<Counter> restored_counter;
  std::shared_ptr<sr::Promise> restored_promise;
};

TEST_F(SiglusNativeWaitSerializationTest,
       RestoresCounterWaitAgainstCapturedState) {
  // A pending producer reconnects to a new counter with the captured progress.
  auto counter = std::make_shared<Counter>();
  counter->Set(4);
  auto future = MakeWait(counter);
  RestoreWait(future, counter, true);
  EXPECT_EQ(restored_counter->Get(), 4);
  PollTarget();
  EXPECT_FALSE(restored_promise->HasResult());
  restored_counter->Set(10);
  PollTarget();
  ASSERT_EQ(restored_promise->status, sr::Promise::Status::Resolved);
  EXPECT_EQ(restored_promise->result->value(), sr::Value(0));
}

TEST_F(SiglusNativeWaitSerializationTest, RestoredProducerWakesSavedVmAwait) {
  // A producer reconnects to the exact promise awaited by the restored fiber.
  auto counter = std::make_shared<Counter>();
  auto future = MakeWait(counter);
  auto& original = *source.GameplayMachine().vm;
  (*original.globals_)["pending"] = future;
  auto* code = original.gc_->Allocate<sr::Code>();
  code->const_pool = {future};
  code->Append(sr::Push{0});
  code->Append(sr::Await{});
  code->Append(sr::Return{});
  original.AddFiber(code);
  original.RunReadyUntilBlocked();
  ExecutionWriter writer;
  ExecutionReader reader;
  auto loaded = serialization_test::RoundTripMachineResource(
      counter, writer, reader, source.GameplayMachine(),
      target.GameplayMachine());
  PrepareNativeWaits(target.GameplayMachine(), reader, loaded.second);
  reader.FinishRestore();
  ActivateNativeWaits(target.GameplayMachine());
  loaded.first->Set(10);
  target_clock->AdvanceTime(5ms);
  auto& restored = *target.GameplayMachine().vm;
  EXPECT_EQ(restored.RunReadyUntilBlocked(), sr::Value(0));
  EXPECT_EQ(sr::get_promise(restored.globals_->at("pending"))->status,
            sr::Promise::Status::Resolved);
}

TEST_F(SiglusNativeWaitSerializationTest, PreparationDoesNotStartProducer) {
  // Prepared producers remain inert until the runtime commits and activates.
  auto counter = std::make_shared<Counter>();
  auto future = MakeWait(counter);
  RestoreWait(future, counter, false);
  restored_counter->Set(10);
  PollTarget();
  EXPECT_FALSE(restored_promise->HasResult());
  ActivateNativeWaits(target.GameplayMachine());
  PollTarget();
  EXPECT_TRUE(restored_promise->HasResult());
}

TEST_F(SiglusNativeWaitSerializationTest,
       UnregisteredPollingDescriptionIsRejected) {
  // An opaque pending lambda cannot silently become a dead future in a save.
  auto& machine = source.GameplayMachine();
  lop::MakePollingWaitFuture(
      *machine.vm, [] { return false; }, false, nullptr, 5ms, {},
      machine.WaitScope());
  ExecutionWriter writer;
  EXPECT_THROW(CaptureNativeWaits(machine, writer), std::runtime_error);
}

TEST_F(SiglusNativeWaitSerializationTest,
       CompletedWaitDoesNotCreateProducerRecord) {
  // Already satisfied predicates stay resolved and require no restored polling.
  auto counter = std::make_shared<Counter>();
  counter->Set(10);
  auto future = MakeWait(counter);
  RestoreWait(future, counter, true);
  EXPECT_TRUE(target.GameplayMachine().restored_native_waits.empty());
}

TEST_F(SiglusNativeWaitSerializationTest,
       RestoredBgmWaitWithoutSystemResolves) {
  // A restored sound wait completes without inventing state when no owner
  // exists.
  auto& machine = source.GameplayMachine();
  auto future = lop::MakePollingWaitFuture(
      *machine.vm, [] { return false; }, false, nullptr, 5ms, {},
      DescribePollingWait(machine.WaitScope(),
                          {.kind = PollingWaitDescription::Kind::Bgm}));
  RestoreWait(future, nullptr, true);
  PollTarget();
  EXPECT_TRUE(restored_promise->HasResult());
}

TEST_F(SiglusNativeWaitSerializationTest, RestoredPcmWaitPreservesPause) {
  // Polling restored PCM waits consults the owner without clearing its live
  // pause.
  ScopedDummyDrivers drivers;
  target.gameexe = std::make_shared<Gameexe>();
  for (const char* line :
       {"SCREEN_SIZE = 640,480", "MWND.CNT = 0", "WAKU.CNT = 0"})
    target.gameexe->parseLine(line);
  target.stage = std::make_unique<Stage>();
  target.asset_scanner = std::make_shared<AssetScanner>();
  target.system = std::make_unique<System>(
      *target.gameexe, *target.stage, target.local_config, target.global_config,
      target.asset_scanner, MwndConfig::ParseSiglus(*target.gameexe));
  target.system->text().BindGraphics({.request_refresh = [] {}});
  target.system->sound().WavPrepare("tone", true, 2);
  target.system->sound().WavPause(2);
  auto& machine = source.GameplayMachine();
  auto future = lop::MakePollingWaitFuture(
      *machine.vm, [] { return false; }, false, nullptr, 5ms, {},
      DescribePollingWait(
          machine.WaitScope(),
          {.kind = PollingWaitDescription::Kind::Pcm, .index = 2}));
  RestoreWait(future, nullptr, true);
  PollTarget();
  EXPECT_TRUE(restored_promise->HasResult());
  EXPECT_EQ(target.system->sound().GetWavStatus(2),
            SoundSystem::PlaybackStatus::Paused);
}

}  // namespace
}  // namespace libsiglus
