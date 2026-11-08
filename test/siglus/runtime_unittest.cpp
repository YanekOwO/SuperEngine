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

#include "core/capture.hpp"
#include "core/frame_action_manager.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/intern_name.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "m6/compiler_pipeline.hpp"
#include "m6/vm_factory.hpp"
#include "siglus/fake_scene.hpp"
#include "srbind/srbind.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "vm/future.hpp"
#include "vm/promise.hpp"
#include "vm/vm.hpp"

#include <chrono>
#include <filesystem>
#include <vector>

namespace siglus_test {

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
  FrameAction::Target menu_target{0, 5};
  FrameAction::Target callback_target{1, 5};
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
      0, {runtime.frame_action_manager->global_action(), {}}, callback_target);
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
      0, {runtime.frame_action_manager->global_action(), {}}, callback_target);
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

}  // namespace siglus_test
