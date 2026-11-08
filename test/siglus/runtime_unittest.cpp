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

#include <fstream>
#include "core/capture.hpp"
#include "core/frame_action_manager.hpp"
#include "core/object_internal/drawer/file.hpp"
#include "core/object_internal/objdrawer.hpp"
#include "core/selection.hpp"
#include "headless_resource_host.hpp"
#include "image_archive_test_support.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/intern_name.hpp"
#include "libsiglus/runtime_checkpoint.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "m6/compiler_pipeline.hpp"
#include "m6/vm_factory.hpp"
#include "recording_audio_test_support.hpp"
#include "serialization_test_helpers.hpp"
#include "siglus/fake_scene.hpp"
#include "srbind/srbind.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/text_window.hpp"
#include "utilities/file.hpp"
#include "vm/future.hpp"
#include "vm/promise.hpp"
#include "vm/vm.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <vector>

namespace {
using namespace libsiglus;

void Evaluate(SiglusMachine& machine, const std::string& source) {
  m6::CompilerPipeline compiler(machine.vm->gc_);
  compiler.compile(m6::SourceBuffer::Create(source, "<runtime-test>"));
  ASSERT_TRUE(compiler.Ok());
  machine.vm->Evaluate(compiler.Get());
}

TEST(SiglusRuntimeTest, ExecutionInstancesHaveIndependentHeaps) {
  // Two instances share a host; their VM globals and allocations stay separate.
  SiglusRuntime runtime;
  auto& gameplay = runtime.GameplayMachine();
  gameplay.vm = std::make_unique<serilang::VM>(m6::VMFactory::Create());
  SiglusMachine other(runtime);
  other.vm = std::make_unique<serilang::VM>(m6::VMFactory::Create());
  Evaluate(gameplay, "value = [17];");
  Evaluate(other, "value = [29];");
  gameplay.vm->CollectGarbage();
  other.vm->CollectGarbage();
  EXPECT_NE(gameplay.vm->gc_, other.vm->gc_);
  EXPECT_NE(gameplay.vm->globals_->at("value"),
            other.vm->globals_->at("value"));
  EXPECT_EQ(&gameplay.host(), &other.host());
}

TEST(SiglusRuntimeTest, MemoryBindingsShareEngineBanks) {
  // Binding a second VM must expose existing flags without reinitializing them.
  SiglusRuntime runtime;
  auto& gameplay = runtime.GameplayMachine();
  gameplay.vm = std::make_unique<serilang::VM>(m6::VMFactory::Create());
  const auto* bind = binding::SiglusBindingRegistry::Find("0_memory");
  ASSERT_NE(bind, nullptr);
  (*bind)(gameplay);
  Evaluate(gameplay, "F[3] = 47;");
  SiglusMachine other(runtime);
  other.vm = std::make_unique<serilang::VM>(m6::VMFactory::Create());
  (*bind)(other);
  Evaluate(other, "observed = F[3]; F[3] = 59;");
  EXPECT_EQ(other.vm->globals_->at("observed"), serilang::Value(47));
  Evaluate(gameplay, "observed = F[3];");
  EXPECT_EQ(gameplay.vm->globals_->at("observed"), serilang::Value(59));
}

class SiglusRuntimeCaptureTest : public ::testing::Test {
 protected:
  SiglusRuntime runtime;
  std::shared_ptr<serilang::Promise> menu_gate =
      std::make_shared<serilang::Promise>();
  std::shared_ptr<serilang::Promise> callback_capture;
  SiglusMachine* callback = nullptr;
  CmdTarget menu_target{0, 5};
  CmdTarget callback_target{1, 5};
  std::vector<CaptureKind> rendered;
  int callback_completions = 0;

