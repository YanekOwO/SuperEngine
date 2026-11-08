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

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include "core/excall.hpp"
#include "core/frame_action_manager.hpp"
#include "headless_resource_host.hpp"
#include "libsiglus/bindings/loader.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/intern_name.hpp"
#include "libsiglus/siglus_scene_renderer.hpp"
#include "long_operations/siglus_wait.hpp"
#include "m6/compiler_pipeline.hpp"
#include "m6/vm_factory.hpp"
#include "mock_clock.hpp"
#include "mock_graphics_object_data.hpp"
#include "poller_helpers.hpp"
#include "siglus/fake_scene.hpp"
#include "srbind/srbind.hpp"
#include "systems/event_system.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"
#include "vm/function.hpp"
#include "vm/future.hpp"
#include "vm/list.hpp"
#include "vm/promise.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace libsiglus::binding {
namespace sr = serilang;
using namespace std::chrono_literals;

class SiglusExcallTest : public ::testing::Test {
 protected:
  ScopedDummyDrivers hints;
  SiglusRuntime runtime;
  MockClock* clock = nullptr;
  std::map<int, std::string> menus;
  std::map<std::string, std::shared_ptr<sr::Promise>> gates;
  std::function<void(int)> observe = [](int) {};
  std::function<void(SiglusMachine&)> configure;

