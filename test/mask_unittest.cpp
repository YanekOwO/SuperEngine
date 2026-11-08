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

#include "core/mask.hpp"
#include "image_archive_test_support.hpp"
#include "mock_clock.hpp"
#include "serialization_test_helpers.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <memory>

namespace {

using std::chrono_literals::operator""ms;

FrameCounter MakeMaskCounter(
    const std::shared_ptr<Clock>& clock,
    int start,
    int end,
    int duration_ms,
    InterpolationType type = InterpolationType::OneShot) {
  return FrameCounter(
      clock, Interpolation(Range(start, end), type, InterpolationMode::Linear),
      duration_ms);
}

TEST(MaskTest, MutatorsForDifferentParametersRunIndependently) {
  auto clock = std::make_shared<MockClock>();
  Mask mask;
  mask.AddMutator(Mask::ParameterMutator(&Mask::Parameters::x,
                                         MakeMaskCounter(clock, 0, 10, 100)));
  mask.AddMutator(Mask::ParameterMutator(&Mask::Parameters::y,
                                         MakeMaskCounter(clock, 20, 40, 200)));

  mask.Execute();
  EXPECT_EQ(mask.Param().x, 0);
  EXPECT_EQ(mask.Param().y, 20);
  EXPECT_TRUE(mask.IsMutatorRunning(&Mask::Parameters::x));
  EXPECT_TRUE(mask.IsMutatorRunning(&Mask::Parameters::y));

  clock->AdvanceTime(100ms);
  mask.Execute();
  EXPECT_EQ(mask.Param().x, 10);
  EXPECT_EQ(mask.Param().y, 30);
  EXPECT_FALSE(mask.IsMutatorRunning(&Mask::Parameters::x));
  EXPECT_TRUE(mask.IsMutatorRunning(&Mask::Parameters::y));

  clock->AdvanceTime(100ms);
  mask.Execute();
  EXPECT_EQ(mask.Param().y, 40);
  EXPECT_FALSE(mask.IsMutatorRunning(&Mask::Parameters::y));
}

TEST(MaskTest, EndMutatorOnlyEndsMatchingParameter) {
  auto clock = std::make_shared<MockClock>();
  Mask mask;
  mask.AddMutator(Mask::ParameterMutator(&Mask::Parameters::x,
                                         MakeMaskCounter(clock, 0, 100, 1000)));
  mask.AddMutator(Mask::ParameterMutator(
      &Mask::Parameters::y,
      MakeMaskCounter(clock, 0, 100, 1000, InterpolationType::Loop)));

  clock->AdvanceTime(250ms);
  mask.EndMutator(&Mask::Parameters::x);
  EXPECT_EQ(mask.Param().x, 100);
  EXPECT_FALSE(mask.IsMutatorRunning(&Mask::Parameters::x));
  EXPECT_TRUE(mask.IsMutatorRunning(&Mask::Parameters::y));

  mask.EndMutator(&Mask::Parameters::y);
  EXPECT_EQ(mask.Param().y, 25);
  EXPECT_FALSE(mask.IsMutatorRunning(&Mask::Parameters::y));
}

TEST(MaskTest, ResetClearsParametersAssetAndMutators) {
  auto clock = std::make_shared<MockClock>();
  Mask mask;
  mask.Create("mask.png", nullptr);
  mask.Param().x = 12;
  mask.Param().y = 34;
  mask.AddMutator(Mask::ParameterMutator(&Mask::Parameters::x,
                                         MakeMaskCounter(clock, 12, 50, 100)));

  mask.Reset();

  EXPECT_EQ(mask.Param().x, 0);
  EXPECT_EQ(mask.Param().y, 0);
  EXPECT_TRUE(mask.filename().empty());
  EXPECT_FALSE(mask.surface());
  EXPECT_FALSE(mask.IsMutatorRunning(&Mask::Parameters::x));
}

TEST(MaskTest, CreateReinitializesParametersAndMutators) {
  auto clock = std::make_shared<MockClock>();
  Mask mask;
  mask.Param().x = 12;
  mask.Param().y = 34;
  mask.AddMutator(Mask::ParameterMutator(&Mask::Parameters::x,
                                         MakeMaskCounter(clock, 12, 50, 100)));

  mask.Create("replacement.png", nullptr);

  EXPECT_EQ(mask.Param().x, 0);
  EXPECT_EQ(mask.Param().y, 0);
  EXPECT_EQ(mask.filename(), "replacement.png");
  EXPECT_FALSE(mask.IsMutatorRunning(&Mask::Parameters::x));
}

// ------------------------------------------------------------------------------

class MaskArchiveTest : public ::testing::Test {};

TEST_F(MaskArchiveTest, RebindsEachEventProperty) {
  // Two partially completed events resume against their own restored
  // coordinates.
  serialization_test::ForEachFormat([&](auto format) {
    using namespace std::chrono_literals;
    auto clock = std::make_shared<MockClock>();
    Mask source;
    source.AddMutator(Mask::ParameterMutator(
        &Mask::Parameters::x,
        FrameCounter(clock, Interpolation({0, 100}), 1000)));
    source.AddMutator(Mask::ParameterMutator(
        &Mask::Parameters::y,
        FrameCounter(clock, Interpolation({20, 60}), 2000)));
    clock->AdvanceTime(250ms);
    source.Execute();
    auto target_clock = std::make_shared<MockClock>();
    target_clock->SetElapsed(8000ms);
    Mask target;
    serialization_test::RoundTrip(source, target, format, {},
                                  {.clock = target_clock});
    target_clock->AdvanceTime(250ms);
    target.Execute();
    EXPECT_EQ(target.Param().x, 50);
    EXPECT_EQ(target.Param().y, 30);
  });
}

TEST_F(MaskArchiveTest, ListRestoresSurfaceAndPosition) {
  // A populated mask reloads its named image while retaining its position.
  serialization_test::ForEachFormat([&](auto format) {
    MaskList source(2), target;
    ImageArchiveHost loading;
    auto surface = std::make_shared<SDLSurface>(Size(2, 1));
    surface->Fill(RGBAColour(1, 2, 3, 4));
    source.At(1).Create("mask", surface);
    source.At(1).Param() = {.x = 17, .y = -9};
    serialization_test::RoundTrip(source, target, format, {},
                                  {.images = &loading.images});
    ASSERT_EQ(target.size(), 2u);
    EXPECT_FALSE(target.At(0).surface());
    EXPECT_EQ(target.At(1).filename(), "mask");
    EXPECT_EQ(target.At(1).Param().x, 17);
    EXPECT_EQ(target.At(1).Param().y, -9);
    ASSERT_TRUE(target.At(1).surface());
    EXPECT_EQ(target.At(1).surface()->GetPixelAt(Point()),
              RGBAColour(12, 34, 56, 78));
  });
}

TEST_F(MaskArchiveTest, EmptyMaskClearsPreviousSurface) {
  // A reset mask clears a populated destination without requiring asset
  // services.
  serialization_test::ForEachFormat([&](auto format) {
    Mask source, target;
    target.Create("previous", std::make_shared<SDLSurface>(Size(1, 1)));
    serialization_test::RoundTrip(source, target, format);
    EXPECT_TRUE(target.filename().empty());
    EXPECT_FALSE(target.surface());
  });
}

TEST_F(MaskArchiveTest, RejectsMismatchedEventCount) {
  // A payload with a property but no event is rejected before event rebinding.
  serialization_test::ForEachFormat([&](auto format) {
    auto bytes = serialization_test::Write(format, [](auto& ar) {
      Mask::Parameters param;
      std::string filename;
      std::vector<int> properties{0};
      std::vector<Mask::ParameterMutator> events;
      ar & param & filename;
      SerializeBoundedSequence(ar, properties);
      SerializeBoundedSequence(ar, events);
    });
    Mask target;
    EXPECT_THROW(serialization_test::Read(
                     bytes, format,
                     [&](auto& ar) {
                       boost::serialization::access::member_load(ar, target, 0);
                     }),
                 std::runtime_error);
  });
}

TEST_F(MaskArchiveTest, RejectsUnknownEventProperty) {
  // A payload with an invalid property identifier cannot construct a setter.
  serialization_test::ForEachFormat([&](auto format) {
    auto bytes = serialization_test::Write(format, [](auto& ar) {
      Mask::Parameters param;
      std::string filename;
      std::vector<int> properties{2};
      std::vector<Mask::ParameterMutator> events(1);
      ar & param & filename;
      SerializeBoundedSequence(ar, properties);
      SerializeBoundedSequence(ar, events);
    });
    Mask target;
    EXPECT_THROW(serialization_test::Read(
                     bytes, format,
                     [&](auto& ar) {
                       boost::serialization::access::member_load(ar, target, 0);
                     }),
                 std::runtime_error);
  });
}

}  // namespace