  void SetUp() override {
    runtime.archive = std::make_shared<Archive>(
        std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
    binding::FakeScene{.scene_name = "menu", .command_name = "menu"}.AddTo(
        *runtime.archive);
    binding::FakeScene{.scene_name = "callback",
                       .command_name = "callback",
                       .args = {Type::FrameAction}}
        .AddTo(*runtime.archive);
    runtime.stage = std::make_unique<Stage>();
    runtime.counter_list = std::make_shared<CounterList>(0);
    runtime.frame_action_manager = std::make_shared<FrameActionManager>(
        *runtime.stage, runtime.local_config, 0);
    runtime.InitializeExcall();
    runtime.binding_names = {"0_memory", "flow", "1_counter", "1_frame_action"};
    runtime.GameplayMachine().Initialize();
    runtime.captures = std::make_shared<CaptureManager>(
        CaptureConfig{.screen_size = Size(1, 1)}, std::filesystem::path{},
        [this](const CaptureRequest& request) {
          rendered.push_back(request.kind);
          return std::make_shared<SDLSurface>(Size(1, 1));
        },
        CaptureManager::Load{});
    runtime.configure_machine = [this](SiglusMachine& machine) {
      if (machine.callback_owner < 0) {
        auto* future = machine.vm->gc_->Allocate<serilang::Future>();
        future->promise = menu_gate;
        (*machine.vm->globals_)["gate"] = future;
        Evaluate(machine, "fn replacement() { await gate; }");
        (*machine.loader->Load(menu_target.scene_id)
              ->globals)[GetUsercmdId(menu_target.entry)] =
            machine.vm->globals_->at("replacement");
      } else {
        callback = &machine;
        srbind::module_ bindings(machine.vm->gc_.get(),
                                 machine.vm->globals_.get());
        bindings.def("capture_callback", [&machine, this] {
          auto future = machine.RequestCapture(
              {.kind = CaptureKind::Tweet, .display = false});
          callback_capture = serilang::get_promise(future);
          return future;
        });
        bindings.def("record_completion", [this] { ++callback_completions; });
        Evaluate(machine,
                 "fn replacement(action) { await capture_callback(); "
                 "record_completion(); }");
        (*machine.loader->Load(callback_target.scene_id)
              ->globals)[GetUsercmdId(callback_target.entry)] =
            machine.vm->globals_->at("replacement");
      }
    };
  }

  void OpenMenu() {
    ASSERT_TRUE(runtime.OpenExcall(menu_target));
    runtime.DriveScripts();
    ASSERT_NE(&runtime.ActiveMachine(), &runtime.GameplayMachine());
  }

  std::shared_ptr<serilang::Promise> Capture(SiglusMachine& machine,
                                             CaptureKind kind,
                                             bool display = true) {
    auto future = machine.RequestCapture({.kind = kind, .display = display});
    return serilang::get_promise(future);
  }
};

TEST_F(SiglusRuntimeCaptureTest,
       ActiveExcallCaptureBypassesPausedGameplayCapture) {
  // An EX capture completes before a previously queued gameplay capture, which
  // stays deferred.
  auto gameplay = Capture(runtime.GameplayMachine(), CaptureKind::Object);
  OpenMenu();
  auto excall = Capture(runtime.ActiveMachine(), CaptureKind::Tweet);
  runtime.ProcessCaptureFrame();
  EXPECT_EQ(excall->status, serilang::Promise::Status::Resolved);
  EXPECT_FALSE(gameplay->HasResult());
  EXPECT_EQ(rendered, (std::vector<CaptureKind>{CaptureKind::Tweet}));
  menu_gate->Resolve(serilang::nil);
  runtime.DriveScripts();
  runtime.ProcessCaptureFrame();
  EXPECT_EQ(gameplay->status, serilang::Promise::Status::Resolved);
  EXPECT_EQ(rendered, (std::vector<CaptureKind>{CaptureKind::Tweet,
                                                CaptureKind::Object}));
}

TEST_F(SiglusRuntimeCaptureTest,
       PausedCaptureDoesNotControlExcallPresentation) {
  // A hidden gameplay capture must not suppress presentation for an active EX
  // capture.
  Capture(runtime.GameplayMachine(), CaptureKind::Object, false);
  OpenMenu();
  Capture(runtime.ActiveMachine(), CaptureKind::Tweet, true);
  EXPECT_TRUE(runtime.HasPendingCaptures());
  EXPECT_TRUE(runtime.CaptureDisplayEnabled());
}

TEST_F(SiglusRuntimeCaptureTest,
       CallbackCaptureCompletesWhileGameplayIsPaused) {
  // A background callback capture executes during EX and resumes its awaiting
  // callback.
  auto gameplay = Capture(runtime.GameplayMachine(), CaptureKind::Object);
  OpenMenu();
  runtime.DispatchFrameAction(
      0, {runtime.frame_action_manager->global_action(), {}},
      ScriptCall{.target = callback_target});
  ASSERT_NE(callback_capture, nullptr);
  runtime.ProcessCaptureFrame();
  runtime.Update(std::chrono::milliseconds(1), std::chrono::milliseconds(1));
  EXPECT_EQ(callback_capture->status, serilang::Promise::Status::Resolved);
  EXPECT_EQ(callback_completions, 1);
  EXPECT_FALSE(gameplay->HasResult());
  EXPECT_EQ(rendered, (std::vector<CaptureKind>{CaptureKind::Tweet}));
}

TEST_F(SiglusRuntimeCaptureTest, CallbackCaptureControlsFramePresentation) {
  // A callback's nondisplaying capture suppresses presentation even when an EX
  // menu owns input.
  OpenMenu();
  runtime.DispatchFrameAction(
      0, {runtime.frame_action_manager->global_action(), {}},
      ScriptCall{.target = callback_target});
  ASSERT_NE(callback_capture, nullptr);
  EXPECT_TRUE(runtime.HasPendingCaptures());
  EXPECT_FALSE(runtime.CaptureDisplayEnabled());
}

TEST_F(SiglusRuntimeCaptureTest, CancelledCaptureGenerationDoesNotRender) {
  // Cancelling an execution rejects its queued capture without rendering its
  // stale request.
  OpenMenu();
  auto capture = Capture(runtime.ActiveMachine(), CaptureKind::Tweet);
  runtime.ActiveMachine().Cancel();
  runtime.Update(std::chrono::milliseconds(1), std::chrono::milliseconds(1));
  EXPECT_EQ(capture->status, serilang::Promise::Status::Rejected);
  EXPECT_TRUE(rendered.empty());
  EXPECT_FALSE(runtime.HasPendingCaptures());
}

// -------------------------------------------------------------------------------

class RuntimeCheckpointArchiveTest : public ::testing::Test {};

TEST_F(RuntimeCheckpointArchiveTest, PreparationLeavesLiveStageUnchanged) {
  // Preparing a decoded stage validates resources without installing it.
  libsiglus::SiglusRuntime runtime;
  runtime.stage = std::make_unique<Stage>(1);
  runtime.stage->objects.Get(0, 0)->Param().SetX(10);
  auto bytes = serialization_test::Save(runtime.CaptureCheckpoint(),
                                        serialization_test::Format::Binary);
  runtime.stage->objects.Get(0, 0)->Param().SetX(99);
  libsiglus::SiglusRuntime::CheckpointState restored;
  serialization_test::Load(bytes, restored, serialization_test::Format::Binary);
  restored.Prepare(runtime);
  EXPECT_EQ(runtime.stage->objects.Get(0, 0)->Param().x(), 99);
}

TEST_F(RuntimeCheckpointArchiveTest,
       TruncatedCheckpointLeavesLiveStageUnchanged) {
  // Truncating an actual checkpoint rejects decoding without replacing live
  // roots.
  libsiglus::SiglusRuntime runtime;
  runtime.stage = std::make_unique<Stage>(1);
  runtime.stage->objects.Get(0, 0)->Param().SetX(99);
  auto bytes = serialization_test::Save(runtime.CaptureCheckpoint(),
                                        serialization_test::Format::Binary);
  bytes.resize(bytes.size() / 2);
  libsiglus::SiglusRuntime::CheckpointState restored;
  EXPECT_THROW(serialization_test::Load(bytes, restored,
                                        serialization_test::Format::Binary),
               std::exception);
  EXPECT_EQ(runtime.stage->objects.Get(0, 0)->Param().x(), 99);
}

TEST_F(RuntimeCheckpointArchiveTest, SuccessfulCommitClearsTransientCaptures) {
  // Loading a checkpoint clears every retained capture and its priority.
  serialization_test::ForEachFormat([&](auto format) {
    SiglusRuntime runtime;
    auto image = std::make_shared<SDLSurface>(Size(1, 1));
    runtime.captures = std::make_shared<CaptureManager>(
        CaptureConfig{.save_thumbnail = true, .thumbnail_size = Size(1, 1)},
        std::filesystem::path{},
        [image](const CaptureRequest&) { return image; },
        CaptureManager::Load{});
    for (auto kind : {CaptureKind::Save, CaptureKind::Object,
                      CaptureKind::Tweet, CaptureKind::Buffer})
      runtime.captures->Capture(
          {.kind = kind, .size = Size(1, 1), .priority = 2});
    auto bytes = serialization_test::Save(runtime.CaptureCheckpoint(), format);
    SiglusRuntime::CheckpointState restored;
    serialization_test::Load(bytes, restored, format);
    restored.Prepare(runtime);
    restored.Commit(runtime);
    for (auto kind : {CaptureKind::Save, CaptureKind::Object,
                      CaptureKind::Tweet, CaptureKind::Buffer})
      EXPECT_FALSE(runtime.captures->Snapshot(kind));
    EXPECT_EQ(runtime.captures->save_priority(), 0);
  });
}

TEST_F(RuntimeCheckpointArchiveTest, PreparationPreservesLiveCapture) {
  // Preparing a decoded checkpoint leaves live captures intact until commit.
  serialization_test::ForEachFormat([&](auto format) {
    SiglusRuntime runtime;
    auto image = std::make_shared<SDLSurface>(Size(1, 1));
    runtime.captures = std::make_shared<CaptureManager>(
        CaptureConfig{.save_thumbnail = true, .thumbnail_size = Size(1, 1)},
        std::filesystem::path{},
        [image](const CaptureRequest&) { return image; },
        CaptureManager::Load{});
    runtime.captures->Capture({.kind = CaptureKind::Save, .priority = 2});
    auto captured = runtime.captures->Snapshot(CaptureKind::Save);
    auto bytes = serialization_test::Save(runtime.CaptureCheckpoint(), format);
    SiglusRuntime::CheckpointState restored;
    serialization_test::Load(bytes, restored, format);
    restored.Prepare(runtime);
    EXPECT_EQ(runtime.captures->Snapshot(CaptureKind::Save), captured);
    EXPECT_EQ(runtime.captures->save_priority(), 2);
  });
}

TEST_F(RuntimeCheckpointArchiveTest, MissingAssetPreservesLiveStageAndCapture) {
  // Failed asset reconstruction cannot replace the live stage or clear
  // captures.
  serialization_test::ForEachFormat([&](auto format) {
    SiglusRuntime runtime;
    ImageArchiveHost loading;
    loading.fail = true;
    runtime.stage = std::make_unique<Stage>(1);
    auto image = std::make_shared<SDLSurface>(Size(1, 1));
    auto object = runtime.stage->objects.Get(kLayerFg, 0);
    object->SetDrawer(std::make_unique<GraphicsObjectOfFile>("missing", image));
    object->Param().SetX(17);
    auto bytes = serialization_test::Save(runtime.CaptureCheckpoint(), format);
    object->Param().SetX(99);
    runtime.captures = std::make_shared<CaptureManager>(
        CaptureConfig{.save_thumbnail = true, .thumbnail_size = Size(1, 1)},
        std::filesystem::path{},
        [image](const CaptureRequest&) { return image; },
        CaptureManager::Load{});
    runtime.captures->Capture({.kind = CaptureKind::Save, .priority = 2});
    auto captured = runtime.captures->Snapshot(CaptureKind::Save);
    SiglusRuntime::CheckpointState restored;
    EXPECT_THROW(serialization_test::Load(bytes, restored, format,
                                          {.images = &loading.images}),
                 std::runtime_error);
    EXPECT_EQ(runtime.stage->objects.Get(kLayerFg, 0), object);
    EXPECT_EQ(object->Param().x(), 99);
    EXPECT_EQ(runtime.captures->Snapshot(CaptureKind::Save), captured);
    EXPECT_EQ(runtime.captures->save_priority(), 2);
  });
}

// ------------------------------------------------------------------------------

class RuntimeTextArchiveTest : public ::testing::Test {
 protected:
  void SetUp() override {
    runtime.gameexe = std::make_shared<Gameexe>();
    for (const char* line :
         {"SCREEN_SIZE = 640,480", "MWND.CNT = 1", "MWND.0.MOJI_CNT = 42,4",
          "MWND.0.MOJI_SIZE = 20", "WAKU.CNT = 0", "WAKU.BTN.CNT = 0"})
      runtime.gameexe->parseLine(line);
    runtime.stage = std::make_unique<Stage>();
    runtime.asset_scanner = std::make_shared<AssetScanner>();
    runtime.system = std::make_unique<System>(
        *runtime.gameexe, *runtime.stage, runtime.local_config,
        runtime.global_config, runtime.asset_scanner,
        MwndConfig::ParseSiglus(*runtime.gameexe));
    runtime.system->text().BindGraphics({.request_refresh = [] {}});
  }
  void TearDown() override {
    runtime.system->text().Reset();
    runtime.stage->Reset();
  }
  ScopedDummyDrivers drivers;
  SiglusRuntime runtime;
};

TEST_F(RuntimeTextArchiveTest, PreparationLeavesLiveTextUnchanged) {
  // Runtime preparation stages hidden text without hiding the live system.
  serialization_test::ForEachFormat([&](auto format) {
    auto& text = runtime.system->text();
    text.set_system_visible(false);
    auto bytes = serialization_test::Save(runtime.CaptureCheckpoint(), format);
    text.set_system_visible(true);
    SiglusRuntime::CheckpointState restored;
    serialization_test::Load(bytes, restored, format,
                             MakeSerializationContext(runtime));
    restored.Prepare(runtime);
    EXPECT_TRUE(text.system_visible());
  });
}

TEST_F(RuntimeTextArchiveTest, CommitRestoresSelectedStageWindowAlias) {
  // Text selection and stage windows remain aliased through the single archive.
  serialization_test::ForEachFormat([&](auto format) {
    auto& text = runtime.system->text();
    auto window = text.GetMwnd(0);
    text.SetCurrentMwnd(window);
    auto bytes = serialization_test::Save(runtime.CaptureCheckpoint(), format);
    text.Reset();
    SiglusRuntime::CheckpointState restored;
    serialization_test::Load(bytes, restored, format,
                             MakeSerializationContext(runtime));
    restored.Prepare(runtime);
    restored.Commit(runtime);
    EXPECT_NE(text.GetCurrentMwnd(), window);
    EXPECT_EQ(text.GetCurrentMwnd(), runtime.stage->mwnd[kLayerFg][0]);
  });
}

TEST_F(RuntimeTextArchiveTest, LaterPreparationFailurePreservesLiveText) {
  // Text decodes successfully, but invalid counter ownership rejects the whole
  // candidate without installing its hidden visibility.
  serialization_test::ForEachFormat([&](auto format) {
    auto& text = runtime.system->text();
    text.set_system_visible(false);
    auto state = runtime.CaptureCheckpoint();
    state.owned_counters.emplace_back(std::make_shared<Counter>(), 2);
    auto bytes = serialization_test::Save(state, format);
    text.set_system_visible(true);
    SiglusRuntime::CheckpointState restored;
    serialization_test::Load(bytes, restored, format,
                             MakeSerializationContext(runtime));
    EXPECT_THROW(restored.Prepare(runtime), std::runtime_error);
    EXPECT_TRUE(text.system_visible());
  });
}

TEST_F(RuntimeTextArchiveTest, CommitInstallsTextState) {
  // A successful commit installs the visibility staged by TextSystem::load.
  serialization_test::ForEachFormat([&](auto format) {
    auto& text = runtime.system->text();
    text.set_system_visible(false);
    auto bytes = serialization_test::Save(runtime.CaptureCheckpoint(), format);
    text.set_system_visible(true);
    SiglusRuntime::CheckpointState restored;
    serialization_test::Load(bytes, restored, format,
                             MakeSerializationContext(runtime));
    restored.Prepare(runtime);
    restored.Commit(runtime);
    EXPECT_FALSE(text.system_visible());
  });
}

TEST_F(RuntimeTextArchiveTest, RestoresOuterCommitReceiver) {
  // Runtime text loading temporarily overrides, then restores, the caller's
  // commit receiver without invoking it for staged text.
  serialization_test::ForEachFormat([&](auto format) {
    auto bytes = serialization_test::Save(runtime.CaptureCheckpoint(), format);
    auto context = MakeSerializationContext(runtime);
    int received = 0;
    context.defer_commit = [&](auto) { ++received; };
    SiglusRuntime::CheckpointState restored;
    serialization_test::Read(
        bytes, format,
        [&](auto& ar) {
          ar >> restored;
          EXPECT_EQ(received, 0);
          SerializationContext::Get(ar).defer_commit([] {});
        },
        context);
    EXPECT_EQ(received, 1);
  });
}

TEST_F(RuntimeTextArchiveTest, RestoresOuterCommitReceiverOnFailure) {
  // Invalid text rejects decoding while still restoring the caller's receiver.
  serialization_test::ForEachFormat([&](auto format) {
    runtime.system->text().SetKeyCursor(-2);
    auto bytes = serialization_test::Save(runtime.CaptureCheckpoint(), format);
    auto context = MakeSerializationContext(runtime);
    int received = 0;
    context.defer_commit = [&](auto) { ++received; };
    SiglusRuntime::CheckpointState restored;
    serialization_test::Read(
        bytes, format,
        [&](auto& ar) {
          EXPECT_THROW(ar >> restored, std::runtime_error);
          EXPECT_EQ(received, 0);
          SerializationContext::Get(ar).defer_commit([] {});
        },
        context);
    EXPECT_EQ(received, 1);
  });
}

TEST_F(RuntimeTextArchiveTest, RejectsUnavailableTextSystem) {
  // A text payload cannot be loaded without destination text services.
  serialization_test::ForEachFormat([&](auto format) {
    auto bytes = serialization_test::Save(runtime.CaptureCheckpoint(), format);
    SiglusRuntime::CheckpointState restored;
    EXPECT_THROW(serialization_test::Load(bytes, restored, format),
                 std::runtime_error);
  });
}

TEST_F(RuntimeTextArchiveTest, LaterDecodeFailurePreservesLiveText) {
  // Truncating the payload after text has decoded must not install staged text.
  serialization_test::ForEachFormat([&](auto format) {
    auto& text = runtime.system->text();
    text.set_system_visible(false);
    auto bytes = serialization_test::Save(runtime.CaptureCheckpoint(), format);
    bytes.resize(bytes.size() - 4);
    text.set_system_visible(true);
    SiglusRuntime::CheckpointState restored;
    EXPECT_THROW(serialization_test::Load(bytes, restored, format,
                                          MakeSerializationContext(runtime)),
                 std::exception);
    EXPECT_TRUE(text.system_visible());
  });
}

class RuntimeSoundArchiveTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::ofstream(directory.dir() / "tone.wav", std::ios::binary)
        << ResourceWave();
    host.scanner->IndexDirectory(directory.dir());
    runtime.system = std::move(host.system);
  }
  void TearDown() override { host.system = std::move(runtime.system); }
  HeadlessResourceHost host;
  ScopedTemporary directory;
  SiglusRuntime runtime;
};

