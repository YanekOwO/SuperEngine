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
#include "m6/compiler_pipeline.hpp"
#include "srbind/srbind.hpp"
#include "vm/exception.hpp"
#include "vm/function.hpp"
#include "vm/gc.hpp"
#include "vm/value.hpp"

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

namespace libsiglus::binding {
namespace sr = serilang;

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

TEST_F(SiglusSceneEntryTest, NamedCommandResolvesThroughCachedMetadata) {
  auto* expected = module->globals->at(GetUsercmdId(5)).Get_if<sr::Function>();
  ASSERT_NE(expected, nullptr);
  EXPECT_EQ(ResolveSceneCommand(loader, "MENU", "open"), expected);
}

TEST_F(SiglusSceneEntryTest, IncludedCommandLoadsDeclaringSceneOnDemand) {
  AddCallbackScene({Type::FrameAction, Type::Object});
  EXPECT_FALSE(vm.builtins_->contains("%%siglus_module@SCENE_0001"));

  const auto& resolved = loader.ResolveUserCommand(*module, "AnImAtE");

  EXPECT_TRUE(vm.builtins_->contains("%%siglus_module@SCENE_0001"));
  const auto* owner = loader.Load(1);
  EXPECT_EQ(resolved.function,
            owner->globals->at(GetUsercmdId(5)).Get_if<sr::Function>());
  EXPECT_EQ(resolved.function->globals, owner->globals);
  EXPECT_EQ(resolved.args,
            (std::vector<Type>{Type::FrameAction, Type::Object}));
  EXPECT_EQ(&loader.ResolveUserCommand(*module, "animate"), &resolved);
}

TEST_F(SiglusSceneEntryTest, IncludedCommandUsesAlreadyLoadedDeclaringScene) {
  AddCallbackScene({Type::FrameAction, Type::Object});
  const auto* owner = loader.Load(1);
  const auto* expected =
      owner->globals->at(GetUsercmdId(5)).Get_if<sr::Function>();

  EXPECT_EQ(ResolveSceneCommand(loader, "menu", "animate"), expected);
  EXPECT_EQ(loader.Load(1), owner);
}

TEST_F(SiglusSceneEntryTest, IncludedCommandTakesPrecedenceOverLocalCommand) {
  AddCommandScene("shared", "open", {Type::Int}, true);
  const auto& resolved = loader.ResolveUserCommand(*module, "OPEN");

  EXPECT_EQ(resolved.function->globals, loader.Load(1)->globals);
  EXPECT_EQ(resolved.args, std::vector<Type>{Type::Int});
  EXPECT_NE(resolved.function,
            module->globals->at(GetUsercmdId(5)).Get_if<sr::Function>());
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
  const auto* expected =
      owner->globals->at(GetUsercmdId(5)).Get_if<sr::Function>();

  EXPECT_EQ(loader.ResolveUserCommand(*owner, "LOCAL").function, expected);
  EXPECT_EQ(ResolveSceneCommand(loader, "menu", "OpEn"),
            module->globals->at(GetUsercmdId(5)).Get_if<sr::Function>());
  EXPECT_THROW(loader.ResolveUserCommand(*module, "local"), sr::RuntimeError);
}

TEST_F(SiglusSceneEntryTest, FiberResolvesIncludedCommandFromNearestScene) {
  AddCallbackScene({Type::FrameAction, Type::Object});
  sr::Fiber fiber;
  fiber.frames.push_back({});  // Native/non-scene frames are ignored.
  fiber.frames.push_back(
      {.fn = module->globals->at("%%script").Get_if<sr::Function>()});
  fiber.frames.push_back({});

  const auto& resolved = loader.ResolveUserCommand(fiber, "ANIMATE");
  EXPECT_EQ(resolved.function->globals, loader.Load(1)->globals);
}

TEST_F(SiglusSceneEntryTest, FiberDoesNotSearchOlderScenesForLocalCommands) {
  const int other = AddCommandScene("other", "local");
  const auto* current = loader.Load(other);
  sr::Fiber fiber;
  fiber.frames.push_back(
      {.fn = module->globals->at("%%script").Get_if<sr::Function>()});
  fiber.frames.push_back(
      {.fn = current->globals->at("%%script").Get_if<sr::Function>()});

  EXPECT_EQ(loader.ResolveUserCommand(fiber, "LOCAL").function->globals,
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
    const auto* bind_counter = SiglusBindingRegistry::Find("1_counter");
    const auto* bind_actions = SiglusBindingRegistry::Find("1_frame_action");
    ASSERT_NE(bind_counter, nullptr);
    ASSERT_NE(bind_actions, nullptr);
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
    EXPECT_EQ(action->function->globals, runtime.loader->Load(1)->globals);
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
  ASSERT_NE(module->globals->at("%%script").Get_if<sr::Function>(), nullptr);
  EXPECT_THROW(ResolveSceneCommand(loader, "menu", "missing"),
               sr::RuntimeError);
}

TEST_F(SiglusSceneEntryTest, ZlabelResolvesLabelAndFallsBackToScript) {
  auto* label = module->globals->at(GetZlabelId(0)).Get_if<sr::Function>();
  auto* script = module->globals->at("%%script").Get_if<sr::Function>();
  ASSERT_NE(label, nullptr);
  ASSERT_NE(script, nullptr);
  EXPECT_EQ(ResolveSceneEntry(loader, "MENU", 0), label);
  EXPECT_EQ(ResolveSceneEntry(loader, "menu", 77), script);
}

}  // namespace libsiglus::binding
