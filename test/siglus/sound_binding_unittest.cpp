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

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <fstream>
#include <sstream>
#include "core/bgm_table.hpp"
#include "core/object_internal/objdrawer.hpp"
#include "headless_resource_host.hpp"
#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/sound.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/runtime_checkpoint.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "mock_clock.hpp"
#include "poller_helpers.hpp"
#include "recording_audio_test_support.hpp"
#include "serialization_test_helpers.hpp"
#include "utilities/file.hpp"
#include "vm/future.hpp"
#include "vm/object.hpp"
#include "vm/serialization.hpp"

#include <memory>

namespace {
using namespace libsiglus;
namespace sr = serilang;

class SiglusSoundBindingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::ofstream(directory.dir() / "tone.wav", std::ios::binary)
        << ResourceWave();
    scanner->IndexDirectory(directory.dir());
    for (const char* line :
         {"SCREEN_SIZE = 640,480", "MWND.CNT = 1", "MWND.0.MOJI_CNT = 42,4",
          "MWND.0.MOJI_SIZE = 20", "WAKU.CNT = 0", "WAKU.BTN.CNT = 0",
          "BGM.000 = \"track\",\"tone\",0,1024,0"})
      gameexe.parseLine(line);
    runtime.system =
        std::make_unique<System>(gameexe, stage, local, global, scanner,
                                 MwndConfig::ParseSiglus(gameexe));
    runtime.system->text().BindGraphics({.request_refresh = [] {}});
    runtime.archive = std::make_shared<Archive>(
        std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
    runtime.gameexe = std::make_shared<Gameexe>();
    runtime.bgm_table = std::make_shared<BgmTable>(
        std::vector<std::string>{"track"}, std::vector<bool>{false});
    runtime.binding_names = {"sound"};
    runtime.GameplayMachine().Initialize();
    auto timer = std::make_unique<MockClock>();
    clock = timer.get();
    runtime.GameplayMachine().vm->scheduler_ =
        sr::Scheduler(std::make_unique<sr::FakePoller>(), std::move(timer));
  }
  std::string Save() {
    return serialization_test::Save(runtime.CaptureCheckpoint(),
                                    serialization_test::Format::Binary);
  }
  void Load(const std::string& bytes) {
    SiglusRuntime::CheckpointState state;
    serialization_test::Load(bytes, state, serialization_test::Format::Binary,
                             MakeSerializationContext(runtime));
    state.Prepare(runtime);
    state.Commit(runtime);
  }
  void TearDown() override {
    stage.Reset();
    runtime.system->text().Reset();
  }
  void Run(std::string text) {
    binding::Execute(*runtime.GameplayMachine().vm, std::move(text));
  }
  sr::Value& Global(const char* name) {
    return runtime.GameplayMachine().vm->globals_->at(name);
  }
  void Poll() {
    clock->AdvanceTime(std::chrono::milliseconds(5));
    runtime.GameplayMachine().vm->scheduler_.DrainExpiredTimers();
  }
  ScopedDummyDrivers drivers;
  Gameexe gameexe;
  Stage stage;
  LocalConfig local;
  GlobalConfig global;
  std::shared_ptr<AssetScanner> scanner = std::make_shared<AssetScanner>();
  ScopedTemporary directory;
  SiglusRuntime runtime;
  MockClock* clock = nullptr;
};

TEST_F(SiglusSoundBindingTest, BgmFadeReportsPlayUntilStopped) {
  // A long fade remains playing; stopping the backend makes check report Free.
  Run("bgm.play(\"track\"); bgm.stop(60000); result = bgm.check();");
  EXPECT_EQ(Global("result"), sr::Value(1));
  runtime.system->sound().BgmStop();
  Run("result = bgm.check();");
  EXPECT_EQ(Global("result"), sr::Value(0));
}

TEST_F(SiglusSoundBindingTest, PcmFadeReportsPlayUntilStopped) {
  // A looping channel reports Play during its fade and Free once halted.
  Run("channel = __SiglusPcmch(2); channel.play_loop(\"tone\"); "
      "channel.stop(60000); result = channel.check();");
  EXPECT_EQ(Global("result"), sr::Value(1));
  runtime.system->sound().WavStop(2);
  Run("result = channel.check();");
  EXPECT_EQ(Global("result"), sr::Value(0));
}

TEST_F(SiglusSoundBindingTest, BgmFadeWaitResolvesAfterPlaybackStops) {
  // A pending fade wait polls the backend and resolves when it becomes silent.
  Run("bgm.play(\"track\"); bgm.stop(60000); pending = bgm.wait_fade();");
  auto promise = sr::get_promise(Global("pending"));
  EXPECT_FALSE(promise->HasResult());
  runtime.system->sound().BgmStop();
  Poll();
  EXPECT_TRUE(promise->HasResult());
  EXPECT_FALSE(runtime.system->sound().BgmFadingOut());
}

TEST_F(SiglusSoundBindingTest, PcmWaitSurvivesHandleCollection) {
  // Collect a temporary channel wrapper while waiting; polling uses its
  // identity.
  Run("channel = __SiglusPcmch(2); channel.play_loop(\"tone\"); pending = "
      "channel.wait(); channel = nil;");
  auto promise = sr::get_promise(Global("pending"));
  EXPECT_FALSE(promise->HasResult());
  runtime.GameplayMachine().vm->CollectGarbage();
  runtime.system->sound().WavStop(2);
  Poll();
  EXPECT_TRUE(promise->HasResult());
  EXPECT_EQ(runtime.system->sound().GetWavStatus(2),
            SoundSystem::PlaybackStatus::Stopped);
}

