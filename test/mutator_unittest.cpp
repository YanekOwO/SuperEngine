// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
// -----------------------------------------------------------------------

#include "core/mutator.hpp"
#include "core/interpolation.hpp"
#include "mock_clock.hpp"
#include "serialization_test_helpers.hpp"

#include <gtest/gtest.h>
#include <memory>

namespace {

using std::chrono_literals::operator""ms;

static inline FrameCounter MakeCounter(
    const std::shared_ptr<Clock>& clock,
    int start,
    int end,
    int duration_ms,
    InterpolationType type = InterpolationType::OneShot) {
  Interpolation interpolation(Range(start, end), type,
                              InterpolationMode::Linear);
  return FrameCounter(clock, std::move(interpolation), duration_ms);
}

struct TestContext {
  int value = 0;
  int other = 0;
};

TEST(MutatorTest, MatchesEqualityComparableSetter) {
  auto clock = std::make_shared<MockClock>();
  using Parameter = int TestContext::*;
  Mutator<Parameter, TestContext> mutator(&TestContext::value,
                                          MakeCounter(clock, 0, 10, 100));

  EXPECT_TRUE(mutator.Matches(&TestContext::value));
  EXPECT_FALSE(mutator.Matches(&TestContext::other));
}

TEST(MutatorTest, ContextFreeSetterTracksCompletion) {
  auto clock = std::make_shared<MockClock>();
  int value = -1;
  Mutator mutator([&value](int next) { value = next; },
                  MakeCounter(clock, 0, 10, 100));

  EXPECT_FALSE(mutator.Update());
  EXPECT_EQ(value, 0);

  clock->AdvanceTime(100ms);
  EXPECT_TRUE(mutator.Update());
  EXPECT_EQ(value, 10);
}

TEST(MutatorTest, ContextFreeAccessorReceivesForcedEndValue) {
  auto clock = std::make_shared<MockClock>();
  int value = -1;
  Mutator mutator([&value]() -> int& { return value; },
                  MakeCounter(clock, 0, 10, 1000));

  clock->AdvanceTime(100ms);
  EXPECT_FALSE(mutator.Update());
  EXPECT_EQ(value, 1);

  mutator.SetToEnd();
  EXPECT_EQ(value, 10);
}

TEST(MutatorTest, ContextualSetterTracksCompletion) {
  auto clock = std::make_shared<MockClock>();
  TestContext context;
  auto setter = [](TestContext& target, int value) { target.value = value; };
  Mutator<decltype(setter), TestContext> mutator(
      setter, MakeCounter(clock, 10, 20, 100));

  EXPECT_FALSE(mutator.Update(context));
  EXPECT_EQ(context.value, 10);

  clock->AdvanceTime(100ms);
  EXPECT_TRUE(mutator.Update(context));
  EXPECT_EQ(context.value, 20);
}

TEST(MutatorTest, ContextualAccessorReceivesFrozenRepeatingValue) {
  auto clock = std::make_shared<MockClock>();
  TestContext context;
  auto accessor = [](TestContext& target) -> int& { return target.value; };
  Mutator<decltype(accessor), TestContext> mutator(
      accessor, MakeCounter(clock, 0, 100, 1000, InterpolationType::Loop));

  clock->AdvanceTime(250ms);
  mutator.SetToEnd(context);
  EXPECT_EQ(context.value, 25);
}

// ------------------------------------------------------------------------------

class MutatorArchiveTest : public ::testing::Test {};

TEST_F(MutatorArchiveTest, ReboundSetterUsesSavedProgress) {
  // A generic mutator saves progress while the destination supplies its own
  // callable.
  serialization_test::ForEachFormat([&](auto format) {
    using namespace std::chrono_literals;
    auto clock = std::make_shared<MockClock>();
    int original = 0, restored = 0;
    using Event = Mutator<std::function<void(int)>>;
    Event source([&](int v) { original = v; },
                 FrameCounter(clock, Interpolation({0, 100}), 100));
    clock->AdvanceTime(40ms);
    Event target;
    serialization_test::RoundTrip(source, target, format, {}, {.clock = clock});
    EXPECT_FALSE(target.setter());
    target.SetSetter([&](int v) { restored = v; });
    clock->AdvanceTime(20ms);
    target.Update();
    EXPECT_EQ(restored, 60);
    EXPECT_EQ(original, 0);
  });
}

}  // namespace
