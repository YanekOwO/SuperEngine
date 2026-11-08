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

#include "core/script_call.hpp"
#include "libsiglus/archive.hpp"
#include "libsiglus/bindings/loader.hpp"
#include "libsiglus/intern_name.hpp"
#include "siglus/fake_scene.hpp"
#include "vm/exception.hpp"
#include "vm/function.hpp"
#include "vm/gc.hpp"
#include "vm/object.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace siglus_test {
using namespace libsiglus;
using namespace libsiglus::binding;
namespace sr = serilang;

static sr::Function* Dispatcher(sr::Value entry) {
  auto* bound = entry.Get_if<sr::BoundMethod>();
  return bound->method.Get_if<sr::Function>();
}

class SiglusLoaderTest : public ::testing::Test {
 protected:
  Archive archive{std::string(sizeof(Pack_hdr), '\0'), xorkey_t{}};
  sr::VM vm{std::make_shared<sr::GarbageCollector>()};
  Loader loader{archive, vm, false};
  sr::Module* mod = nullptr;

  void AddCallbackScene(std::vector<Type> args) {
    FakeScene{.scene_name = "ui",
              .command_name = "animate",
              .args = std::move(args),
              .included = true}
        .AddTo(archive);
  }

  void SetUp() override {
    FakeScene{.scene_name = "menu", .command_name = "open"}.AddTo(archive);
    mod = loader.Load(0);
  }
};

TEST_F(SiglusLoaderTest, LoadSceneNormalizesAndReusesCachedModule) {
  // A mixed-case name must select the existing module without reloading it.
  EXPECT_EQ(&loader.LoadScene("MeNu"), mod);
}

TEST_F(SiglusLoaderTest, LoadSceneRejectsEmptyName) {
  // An empty destination name must fail before attempting scene loading.
  EXPECT_THROW(loader.LoadScene(""), sr::RuntimeError);
}

TEST_F(SiglusLoaderTest, LoadSceneRejectsMissingScene) {
  // A name absent from the archive must report a runtime error.
  EXPECT_THROW(loader.LoadScene("missing"), sr::RuntimeError);
}

TEST_F(SiglusLoaderTest, SceneUserCommandReturnsCachedMetadata) {
  // Named lookup exposes the same command metadata as module-based lookup.
  EXPECT_EQ(&loader.ResolveSceneUserCommand("MENU", "OPEN"),
            &loader.ResolveUserCommand(*mod, "open"));
}

TEST_F(SiglusLoaderTest, ScriptCallResolvesCommandTarget) {
  // A command target selects its source entry's dispatcher adapter.
  const ScriptCall call{.target = CmdTarget{0, 5}};
  EXPECT_EQ(loader.ResolveScriptCall(call), mod->globals->at(GetUsercmdId(5)));
}

TEST_F(SiglusLoaderTest, ScriptCallResolvesNumericSceneTarget) {
  // A numeric scene target selects the requested z-label adapter.
  const ScriptCall call{.target = SceneTarget{0, 0}};
  EXPECT_EQ(loader.ResolveScriptCall(call), mod->globals->at(GetZlabelId(0)));
}

TEST_F(SiglusLoaderTest, ScriptCallResolvesNamedSceneTarget) {
  // A mixed-case scene target resolves its name before selecting the label.
  const ScriptCall call{.target = SceneTarget{std::string("MENU"), 0}};
  EXPECT_EQ(loader.ResolveScriptCall(call), mod->globals->at(GetZlabelId(0)));
}

TEST_F(SiglusLoaderTest, ScriptCallFallsBackToMainScript) {
  // An absent z-label in a valid scene selects the main script adapter.
  const ScriptCall call{.target = SceneTarget{0, 77}};
  EXPECT_EQ(loader.ResolveScriptCall(call), mod->globals->at("%%script"));
}

TEST_F(SiglusLoaderTest, ScriptCallRejectsNegativeSceneId) {
  // A negative numeric scene target must fail instead of loading a scene.
  const ScriptCall call{.target = SceneTarget{-1, 0}};
  EXPECT_THROW(loader.ResolveScriptCall(call), sr::RuntimeError);
}

