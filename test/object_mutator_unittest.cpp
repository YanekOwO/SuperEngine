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

#include "core/object_internal/object_mutator.hpp"
#include "core/object_internal/objdrawer.hpp"
#include "core/object_internal/object_parameter.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/runtime_checkpoint.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "mock_clock.hpp"
#include "serialization_test_helpers.hpp"
#include "systems/sdl/sdl_surface.hpp"

namespace {

class ObjectMutatorArchiveTest : public ::testing::Test {};

TEST_F(ObjectMutatorArchiveTest, RebindsSymbolicSetterAndContinuesProgress) {
  // An event resolves its saved name and index in the destination context and
  // resumes halfway.
  serialization_test::ForEachFormat([&](auto format) {
    using namespace std::chrono_literals;
    auto clock = std::make_shared<MockClock>();
    ObjectMutator source(ObjectParameterMutator(
                             [](ObjectParameter& p, int v) { p.SetX(v); },
                             FrameCounter(clock, Interpolation({0, 100}), 100)),
                         3, "x_eve");
    clock->AdvanceTime(50ms);
    auto loading_clock = std::make_shared<MockClock>();
    int bindings = 0;
    SerializationContext context{.clock = loading_clock};
    context.object_setter = [&](const std::string& name, int index) {
      EXPECT_EQ(name, "x_eve");
      EXPECT_EQ(index, 3);
      ++bindings;
      return [](ObjectParameter& p, int v) { p.SetY(v); };
    };
    ObjectMutator target;
    serialization_test::RoundTrip(source, target, format, {}, context);
    EXPECT_EQ(bindings, 1);
    loading_clock->AdvanceTime(25ms);
    ObjectParameter param;
    EXPECT_FALSE(target.Update(param));
    EXPECT_EQ(param.y(), 75);
    EXPECT_EQ(param.x(), 0);
  });
}

TEST_F(ObjectMutatorArchiveTest, MissingSetterServicesRejectLoad) {
  // A saved event cannot load without the service that resolves its setter.
  serialization_test::ForEachFormat([&](auto format) {
    ObjectMutator source;
    auto bytes = serialization_test::Save(source, format);
    ObjectMutator target;
    EXPECT_THROW(serialization_test::Load(bytes, target, format),
                 std::runtime_error);
  });
}

TEST_F(ObjectMutatorArchiveTest, CompletionCallbackCannotBeSaved) {
  // An executable completion callback has no persistent representation and
  // rejects saving.
  serialization_test::ForEachFormat([&](auto format) {
    ObjectMutator source;
    source.OnComplete([](ObjectParameter&) {});
    EXPECT_THROW(serialization_test::Save(source, format), std::runtime_error);
  });
}

// ------------------------------------------------------------------------------

class ObjectMutatorBindingArchiveTest : public ::testing::Test {};

TEST_F(ObjectMutatorBindingArchiveTest,
       NativeSetterContinuesSharedCounterEvent) {
  // An archived native event resolves its actual binding and continues on the
  // restored shared counter.
  serialization_test::ForEachFormat([&](auto format) {
    using namespace std::chrono_literals;
    libsiglus::SiglusRuntime runtime;
    runtime.stage = std::make_unique<Stage>(1);
    runtime.GameplayMachine().vm = std::make_unique<serilang::VM>(
        std::make_shared<serilang::GarbageCollector>());
    auto* bind = libsiglus::binding::SiglusBindingRegistry::Find("1_object");
    ASSERT_NE(bind, nullptr);
    (*bind)(runtime.GameplayMachine());
    auto counter = std::make_shared<Counter>();
    counter->StartGame();
    FrameCounter progress(counter, Interpolation({0, 100}), 100);
    counter->Advance(50ms, 0ms);
    auto object = std::make_shared<GraphicsObject>();
    object->AddObjectMutator(ObjectMutator(
        ObjectParameterMutator([](ObjectParameter& p, int v) { p.SetX(v); },
                               progress),
        -1, "x_eve"));
    auto source = std::pair{object, counter};
    decltype(source) target;
    serialization_test::RoundTrip(source, target, format, {},
                                  libsiglus::MakeSerializationContext(runtime));
    target.second->Advance(25ms, 0ms);
    target.first->ExecuteMutators();
    EXPECT_EQ(target.first->Param().x(), 75);
  });
}

}  // namespace