TEST_F(SiglusSoundBindingTest, ImmediateStopClearsPendingFade) {
  // An immediate stop supersedes a long fade and leaves no fade wait pending.
  Run("bgm.play(\"track\"); bgm.stop(60000); bgm.stop(); pending = "
      "bgm.wait_fade();");
  EXPECT_TRUE(sr::get_promise(Global("pending"))->HasResult());
  EXPECT_FALSE(runtime.system->sound().BgmFadingOut());
  EXPECT_EQ(runtime.system->sound().GetBgmStatus(),
            SoundSystem::PlaybackStatus::Stopped);
}

TEST_F(SiglusSoundBindingTest, CallbackUsesPreparedChannelFromGameplay) {
  // Gameplay prepares a channel; a separate callback resumes that shared
  // intent.
  Run("channel = __SiglusPcmch(2); channel.ready_loop(\"tone\");");
  SiglusMachine callback(runtime, 0);
  callback.Initialize();
  binding::Execute(*callback.vm,
                   "channel = __SiglusPcmch(2); channel.resume();");
  EXPECT_TRUE(runtime.system->sound().WavPlaying(2));
  Run("result = channel.check();");
  EXPECT_EQ(Global("result"), sr::Value(1));
}

TEST_F(SiglusSoundBindingTest, NativeHandleRetainsChannelIdentity) {
  // Restoring a native handle reconnects it to the original logical channel.
  Run("channel = __SiglusPcmch(2); channel.ready_loop(\"tone\");");
  auto& vm = *runtime.GameplayMachine().vm;
  std::ostringstream bytes;
  {
    boost::archive::binary_oarchive archive(bytes);
    sr::SerializationSession session;
    sr::ObjectWriter writer(archive, vm, session);
    writer.Write(Global("channel").Get<sr::NativeInstance*>());
    writer.Finish();
  }
  {
    std::istringstream input(bytes.str());
    boost::archive::binary_iarchive archive(input);
    sr::SerializationSession session;
    sr::ObjectReader reader(archive, vm, session);
    (*vm.globals_)["restored"] = reader.Read();
    reader.Finish();
  }
  Run("restored.resume();");
  EXPECT_TRUE(runtime.system->sound().WavPlaying(2));
}

TEST_F(SiglusSoundBindingTest, ExistingHandleUsesRestoredPreparation) {
  // A handle retained across load resolves the committed channel preparation.
  Run("channel = __SiglusPcmch(2); channel.ready_loop(\"tone\");");
  const auto bytes = Save();
  Run("channel.ready(\"missing\");");
  Load(bytes);
  Run("channel.resume(); result = channel.check();");
  EXPECT_EQ(Global("result"), sr::Value(1));
}

TEST_F(SiglusSoundBindingTest, BgmQueriesUseDirectSoundSystemChanges) {
  // Direct owner operations must be visible through Siglus without shadow
  // state.
  runtime.system->sound().BgmPrepare("track", false);
  Run("result = bgm.check(); name = bgm.get_regist_name();");
  EXPECT_EQ(Global("result"), sr::Value(2));
  EXPECT_EQ(binding::AsString(Global("name")), "track");
}

TEST_F(SiglusSoundBindingTest, PcmPauseRemainsLiveAndResumable) {
  // Pausing a one-shot silences it and reports Pause; resuming reports Play.
  Run("channel = __SiglusPcmch(2); channel.play(\"tone\"); channel.pause(); "
      "result = channel.check();");
  EXPECT_EQ(Global("result"), sr::Value(2));
  EXPECT_FALSE(runtime.system->sound().WavPlaying(2));
  Run("channel.resume(); result = channel.check();");
  EXPECT_EQ(Global("result"), sr::Value(1));
}

TEST_F(SiglusSoundBindingTest, PausedOneShotLoadsStopped) {
  // An ordinary paused one-shot reloads stopped while its channel volume
  // survives.
  Run("channel = __SiglusPcmch(2); channel.set_volume(93); "
      "channel.play(\"tone\"); channel.pause();");
  Load(Save());
  Run("result = channel.check(); volume = channel.get_volume();");
  EXPECT_EQ(Global("result"), sr::Value(0));
  EXPECT_EQ(Global("volume"), sr::Value(93));
}

TEST_F(SiglusSoundBindingTest, WaitDoesNotClearPreparedBgm) {
  // Waiting on prepared music completes without losing its paused intent.
  Run("bgm.ready(\"track\"); pending = bgm.wait(); result = bgm.check();");
  EXPECT_TRUE(sr::get_promise(Global("pending"))->HasResult());
  EXPECT_EQ(Global("result"), sr::Value(2));
}

TEST_F(SiglusSoundBindingTest, FadeWaitWithoutFadeResolves) {
  // Playing a loop without a fade must not leave a fade-only wait pending.
  Run("bgm.play(\"track\"); pending = bgm.wait_fade();");
  EXPECT_TRUE(sr::get_promise(Global("pending"))->HasResult());
  EXPECT_TRUE(runtime.system->sound().BgmPlaying());
}

class SiglusSoundWithoutSystemTest : public ::testing::Test {};

TEST_F(SiglusSoundWithoutSystemTest, SoundOperationsRemainInert) {
  // With no System, commands retain no state and waits resolve immediately.
  SiglusRuntime runtime;
  runtime.archive = std::make_shared<Archive>(
      std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
  runtime.gameexe = std::make_shared<Gameexe>();
  runtime.binding_names = {"sound"};
  auto& machine = runtime.GameplayMachine();
  machine.Initialize();
  binding::Execute(*machine.vm,
                   "bgm.play(\"missing\"); bgm.pause(); result = bgm.check(); "
                   "pending = bgm.wait();");
  EXPECT_EQ(machine.vm->globals_->at("result"), sr::Value(0));
  EXPECT_TRUE(
      sr::get_promise(machine.vm->globals_->at("pending"))->HasResult());
}

}  // namespace