TEST_F(SiglusLoaderTest, SceneEntryRejectsMissingAdapter) {
  // Remove both label and fallback; resolution must report a missing entry.
  mod->globals->erase(GetZlabelId(0));
  mod->globals->erase("%%script");
  EXPECT_THROW(loader.ResolveSceneEntry("menu", 0), sr::RuntimeError);
}

TEST_F(SiglusLoaderTest, SceneEntryRejectsInvalidDispatcherAdapter) {
  // A non-adapter stored at the requested label must fail validation.
  (*mod->globals)[GetZlabelId(0)] = sr::Value(42);
  EXPECT_THROW(loader.ResolveSceneEntry("menu", 0), sr::RuntimeError);
}

TEST_F(SiglusLoaderTest, EntriesShareOneDispatcher) {
  // Scene, label, and command adapters all retain the same execution function.
  auto* dispatcher = mod->globals->at("%%dispatch").Get_if<sr::Function>();
  EXPECT_EQ(Dispatcher(mod->globals->at("%%script")), dispatcher);
  EXPECT_EQ(Dispatcher(mod->globals->at(GetZlabelId(0))), dispatcher);
  EXPECT_EQ(Dispatcher(mod->globals->at(GetUsercmdId(5))), dispatcher);
}

TEST_F(SiglusLoaderTest, NamedCommandResolvesThroughCachedMetadata) {
  // Resolve a named command from a loaded scene; return its command adapter.
  auto expected = mod->globals->at(GetUsercmdId(5));
  ASSERT_NE(expected.Get_if<sr::BoundMethod>(), nullptr);
  EXPECT_EQ(loader.ResolveSceneCommand("MENU", "open"), expected);
}

TEST_F(SiglusLoaderTest, IncludedCommandLoadsDeclaringSceneOnDemand) {
  // Resolve an included command before its owner is loaded; load and cache
  // the declaring scene and command metadata.
  AddCallbackScene({Type::FrameAction, Type::Object});
  EXPECT_FALSE(vm.builtins_->contains("%%siglus_module@SCENE_0001"));

  const auto& resolved = loader.ResolveUserCommand(*mod, "AnImAtE");

  EXPECT_TRUE(vm.builtins_->contains("%%siglus_module@SCENE_0001"));
  const auto* owner = loader.Load(1);
  EXPECT_EQ(resolved.function, owner->globals->at(GetUsercmdId(5)));
  EXPECT_EQ(Dispatcher(resolved.function)->globals, owner->globals);
  EXPECT_EQ(resolved.args,
            (std::vector<Type>{Type::FrameAction, Type::Object}));
  EXPECT_EQ(&loader.ResolveUserCommand(*mod, "animate"), &resolved);
}

TEST_F(SiglusLoaderTest, IncludedCommandUsesAlreadyLoadedDeclaringScene) {
  // Preload the declaring scene; included lookup must reuse its adapter.
  AddCallbackScene({Type::FrameAction, Type::Object});
  const auto* owner = loader.Load(1);
  const auto expected = owner->globals->at(GetUsercmdId(5));

  EXPECT_EQ(loader.ResolveSceneCommand("menu", "animate"), expected);
  EXPECT_EQ(loader.Load(1), owner);
}

TEST_F(SiglusLoaderTest, IncludedCommandTakesPrecedenceOverLocalCommand) {
  // Declare an included command with a local name; lookup selects the
  // included command.
  FakeScene{.scene_name = "shared",
            .command_name = "open",
            .args = {Type::Int},
            .included = true}
      .AddTo(archive);
  const auto& resolved = loader.ResolveUserCommand(*mod, "OPEN");

  EXPECT_EQ(Dispatcher(resolved.function)->globals, loader.Load(1)->globals);
  EXPECT_EQ(resolved.args, std::vector<Type>{Type::Int});
  EXPECT_NE(resolved.function, mod->globals->at(GetUsercmdId(5)));
}

TEST_F(SiglusLoaderTest, InvalidIncludedTargetDoesNotFallBackToLocalCommand) {
  // Point an included command at a missing scene; lookup must fail rather
  // than invoke the local command of the same name.
  archive.cmd_map_["open"] = archive.cmd_.size();
  archive.cmd_.push_back(Command{.scene_id = 99, .offset = 5, .name = "open"});

  EXPECT_THROW(loader.ResolveUserCommand(*mod, "open"), sr::RuntimeError);
}