  void SetUp() override {
    runtime.gameexe = std::make_shared<Gameexe>();
    for (const auto* setting :
         {"SCREEN_SIZE = 640,480", "MWND.CNT = 1", "WAKU.CNT = 1",
          "WAKU.BTN.CNT = 0", "OBJECT.CNT = 4", "FLAG.CNT = 3",
          "COUNTER.CNT = 2", "FRAME_ACTION_CH.CNT = 2", "EFFECT.CNT = 2"})
      runtime.gameexe->parseLine(setting);
    runtime.stage = std::make_unique<Stage>(4, 2, 2, 2);
    runtime.asset_scanner = std::make_shared<AssetScanner>();
    runtime.system = std::make_unique<System>(
        *runtime.gameexe, *runtime.stage, runtime.local_config,
        runtime.global_config, runtime.asset_scanner,
        MwndConfig::ParseSiglus(*runtime.gameexe));
    runtime.GameplayMachine().vm =
        std::make_unique<sr::VM>(m6::VMFactory::Create());
    auto time = std::make_unique<MockClock>(Clock::timepoint_t{});
    clock = time.get();
    runtime.GameplayMachine().vm->scheduler_ =
        sr::Scheduler(std::make_unique<sr::RecordingPoller>(), std::move(time));
    runtime.archive = std::make_shared<Archive>(
        std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
    runtime.GameplayMachine().loader = std::make_unique<Loader>(
        *runtime.archive, *runtime.GameplayMachine().vm, false);
    runtime.counter_list = std::make_shared<CounterList>(2);
    runtime.frame_action_manager = std::make_shared<FrameActionManager>(
        *runtime.stage, runtime.local_config, 2);
    runtime.GameplayMachine().frame_requests =
        std::make_unique<FrameRequestManager>(
            [] { return std::make_shared<sr::Promise>(); });
    runtime.binding_names = {"0_memory",       "flow",     "1_counter",
                             "1_frame_action", "1_object", "1_mwnd",
                             "1_group",        "0_effect", "0_quake",
                             "2_stage",        "3_excall"};
    for (const auto& name : runtime.binding_names) {
      const auto* binding = SiglusBindingRegistry::Find(name);
      ASSERT_NE(binding, nullptr) << name;
      (*binding)(runtime.GameplayMachine());
    }
    runtime.configure_machine = [this](SiglusMachine& machine) {
      srbind::module_ bindings(machine.vm->gc_.get(),
                               machine.vm->globals_.get());
      bindings.def("observe", [this](int value) { observe(value); });
      for (const auto& [name, promise] : gates) {
        auto* future = machine.vm->gc_->Allocate<sr::Future>();
        future->promise = promise;
        (*machine.vm->globals_)[name] = sr::Value(future);
      }
      for (const auto& [id, body] : menus) {
        const auto name = "menu_" + std::to_string(id);
        Execute(*machine.vm, "fn " + name + "() { " + body + " }");
        (*machine.loader->Load(id)->globals)[GetUsercmdId(5)] =
            machine.vm->globals_->at(name);
      }
      if (configure)
        configure(machine);
    };
    runtime.system->text().SetExcallStage(
        &runtime.excall->stage, [&] { return runtime.excall->allocated; },
        [&] { return runtime.IsExcall(); });
    runtime.system->text().SetFontConfigSource([&]() -> const LocalConfig& {
      return runtime.IsExcall() ? runtime.excall->config : runtime.local_config;
    });
  }

  void ExpectSizes(const Excall::AllocationSizes& sizes) {
    const auto& excall = *runtime.excall;
    EXPECT_EQ(excall.flags.size(), sizes.flag_count);
    EXPECT_EQ(excall.counters->elements_.size(), sizes.counter_count);
    EXPECT_EQ(excall.actions->global_channels().size(),
              sizes.frame_action_count);
    for (int layer : {kLayerFg, kLayerBg, kLayerNext}) {
      EXPECT_EQ(excall.stage.objects.Size(layer), sizes.object_count);
      EXPECT_EQ(excall.stage.effects.Size(layer), sizes.effect_count);
      EXPECT_EQ(excall.stage.groups.Size(layer), sizes.group_count);
      EXPECT_EQ(excall.stage.quakes.Size(layer), sizes.quake_count);
      EXPECT_EQ(excall.stage.mwnd.Size(layer), sizes.window_count);
    }
  }

  void Run(const std::string& source) {
    m6::CompilerPipeline compiler(runtime.GameplayMachine().vm->gc_);
    compiler.compile(m6::SourceBuffer::Create(source, "<excall>"));
    if (!compiler.Ok())
      throw std::runtime_error(compiler.FormatErrors());
    runtime.GameplayMachine().vm->Evaluate(compiler.Get());
  }
  CmdTarget Menu(const std::string& name, std::string body) {
    const int id = FakeScene{.scene_name = name, .command_name = name}.AddTo(
        *runtime.archive);
    menus.emplace(id, std::move(body));
    return {id, 5};
  }
  void DriveScripts() {
    for (int iteration = 0; iteration < 4; ++iteration)
      runtime.DriveScripts();
  }
  sr::Value Global(const char* name) {
    return runtime.GameplayMachine().vm->globals_->at(name);
  }
};

TEST_F(SiglusExcallTest, AllocationIsIndependentOfExecution) {
  // Allocating storage makes it ready without entering an EX call.
  Run("before = excall.check_alloc(); excall.alloc(); ready = "
      "excall[1].check_alloc(); active = excall.is_excall();");
  EXPECT_EQ(Global("before"), sr::Value(0));
  EXPECT_EQ(Global("ready"), sr::Value(1));
  EXPECT_EQ(Global("active"), sr::Value(0));
}

TEST_F(SiglusExcallTest, AllocationUsesConfiguredCounts) {
  // Distinct configured counts must reach their corresponding EX resources.
  for (const auto& [key, count] : {std::pair{"FLAG.CNT", 2},
                                   {"COUNTER.CNT", 3},
                                   {"FRAME_ACTION_CH.CNT", 4},
                                   {"OBJECT.CNT", 5},
                                   {"EFFECT.CNT", 6},
                                   {"BUTTON.GROUP.CNT", 7},
                                   {"QUAKE.CNT", 8},
                                   {"MWND.CNT", 9}})
    runtime.gameexe->SetIntAt(key, count);
  Run("excall.alloc();");
  ExpectSizes({.flag_count = 2,
               .counter_count = 3,
               .frame_action_count = 4,
               .object_count = 5,
               .effect_count = 6,
               .group_count = 7,
               .quake_count = 8,
               .window_count = 9});
}

TEST_F(SiglusExcallTest, MissingSettingsUseDefaultCounts) {
  // An empty configuration supplies the legacy default allocation sizes.
  const auto previous = runtime.gameexe;
  runtime.gameexe = std::make_shared<Gameexe>();
  Run("excall.alloc();");
  ExpectSizes({.flag_count = 1000,
               .counter_count = 16,
               .frame_action_count = 4,
               .object_count = 256,
               .effect_count = 0,
               .group_count = 16,
               .quake_count = 16,
               .window_count = 1});
  runtime.gameexe = previous;
}

TEST_F(SiglusExcallTest, MissingGameexeUsesDefaultCounts) {
  // Keep the system's config alive while testing allocation without Gameexe.
  const auto previous = std::move(runtime.gameexe);
  Run("excall.alloc();");
  ExpectSizes({.flag_count = 1000,
               .counter_count = 16,
               .frame_action_count = 4,
               .object_count = 256,
               .effect_count = 0,
               .group_count = 16,
               .quake_count = 16,
               .window_count = 1});
  runtime.gameexe = previous;
}

TEST_F(SiglusExcallTest, NegativeCountsAllocateEmptyResources) {
  // Negative configuration counts clamp to zero for every EX resource.
  for (const auto* key :
       {"FLAG.CNT", "COUNTER.CNT", "FRAME_ACTION_CH.CNT", "OBJECT.CNT",
        "EFFECT.CNT", "BUTTON.GROUP.CNT", "QUAKE.CNT", "MWND.CNT"})
    runtime.gameexe->SetIntAt(key, -1);
  Run("excall.alloc();");
  ExpectSizes({});
}

TEST_F(SiglusExcallTest, FrameActionChannelsHaveAnUpperLimit) {
  // Counts above the legacy limit allocate exactly 256 EX action channels.
  runtime.gameexe->SetIntAt("FRAME_ACTION_CH.CNT", 300);
  Run("excall.alloc();");
  EXPECT_EQ(runtime.excall->actions->global_channels().size(), 256);
}

TEST_F(SiglusExcallTest, ReallocationReadsUpdatedConfiguration) {
  // Config changes affect a new allocation after free, preserving active data.
  Run("excall.alloc(); excall.F[1] = 42;");
  runtime.gameexe->SetIntAt("FLAG.CNT", 1);
  Run("excall.alloc();");
  ASSERT_EQ(runtime.excall->flags.size(), 3);
  EXPECT_EQ(runtime.excall->flags[1], 42);
  Run("excall.free(); excall.alloc();");
  EXPECT_EQ(runtime.excall->flags.size(), 1);
}

TEST_F(SiglusExcallTest, UnallocatedChildrenRejectAccess) {
  // Child storage is unavailable before alloc, while status remains readable.
  EXPECT_THROW(Run("value = excall.F[0];"), sr::UnhandledError);
}

TEST_F(SiglusExcallTest, InvalidOwnerIsRejected) {
  // Only normal and EX indices are valid; an invalid owner must fail.
  EXPECT_THROW(Run("value = excall[2];"), sr::UnhandledError);
}

TEST_F(SiglusExcallTest, NormalFlagsAliasTheExistingBank) {
  // Index zero names the ordinary bank; the EX bank retains its own value.
  Run("excall.alloc(); F[0] = 17; excall.F[0] = 29; normal = excall[0].F[0]; "
      "extra = excall[1].F[0];");
  EXPECT_EQ(Global("normal"), sr::Value(17));
  EXPECT_EQ(Global("extra"), sr::Value(29));
}

TEST_F(SiglusExcallTest, RetainedFlagBitViewSurvivesGarbageCollection) {
  // A stored bit view keeps its EX flag accessor alive across collection.
  Run("excall.alloc(); bits = excall.F.b1;");
  runtime.GameplayMachine().vm->CollectGarbage();
  Run("bits[1] = 1; value = excall.F[0];");
  EXPECT_EQ(Global("value"), sr::Value(2));
}

TEST_F(SiglusExcallTest, RetainedFlagViewFollowsReallocation) {
  // A retained EX view rejects freed storage and follows the next allocation.
  Run("excall.alloc(); flags = excall.F; excall.free(); caught = 0; "
      "try { flags[0]; } catch (error) { caught = 1; } "
      "excall.alloc(); flags[0] = 31; value = excall.F[0];");
  EXPECT_EQ(Global("caught"), sr::Value(1));
  EXPECT_EQ(Global("value"), sr::Value(31));
}

TEST_F(SiglusExcallTest, ObjectBitViewKeepsItsDetachedObjectAlive) {
  // A borrowed view owns the object even after its stage and wrapper disappear.
  Run("excall.alloc(); object = excall.front.object[0]; "
      "bits = object.F.b1; object = nil; excall.free();");
  runtime.GameplayMachine().vm->CollectGarbage();
  Run("bits[2] = 1; value = bits[2];");
  EXPECT_EQ(Global("value"), sr::Value(1));
}

TEST_F(SiglusExcallTest, RepeatedAllocationPreservesResources) {
  // Repeated alloc retains the same storage and its contents.
  Run("excall.alloc(); excall.F[1] = 42; excall.alloc(); value = excall.F[1];");
  EXPECT_EQ(Global("value"), sr::Value(42));
}

TEST_F(SiglusExcallTest, FreeReleasesOnlyExcallResources) {
  // Free removes EX objects and flags without changing ordinary objects.
  Run("excall.alloc(); stage.front.object[0].x = 17; excall.front.object[0].x "
      "= 29; excall.free(); normal = stage.front.object[0].x; ready = "
      "excall.check_alloc();");
  EXPECT_EQ(Global("normal"), sr::Value(17));
  EXPECT_EQ(Global("ready"), sr::Value(0));
  EXPECT_EQ(runtime.excall->stage.objects.Size(kLayerFg), 0);
}

TEST_F(SiglusExcallTest, StageAliasesAddressTheSameExcallObject) {
  // The named front layer and stage index one resolve the same EX object.
  Run("excall.alloc(); excall.front.object[1].x = 23; value = "
      "excall.stage[1].object[1].x;");
  EXPECT_EQ(Global("value"), sr::Value(23));
  EXPECT_FALSE(runtime.stage->objects.Exists(kLayerFg, 1));
}

TEST_F(SiglusExcallTest, ChildEventsAdvanceWhileOrdinaryTimeStops) {
  // A child animation in EX storage continues through ordinary time_stop.
  Run("excall.alloc(); excall.front.object[0].child.resize(1); "
      "excall.front.object[0].child[0].x_eve.set(100, 100, 0, 0);");
  runtime.local_config.time_stop = true;
  runtime.Update(50ms, 50ms);
  runtime.excall->stage.Execute();
  EXPECT_EQ(runtime.excall->stage.objects.Get(kLayerFg, 0)
                ->GetChildren()[0]
                ->Param()
                .x(),
            50);
}

TEST_F(SiglusExcallTest, CounterTimeStopDoesNotStopExcallCounters) {
  // EX counters advance independently of the ordinary counter stop flag.
  Run("excall.alloc(); counter[0].start(); excall.counter[0].start();");
  runtime.local_config.counter_time_stop = true;
  runtime.Update(17ms, 31ms);
  EXPECT_EQ(runtime.counter_list->GetHandle(0)->Get(), 0);
  EXPECT_EQ(runtime.excall->counters->GetHandle(0)->Get(), 31);
}

TEST_F(SiglusExcallTest, FontOverridesDoNotChangeOrdinarySettings) {
  // EX font methods update only their own overrides and support defaults.
  runtime.local_config.font_name = "ordinary";
  Run("excall.alloc(); excall.script.set_font_name(\"menu\"); "
      "excall.script.set_font_bold(1); excall.script.set_font_shadow(3); name "
      "= excall.script.get_font_name(); bold = excall.script.get_font_bold(); "
      "shadow = excall.script.get_font_shadow();");
  EXPECT_EQ(Global("name").Str(), "menu");
  EXPECT_EQ(Global("bold"), sr::Value(1));
  EXPECT_EQ(Global("shadow"), sr::Value(3));
  EXPECT_EQ(runtime.local_config.font_name, "ordinary");
}

TEST_F(SiglusExcallTest, FreeRetainsNumericFontOverridesUntilLocalReset) {
  // Legacy free clears the name; numeric font overrides survive reallocation.
  Run("excall.alloc(); excall.script.set_font_name(\"menu\"); "
      "excall.script.set_font_bold(1); excall.free(); excall.alloc();");
  EXPECT_TRUE(runtime.excall->config.font_name.empty());
  EXPECT_EQ(runtime.excall->config.font_futoku, 1);
  runtime.ResetExcall();
  EXPECT_EQ(runtime.excall->config.font_futoku, -1);
}

TEST_F(SiglusExcallTest, ExplicitFreeFinishesActionsOnce) {
  // An active EX action receives its final callback once before release.
  Run("excall.alloc();");
  int finishes = 0;
  runtime.excall->actions->SetExec([&](const ActionReference& ref, auto) {
    if (ref.action->end_action)
      ++finishes;
  });
  runtime.excall->actions->Start({runtime.excall->actions->global_action(), {}},
                                 100, false, ScriptCall{.target = CmdTarget{}});
  Run("excall.free(); excall.free();");
  EXPECT_EQ(finishes, 1);
}

TEST_F(SiglusExcallTest, MenuInputCannotCompleteAnUnderlyingPollingWait) {
  // A gameplay wait ignores input routed to an independently executing menu.
  auto value = lop::MakePollingWaitFuture(
      *runtime.GameplayMachine().vm, [] { return false; }, true,
      &runtime.system->event(), 5ms, {}, runtime.GameplayMachine().WaitScope());
  auto promise = value.Get<sr::Future*>()->promise;
  gates["gate"] = std::make_shared<sr::Promise>();
  ASSERT_TRUE(runtime.OpenExcall(Menu("menu", "await gate;")));
  DriveScripts();
  SDL_Event event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.scancode = SDL_SCANCODE_SPACE;
  event.key.key = SDLK_SPACE;
  ASSERT_TRUE(SDL_PushEvent(&event));
  runtime.system->event().ExecuteEventSystem();
  EXPECT_FALSE(promise->HasResult());
  gates["gate"]->Resolve(sr::nil);
  DriveScripts();
}

TEST_F(SiglusExcallTest, FreeCancelsPendingExcallPollingWork) {
  // Releasing EX storage cancels its native polling tasks on the next poll.
  Run("excall.alloc();");
  gates["gate"] = std::make_shared<sr::Promise>();
  ASSERT_TRUE(runtime.OpenExcall(Menu("menu", "await gate;")));
  DriveScripts();
  auto& machine = runtime.ActiveMachine();
  auto time = std::make_unique<MockClock>(Clock::timepoint_t{});
  auto* menu_clock = time.get();
  machine.vm->scheduler_ =
      sr::Scheduler(std::make_unique<sr::RecordingPoller>(), std::move(time));
  auto value = lop::MakePollingWaitFuture(
      *machine.vm, [] { return false; }, false, nullptr, 5ms, {},
      machine.WaitScope(1));
  auto promise = value.Get<sr::Future*>()->promise;
  runtime.FreeExcall();
  menu_clock->AdvanceTime(5ms);
  machine.vm->RunReadyUntilBlocked();
  ASSERT_TRUE(promise->HasResult());
  EXPECT_FALSE(promise->result->has_value());
  gates["gate"]->Resolve(sr::nil);
  DriveScripts();
}

TEST_F(SiglusExcallTest, FreeCancelsPollingOnExcallFrameActionCounter) {
  // Poll an EX-owned counter from gameplay; freeing EX storage rejects its
  // wait.
  Run("excall.alloc(); pending = excall.frame_action.counter.wait(1);");
  auto promise = runtime.GameplayMachine()
                     .vm->globals_->at("pending")
                     .Get<sr::Future*>()
                     ->promise;
  runtime.FreeExcall();
  clock->AdvanceTime(5ms);
  runtime.GameplayMachine().vm->RunReadyUntilBlocked();
  ASSERT_TRUE(promise->HasResult());
  EXPECT_FALSE(promise->result->has_value());
}

TEST_F(SiglusExcallTest, MessageWindowReferenceRetainsItsStageOwner) {
  // Selecting an EX window reference keeps it distinct from the normal window.
  Run("excall.alloc(); set_mwnd(0, [excall.front.mwnd[0]], {});");
  auto extra = runtime.excall->stage.mwnd[kLayerFg][0];
  EXPECT_EQ(runtime.system->text().GetCurrentMwnd(), extra);
  EXPECT_FALSE(runtime.stage->mwnd.Exists(kLayerFg, 0));
}

TEST_F(SiglusExcallTest,
       FailedMenuReportsItsErrorAfterRestoringExecutionScope) {
  // A failed child releases EX scope and reports its unhandled script error.
  ASSERT_TRUE(runtime.OpenExcall(Menu("menu", "throw \"menu failure\";")));
  EXPECT_THROW(DriveScripts(), sr::UnhandledError);
  EXPECT_FALSE(runtime.IsExcall());
}

TEST_F(SiglusExcallTest, ExternalCallParksReadyGameplayAndResumesItOnce) {
  // A child menu leaves a gameplay completion queued until gameplay resumes.
  Run("gameplay_gate = nil; done = [0]; fn gameplay() { await "
      "gameplay_gate; done[0] += 1; }");
  auto* gameplay_gate =
      runtime.GameplayMachine().vm->gc_->Allocate<sr::Future>();
  (*runtime.GameplayMachine().vm->globals_)["gameplay_gate"] =
      sr::Value(gameplay_gate);
  gates["menu_gate"] = std::make_shared<sr::Promise>();
  runtime.GameplayMachine().vm->ScheduleExecute(Global("gameplay"));
  runtime.GameplayMachine().vm->RunReadyUntilBlocked();
  ASSERT_TRUE(runtime.OpenExcall(Menu("menu", "await menu_gate;")));
  DriveScripts();
  gameplay_gate->promise->Resolve(sr::nil);
  DriveScripts();
  EXPECT_EQ(Global("done").Get<sr::List*>()->items[0], sr::Value(0));
  gates["menu_gate"]->Resolve(sr::nil);
  DriveScripts();
  EXPECT_EQ(Global("done").Get<sr::List*>()->items[0], sr::Value(1));
  EXPECT_FALSE(runtime.IsExcall());
}

TEST_F(SiglusExcallTest, ExcallRenderOffsetParticipatesInCombinedOrdering) {
  // An EX object sorts between ordinary objects using the configured offset.
  Run("excall.alloc(); stage.front.object[0].order = 30; "
      "stage.front.object[1].order = 10; excall.front.object[0].order = 0;");
  auto high = runtime.stage->objects.Get(kLayerFg, 0);
  auto low = runtime.stage->objects.Get(kLayerFg, 1);
  auto extra = runtime.excall->stage.objects.Get(kLayerFg, 0);
  std::vector<const GraphicsObject*> rendered;
  for (const auto& object : {high, low, extra}) {
    object->Param().SetVisible(1);
    auto drawer = std::make_unique<MockGraphicsObjectData>();
    drawer->SetMaskRenderCallback([&](const GraphicsObject& object, auto,
                                      auto) { rendered.push_back(&object); });
    object->SetDrawer(std::move(drawer));
  }
  SiglusSceneRenderer renderer(*runtime.stage, *runtime.system, {}, {},
                               runtime.local_config, Size(640, 480),
                               [] { return false; });
  renderer.SetExcall(runtime.excall.get(), 20);
  renderer.RenderScene();
  EXPECT_EQ(rendered, (std::vector<const GraphicsObject*>{
                          low.get(), extra.get(), high.get()}));
}

TEST_F(SiglusExcallTest, CaptureBoundsUseTheOffsetExcallSorter) {
  // A capture ending below EX's effective order excludes its object.
  Run("excall.alloc(); excall.front.object[0].order = 0;");
  auto object = runtime.excall->stage.objects.Get(kLayerFg, 0);
  object->Param().SetVisible(1);
  int rendered = 0;
  auto drawer = std::make_unique<MockGraphicsObjectData>();
  drawer->SetMaskRenderCallback(
      [&](const GraphicsObject&, auto, auto) { ++rendered; });
  object->SetDrawer(std::move(drawer));
  SiglusSceneRenderer renderer(*runtime.stage, *runtime.system, {}, {},
                               runtime.local_config, Size(640, 480),
                               [] { return false; });
  renderer.SetExcall(runtime.excall.get(), 20);
  renderer.RenderCapture({19, 0});
  EXPECT_EQ(rendered, 0);
}

TEST_F(SiglusExcallTest, AllocatedExcallOwnsWipesWithoutChangingNormalLayers) {
  // Wipe promotes EX back to front while ordinary back remains untouched.
  (*SiglusBindingRegistry::Find("wipe"))(runtime.GameplayMachine());
  runtime.global_config.system.disable_wipe_anime = true;
  Run("excall.alloc(); stage.back.object[0].x = 3; excall.back.object[0].x = "
      "7; excall.back.object[0].child.resize(1); await wipe.wipe_all(0, 0);");
  EXPECT_EQ(runtime.excall->stage.objects.Get(kLayerFg, 0)->Param().x(), 7);
  EXPECT_FALSE(runtime.stage->objects.Exists(kLayerFg, 0));
  EXPECT_EQ(runtime.stage->objects.Get(kLayerBg, 0)->Param().x(), 3);
}

TEST_F(SiglusExcallTest, OrdinaryStopFlagsDoNotStopExcallActions) {
  // EX frame actions retain their own clock while all normal clocks stop.
  Run("excall.alloc();");
  auto action = runtime.excall->actions->global_action();
  runtime.excall->actions->SetExec([](const ActionReference&, auto) {});
  runtime.excall->actions->Start({action, {}}, 100, false,
                                 ScriptCall{.target = CmdTarget{}});
  runtime.local_config.time_stop = true;
  runtime.local_config.frame_action_time_stop = true;
  runtime.local_config.stage_time_stop = true;
  runtime.Update(17ms, 31ms);
  EXPECT_EQ(action->GetCounter()->Get(), 31);
}

TEST_F(SiglusExcallTest, LocalResetCancelsActionsWithoutTheirFinalCallback) {
  // Local reset cancels rather than finishing EX script callbacks.
  Run("excall.alloc();");
  int callbacks = 0;
  runtime.excall->actions->SetExec(
      [&](const ActionReference&, auto) { ++callbacks; });
  runtime.excall->actions->Start({runtime.excall->actions->global_action(), {}},
                                 100, false, ScriptCall{.target = CmdTarget{}});
  runtime.ResetExcall();
  runtime.Update(100ms, 100ms);
  EXPECT_EQ(callbacks, 0);
}

TEST_F(SiglusExcallTest, NestedEngineCallbackReturnsToTheOuterExcall) {
  // A nested child returns to its paused EX parent before returning to
  // gameplay.
  std::vector<int> calls;
  observe = [&](int value) { calls.push_back(value); };
  gates["gate"] = std::make_shared<sr::Promise>();
  auto menu = Menu("menu", "observe(1); await gate; observe(3);");
  auto callback = Menu("callback", "observe(2);");
  ASSERT_TRUE(runtime.OpenExcall(menu));
  DriveScripts();
  ASSERT_TRUE(runtime.OpenExcall(callback));
  DriveScripts();
  EXPECT_TRUE(runtime.IsExcall());
  gates["gate"]->Resolve(sr::nil);
  DriveScripts();
  EXPECT_FALSE(runtime.IsExcall());
  EXPECT_EQ(calls, (std::vector<int>{1, 2, 3}));
}

TEST_F(SiglusExcallTest, FreeCancelsSuspendedFrameActionCallbacks) {
  // An EX action suspends in script; free cancels both regular and final work.
  FakeScene scene{.scene_name = "animation",
                  .command_name = "animate",
                  .args = {Type::FrameAction}};
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
  const int id = scene.AddTo(*runtime.archive);
  Run("excall.alloc();");
  int calls = 0;
  observe = [&](int) { ++calls; };
  gates["gate"] = std::make_shared<sr::Promise>();
  configure = [](SiglusMachine& machine) {
    Execute(*machine.vm,
            "fn probe(n) { await gate; observe(1); return \"done\"; }");
    srbind::module_ math(*machine.vm, "math");
    (*math.dict())["tostr"] = machine.vm->globals_->at("probe");
  };
  runtime.excall->actions->Start({runtime.excall->actions->global_action(), {}},
                                 1000, false,
                                 ScriptCall{.target = CmdTarget{id, 5}});
  runtime.Update(1ms, 1ms);
  runtime.FreeExcall();
  runtime.GameplayMachine().vm->RunReadyUntilBlocked();
  runtime.GameplayMachine().vm->CollectGarbage();
  gates["gate"]->Resolve(sr::nil);
  DriveScripts();
  EXPECT_EQ(calls, 0);
}

}  // namespace libsiglus::binding