TEST_F(RuntimeSoundArchiveTest, PreparationLeavesLiveSoundUnchanged) {
  // Resource decoding prepares sound but keeps the live channel gain until
  // commit.
  serialization_test::ForEachFormat([&](auto format) {
    auto& sound = runtime.system->sound();
    sound.SetChannelVolume(2, 90);
    const auto bytes =
        serialization_test::Save(runtime.CaptureCheckpoint(), format);
    sound.SetChannelVolume(2, 150);
    SiglusRuntime::CheckpointState restored;
    serialization_test::Load(bytes, restored, format,
                             MakeSerializationContext(runtime));
    restored.Prepare(runtime);
    EXPECT_EQ(sound.GetChannelVolume(2), 150);
  });
}

TEST_F(RuntimeSoundArchiveTest, CommitInstallsSoundState) {
  // A successful resource commit installs the prepared channel and its gain.
  serialization_test::ForEachFormat([&](auto format) {
    auto& sound = runtime.system->sound();
    sound.WavPrepare("tone", true, 2);
    sound.SetChannelVolume(2, 90);
    const auto bytes =
        serialization_test::Save(runtime.CaptureCheckpoint(), format);
    sound.WavStop(2);
    sound.SetChannelVolume(2, 150);
    SiglusRuntime::CheckpointState restored;
    serialization_test::Load(bytes, restored, format,
                             MakeSerializationContext(runtime));
    restored.Prepare(runtime);
    restored.Commit(runtime);
    sound.WavResume(2);
    EXPECT_TRUE(sound.WavPlaying(2));
    EXPECT_EQ(sound.GetChannelVolume(2), 90);
    sound.WavStop(2);
  });
}