TEST_F(SiglusLoaderTest, LocalCommandNamesAreCaseInsensitive) {
  // Use mixed-case local names; lookup ignores case but stays within the
  // selected scene.
  const int scene =
      FakeScene{.scene_name = "other", .command_name = "LoCaL"}.AddTo(archive);
  const auto* owner = loader.Load(scene);
  const auto expected = owner->globals->at(GetUsercmdId(5));

  EXPECT_EQ(loader.ResolveUserCommand(*owner, "LOCAL").function, expected);
  EXPECT_EQ(loader.ResolveSceneCommand("menu", "OpEn"),
            mod->globals->at(GetUsercmdId(5)));
  EXPECT_THROW(loader.ResolveUserCommand(*mod, "local"), sr::RuntimeError);
}

TEST_F(SiglusLoaderTest, FiberResolvesIncludedCommandFromNearestScene) {
  // Surround a scene frame with native frames; lookup finds its included
  // command while ignoring native frames.
  AddCallbackScene({Type::FrameAction, Type::Object});
  sr::Fiber fiber;
  fiber.frames.push_back({});  // Native/non-scene frames are ignored.
  fiber.frames.push_back(
      {.fn = mod->globals->at("%%dispatch").Get_if<sr::Function>()});
  fiber.frames.push_back({});

  const auto& resolved = loader.ResolveUserCommand(fiber, "ANIMATE");
  EXPECT_EQ(Dispatcher(resolved.function)->globals, loader.Load(1)->globals);
}

TEST_F(SiglusLoaderTest, FiberDoesNotSearchOlderScenesForLocalCommands) {
  // Stack two scene frames; local lookup must use only the nearest scene.
  const int other =
      FakeScene{.scene_name = "other", .command_name = "local"}.AddTo(archive);
  const auto* current = loader.Load(other);
  sr::Fiber fiber;
  fiber.frames.push_back(
      {.fn = mod->globals->at("%%dispatch").Get_if<sr::Function>()});
  fiber.frames.push_back(
      {.fn = current->globals->at("%%dispatch").Get_if<sr::Function>()});

  EXPECT_EQ(
      Dispatcher(loader.ResolveUserCommand(fiber, "LOCAL").function)->globals,
      current->globals);
  EXPECT_THROW(loader.ResolveUserCommand(fiber, "open"), sr::RuntimeError);
}

TEST_F(SiglusLoaderTest, IncludedCommandsStillRequireSceneContext) {
  // Resolve an included command without a loaded scene context; both
  // fiber and module lookup must reject the request.
  AddCallbackScene({Type::FrameAction, Type::Object});
  sr::Fiber fiber;
  EXPECT_THROW(loader.ResolveUserCommand(fiber, "animate"), sr::RuntimeError);
  sr::Module unloaded("unloaded");
  EXPECT_THROW(loader.ResolveUserCommand(unloaded, "animate"),
               sr::RuntimeError);
}

TEST_F(SiglusLoaderTest, MissingNamedCommandDoesNotFallBackToScript) {
  // Request a missing command in a valid scene; lookup must fail rather
  // than invoke the scene script.
  ASSERT_NE(mod->globals->at("%%dispatch").Get_if<sr::Function>(), nullptr);
  EXPECT_THROW(loader.ResolveSceneCommand("menu", "missing"), sr::RuntimeError);
}

TEST_F(SiglusLoaderTest, ZlabelResolvesLabelAndFallsBackToScript) {
  // Resolve present and absent labels; return the label adapter or the
  // scene script fallback respectively.
  auto label = mod->globals->at(GetZlabelId(0));
  auto script = mod->globals->at("%%script");
  ASSERT_NE(label.Get_if<sr::BoundMethod>(), nullptr);
  ASSERT_NE(script.Get_if<sr::BoundMethod>(), nullptr);
  EXPECT_EQ(loader.ResolveSceneEntry("MENU", 0), label);
  EXPECT_EQ(loader.ResolveSceneEntry("menu", 77), script);
}

}  // namespace siglus_test
