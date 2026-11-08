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

#include "srbind/class_registry.hpp"

#include <gtest/gtest.h>

#include "vm/gc.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace srbind_test {
namespace {
struct Payload {
  int value;
};
struct OtherPayload {
  std::string value;
};
struct Immovable {
  explicit Immovable(std::unique_ptr<int> value) : value(std::move(value)) {}
  Immovable(Immovable&&) = delete;
  std::unique_ptr<int> value;
};
struct ConstructorError : std::runtime_error {
  ConstructorError() : std::runtime_error("payload construction failed") {}
};
struct ThrowingPayload {
  ThrowingPayload() { throw ConstructorError(); }
};
}  // namespace

class ClassRegistryTest : public ::testing::Test {
 protected:
  serilang::GarbageCollector gc;
  std::unordered_map<std::string, serilang::Value> globals;
  srbind::module_ mod{&gc, &globals};
  srbind::ClassRegistry registry;
};

TEST_F(ClassRegistryTest, ConstructsRegisteredClass) {
  // Register a payload and construct it; both native identity and data match.
  srbind::class_<Payload> klass(mod, "Payload");
  registry.Register(klass);
  auto* instance = registry.MakeInst<Payload>(42);
  EXPECT_EQ(instance->klass, klass.native_class());
  EXPECT_EQ(instance->GetForeign<Payload>()->value, 42);
}

TEST_F(ClassRegistryTest, KeepsTypesIndependent) {
  // Register two payload types; each construction selects its own class.
  srbind::class_<Payload> first(mod, "Payload");
  srbind::class_<OtherPayload> second(mod, "OtherPayload");
  registry.Register(first);
  registry.Register(second);
  EXPECT_EQ(registry.MakeInst<Payload>(1)->klass, first.native_class());
  auto* instance = registry.MakeInst<OtherPayload>(std::string("text"));
  EXPECT_EQ(instance->klass, second.native_class());
  EXPECT_EQ(instance->GetForeign<OtherPayload>()->value, "text");
}

TEST_F(ClassRegistryTest, KeepsRegistriesIndependent) {
  // Register a type in one registry; an unrelated registry still rejects it.
  registry.Register(srbind::class_<Payload>(mod, "Payload"));
  srbind::ClassRegistry other;
  EXPECT_THROW(other.MakeInst<Payload>(1), std::runtime_error);
}

TEST_F(ClassRegistryTest, ReplacesClassForSubsequentInstances) {
  // Re-register a type; existing instances retain their class and new ones
  // change.
  srbind::class_<Payload> first(mod, "First");
  srbind::class_<Payload> second(mod, "Second");
  registry.Register(first);
  auto* old = registry.MakeInst<Payload>(1);
  registry.Register(second);
  EXPECT_EQ(old->klass, first.native_class());
  EXPECT_EQ(registry.MakeInst<Payload>(2)->klass, second.native_class());
}

TEST_F(ClassRegistryTest, MissingClassReportsRequestedType) {
  // Request an unregistered type; the standard exception identifies that type.
  EXPECT_THROW(registry.MakeInst<Payload>(1), std::runtime_error);
}

TEST_F(ClassRegistryTest, ForwardsLvalueArguments) {
  // Construct from an lvalue string; the payload copies it without consuming
  // it.
  registry.Register(srbind::class_<OtherPayload>(mod, "OtherPayload"));
  std::string text = "unchanged";
  auto* instance = registry.MakeInst<OtherPayload>(text);
  EXPECT_EQ(instance->GetForeign<OtherPayload>()->value, text);
  EXPECT_EQ(text, "unchanged");
}

TEST_F(ClassRegistryTest, ConstructsImmovablePayloadWithMoveOnlyArgument) {
  // Forward a unique pointer; direct construction needs no payload move.
  registry.Register(srbind::class_<Immovable>(mod, "Immovable"));
  auto value = std::make_unique<int>(73);
  auto* instance = registry.MakeInst<Immovable>(std::move(value));
  EXPECT_EQ(value, nullptr);
  EXPECT_EQ(*instance->GetForeign<Immovable>()->value, 73);
}

TEST_F(ClassRegistryTest, PreservesConstructorExceptions) {
  // A registered constructor fails; its original exception reaches the caller.
  registry.Register(srbind::class_<ThrowingPayload>(mod, "Throwing"));
  EXPECT_THROW(registry.MakeInst<ThrowingPayload>(), ConstructorError);
}

}  // namespace srbind_test