TEST_F(RuntimeSoundArchiveTest, LaterPreparationFailurePreservesSound) {
  // A later invalid counter owner rejects the candidate without installing
  // sound.
  serialization_test::ForEachFormat([&](auto format) {
    auto& sound = runtime.system->sound();
    sound.SetChannelVolume(2, 90);
    auto state = runtime.CaptureCheckpoint();
    state.owned_counters.emplace_back(std::make_shared<Counter>(), 2);
    const auto bytes = serialization_test::Save(state, format);
    sound.SetChannelVolume(2, 150);
    SiglusRuntime::CheckpointState restored;
    serialization_test::Load(bytes, restored, format,
                             MakeSerializationContext(runtime));
    EXPECT_THROW(restored.Prepare(runtime), std::runtime_error);
    EXPECT_EQ(sound.GetChannelVolume(2), 150);
  });
}

TEST_F(RuntimeSoundArchiveTest, LaterDecodeFailurePreservesSound) {
  // Failure after the sound payload leaves the staged gain uninstalled.
  serialization_test::ForEachFormat([&](auto format) {
    auto& sound = runtime.system->sound();
    sound.SetChannelVolume(2, 90);
    auto bytes = serialization_test::Write(format, [&](auto& ar) {
      ar << runtime.CaptureCheckpoint() << std::string(32, 'x') << 42;
    });
    bytes.resize(bytes.size() - 16);
    sound.SetChannelVolume(2, 150);
    SiglusRuntime::CheckpointState restored;
    EXPECT_THROW(serialization_test::Read(
                     bytes, format,
                     [&](auto& ar) {
                       std::string tail;
                       int marker;
                       ar >> restored >> tail >> marker;
                     },
                     MakeSerializationContext(runtime)),
                 std::exception);
    EXPECT_EQ(sound.GetChannelVolume(2), 150);
  });
}

