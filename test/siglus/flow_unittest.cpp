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
#include "libsiglus/intern_name.hpp"
#include "libsiglus/lexeme.hpp"
#include "vm/exception.hpp"
#include "vm/function.hpp"
#include "vm/gc.hpp"
#include "vm/value.hpp"

#include <cstdint>
#include <memory>
#include <string>

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

  void AddCallbackScene(std::vector<Type> args) {
    std::string code(5, '\0');
    code[0] = static_cast<char>(ByteCode::Return);
    const int32_t offset = code.size();
    for (std::size_t i = 0; i < args.size(); ++i) {
      code += static_cast<char>(ByteCode::Declare);
      const auto type = static_cast<int32_t>(args[i]);
      const auto index = static_cast<int32_t>(i);
      code.append(reinterpret_cast<const char*>(&type), sizeof(type));
      code.append(reinterpret_cast<const char*>(&index), sizeof(index));
    }
    code += static_cast<char>(ByteCode::Arg);
    code += static_cast<char>(ByteCode::Return);
    code.append(4, '\0');
    code += static_cast<char>(ByteCode::End);
    const Scene_hdr header{.header_size = sizeof(Scene_hdr),
                           .scene_offset = sizeof(Scene_hdr),
                           .scene_size = static_cast<int32_t>(code.size())};
    std::string scene(reinterpret_cast<const char*>(&header), sizeof(header));
    scene += code;
    archive.raw_scene_data_.push_back(std::move(scene));
    archive.scene_names_.push_back("ui");
    archive.scn_map_["ui"] = 1;
    archive.cmd_.push_back(
        Command{.scene_id = 1, .offset = offset, .name = "animate"});
  }

  void SetUp() override {
    // A top-level return followed by a zero-argument named command.
    std::string code(5, '\0');
    code[0] = static_cast<char>(ByteCode::Return);
    const int32_t command_offset = code.size();
    code += static_cast<char>(ByteCode::Arg);
    code += static_cast<char>(ByteCode::Return);
    code.append(4, '\0');
    code += static_cast<char>(ByteCode::End);

    const Scene_hdr header{
        .header_size = sizeof(Scene_hdr),
        .scene_offset = sizeof(Scene_hdr),
        .scene_size = static_cast<int32_t>(code.size()),
        .zlabel_list_offset =
            static_cast<int32_t>(sizeof(Scene_hdr) + code.size()),
        .zlabel_cnt = 1,
    };
    std::string scene(reinterpret_cast<const char*>(&header), sizeof(header));
    scene += code;
    scene.append(reinterpret_cast<const char*>(&command_offset),
                 sizeof(command_offset));
    archive.raw_scene_data_.push_back(std::move(scene));
    archive.scene_names_.push_back("menu");
    archive.scn_map_["menu"] = 0;
    archive.cmd_.push_back(
        Command{.scene_id = 0, .offset = command_offset, .name = "open"});
    module = loader.Load(0);
  }
};

TEST_F(SiglusSceneEntryTest, NamedCommandResolvesThroughCachedMetadata) {
  auto* expected = module->globals->at(GetUsercmdId(5)).Get_if<sr::Function>();
  ASSERT_NE(expected, nullptr);
  EXPECT_EQ(ResolveSceneCommand(loader, "MENU", "open"), expected);
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
  action->GetCounter()->Advance(32, 1);
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
  global_counter->Start(false);
  auto object = std::make_shared<GraphicsObject>();
  stage.objects[kLayerFg][0] = object;
  auto object_counter = object->GetFrameAction()->GetCounter();
  object_counter->Start(false);

  config.frame_action_time_stop = true;
  manager.AdvanceAndDispatch(std::chrono::milliseconds(10), false);
  EXPECT_EQ(global_counter->Get(), 0);
  EXPECT_EQ(object_counter->Get(), 10);

  config.frame_action_time_stop = false;
  config.stage_time_stop = true;
  manager.AdvanceAndDispatch(std::chrono::milliseconds(10), false);
  EXPECT_EQ(global_counter->Get(), 10);
  EXPECT_EQ(object_counter->Get(), 10);

  config = LocalConfig{};
  manager.AdvanceAndDispatch(std::chrono::milliseconds(5), true);
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
