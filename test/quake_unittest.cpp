// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------

#include <gtest/gtest.h>

#include "core/quake.hpp"

#include <limits>
#include <memory>

namespace {

QuakeState::StartParams ShakeParams(int type = 1) {
  return QuakeState::StartParams{.type = type,
                                 .direction = 2,
                                 .power = 100,
                                 .period_ms = 400,
                                 .count = 0,
                                 .end_count = 0,
                                 .begin_order = 3,
                                 .end_order = 7};
}

TEST(QuakeStateTest, DirectionalShakeTraversesBothSides) {
  QuakeState quake;
  ASSERT_TRUE(quake.Start(ShakeParams()));
  EXPECT_EQ(quake.Check(), 1);
  EXPECT_TRUE(quake.CurrentTransform().IsIdentity());

  quake.Advance(100);
  EXPECT_DOUBLE_EQ(quake.CurrentTransform().translate_x, 50.0);
  quake.Advance(100);
  EXPECT_DOUBLE_EQ(quake.CurrentTransform().translate_x, 0.0);
  quake.Advance(100);
  EXPECT_DOUBLE_EQ(quake.CurrentTransform().translate_x, -50.0);
  quake.Advance(100);
  EXPECT_TRUE(quake.CurrentTransform().IsIdentity());
}

TEST(QuakeStateTest, VectorShakeUsesOneSidedWave) {
  QuakeState quake;
  ASSERT_TRUE(quake.Start(ShakeParams(0)));

  quake.Advance(100);
  EXPECT_DOUBLE_EQ(quake.CurrentTransform().translate_x, 50.0);
  quake.Advance(100);
  EXPECT_DOUBLE_EQ(quake.CurrentTransform().translate_x, 100.0);
  quake.Advance(100);
  EXPECT_DOUBLE_EQ(quake.CurrentTransform().translate_x, 50.0);
  quake.Advance(100);
  EXPECT_TRUE(quake.CurrentTransform().IsIdentity());
}

TEST(QuakeStateTest, ZoomCompensatesAroundConfiguredCenter) {
  QuakeState quake;
  auto params = ShakeParams(2);
  params.power = 128;
  params.center_x = 320;
  params.center_y = 180;
  ASSERT_TRUE(quake.Start(params));

  quake.Advance(100);
  QuakeTransform transform = quake.CurrentTransform();
  EXPECT_DOUBLE_EQ(transform.scale, 1.5);
  EXPECT_DOUBLE_EQ(transform.translate_x, -160.0);
  EXPECT_DOUBLE_EQ(transform.translate_y, -90.0);

  quake.Advance(100);
  transform = quake.CurrentTransform();
  EXPECT_DOUBLE_EQ(transform.scale, 2.0);
  EXPECT_DOUBLE_EQ(transform.translate_x, -320.0);
  EXPECT_DOUBLE_EQ(transform.translate_y, -180.0);
}

TEST(QuakeStateTest, NaturalEndEnvelopeAndTimedEndReachIdentity) {
  QuakeState quake;
  auto params = ShakeParams();
  params.count = 1;
  params.end_count = 1;
  ASSERT_TRUE(quake.Start(params));

  quake.Advance(500);
  EXPECT_DOUBLE_EQ(quake.CurrentTransform().translate_x, 37.0);
  quake.End(200);
  EXPECT_EQ(quake.Check(), 2);
  quake.Advance(100);
  EXPECT_EQ(quake.Check(), 2);
  EXPECT_DOUBLE_EQ(quake.CurrentTransform().translate_x, 0.0);
  quake.Advance(100);
  EXPECT_EQ(quake.Check(), 0);
  EXPECT_TRUE(quake.CurrentTransform().IsIdentity());
}

TEST(QuakeStateTest, FiniteQuakeEndsAndInfiniteQuakeLoops) {
  QuakeState finite;
  auto params = ShakeParams();
  params.count = 1;
  ASSERT_TRUE(finite.Start(params));
  finite.Advance(399);
  EXPECT_EQ(finite.Check(), 1);
  finite.Advance(1);
  EXPECT_EQ(finite.Check(), 0);

  QuakeState loop;
  ASSERT_TRUE(loop.Start(ShakeParams()));
  loop.Advance(std::numeric_limits<std::int64_t>::max());
  EXPECT_EQ(loop.Check(), 1);
  loop.End();
  EXPECT_EQ(loop.Check(), 0);
}

TEST(QuakeStateTest, UnsupportedTypeIsNoopAndOrderRangeIsInclusive) {
  QuakeState quake;
  ASSERT_TRUE(quake.Start(ShakeParams()));
  EXPECT_TRUE(quake.Contains(3));
  EXPECT_TRUE(quake.Contains(7));
  EXPECT_FALSE(quake.Contains(2));
  EXPECT_FALSE(quake.Start(QuakeState::StartParams{.type = 3}));
  EXPECT_EQ(quake.Check(), 1);
}

TEST(QuakeTransformTest, CompositionAppliesLaterTransformAfterEarlierOne) {
  QuakeTransform first{
      .scale = 2.0, .translate_x = -10.0, .translate_y = -20.0};
  first.Then(
      QuakeTransform{.scale = 0.5, .translate_x = 7.0, .translate_y = 11.0});

  EXPECT_DOUBLE_EQ(first.scale, 1.0);
  EXPECT_DOUBLE_EQ(first.translate_x, 2.0);
  EXPECT_DOUBLE_EQ(first.translate_y, 1.0);
}

TEST(QuakeListTest, ComposesOnlyMatchingActiveQuakesInIndexOrder) {
  QuakeList list(2);
  auto first = list.GetHandle(0).lock();
  auto second = list.GetHandle(1).lock();
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);

  auto translation = ShakeParams();
  translation.begin_order = 5;
  translation.end_order = 5;
  ASSERT_TRUE(first->Start(translation));
  first->Advance(100);

  auto zoom = ShakeParams(2);
  zoom.power = 128;
  zoom.center_x = 0;
  zoom.center_y = 0;
  zoom.begin_order = 5;
  zoom.end_order = 5;
  ASSERT_TRUE(second->Start(zoom));
  second->Advance(200);

  QuakeTransform result = list.TransformForOrder(5);
  EXPECT_DOUBLE_EQ(result.scale, 2.0);
  EXPECT_DOUBLE_EQ(result.translate_x, 100.0);
  EXPECT_TRUE(list.TransformForOrder(4).IsIdentity());
}

}  // namespace