TEST_F(RuntimeSoundArchiveTest, SoundFailureRestoresOuterCommitReceiver) {
  // Missing saved audio restores the outer receiver and leaves staged text
  // inert.
  serialization_test::ForEachFormat([&](auto format) {
    runtime.system->text().set_system_visible(false);
    runtime.system->sound().WavPrepare("missing", true, 2);
    const auto bytes =
        serialization_test::Save(runtime.CaptureCheckpoint(), format);
    runtime.system->text().set_system_visible(true);
    auto context = MakeSerializationContext(runtime);
    int received = 0;
    context.defer_commit = [&](auto) { ++received; };
    SiglusRuntime::CheckpointState restored;
    serialization_test::Read(
        bytes, format,
        [&](auto& ar) {
          EXPECT_THROW(ar >> restored, std::exception);
          EXPECT_EQ(received, 0);
          SerializationContext::Get(ar).defer_commit([] {});
        },
        context);
    EXPECT_EQ(received, 1);
    EXPECT_TRUE(runtime.system->text().system_visible());
  });
}

TEST_F(RuntimeSoundArchiveTest, RejectsUnavailableSoundSystem) {
  // A standalone sound payload requires a destination owner for reconstruction.
  serialization_test::ForEachFormat([&](auto format) {
    auto state = runtime.CaptureCheckpoint();
    state.text = nullptr;
    const auto bytes = serialization_test::Save(state, format);
    SiglusRuntime::CheckpointState restored;
    EXPECT_THROW(serialization_test::Load(bytes, restored, format),
                 std::runtime_error);
  });
}

}  // namespace
