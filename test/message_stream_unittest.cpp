// Copyright (C) 2026 RLVM contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/message_stream.hpp"

#include "systems/sdl/sdl_surface.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::StrictMock;

MessageStream::Glyph Glyph(std::string text) {
  return {std::move(text), {}, RGBColour::White(), std::nullopt, Point(0, 0)};
}

class MessageStreamTest : public ::testing::Test {
 protected:
  auto Record() {
    return [this](const MessageStream::Entry& entry) {
      if (const auto* glyph = std::get_if<MessageStream::Glyph>(&entry))
        revealed_.push_back(glyph->text);
      else
        revealed_.push_back("ruby");
    };
  }

  MessageStream stream_;
  std::vector<std::string> revealed_;
};

TEST_F(MessageStreamTest, StartsEmptyAndAcceptsFirstGlyph) {
  EXPECT_TRUE(stream_.Empty());
  EXPECT_FALSE(stream_.Advance(100ms, 10ms, Record()));

  stream_.Enqueue(Glyph("a"));
  EXPECT_FALSE(stream_.Empty());
  EXPECT_FALSE(stream_.Advance(9ms, 10ms, Record()));
  EXPECT_TRUE(revealed_.empty());
  EXPECT_TRUE(stream_.Advance(1ms, 10ms, Record()));
  EXPECT_THAT(revealed_, ElementsAre("a"));
  EXPECT_TRUE(stream_.Empty());
  EXPECT_FALSE(stream_.Advance(10ms, 10ms, Record()));
}

TEST_F(MessageStreamTest, AccumulatesTimeAndRevealsMultipleGlyphsInOrder) {
  stream_.Enqueue(Glyph("a"));
  stream_.Enqueue(Glyph("b"));
  stream_.Enqueue(Glyph("c"));

  EXPECT_TRUE(stream_.Advance(25ms, 10ms, Record()));
  EXPECT_THAT(revealed_, ElementsAre("a", "b"));
  EXPECT_FALSE(stream_.Empty());
  EXPECT_FALSE(stream_.Advance(4ms, 10ms, Record()));
  EXPECT_TRUE(stream_.Advance(1ms, 10ms, Record()));
  EXPECT_THAT(revealed_, ElementsAre("a", "b", "c"));
  EXPECT_TRUE(stream_.Empty());
}

TEST_F(MessageStreamTest, StreamsHaveIndependentElapsedTime) {
  stream_.Enqueue(Glyph("a"));
  stream_.Enqueue(Glyph("b"));
  EXPECT_FALSE(stream_.Advance(6ms, 10ms, Record()));

  stream_.NextStream();
  stream_.Enqueue(Glyph("x"));
  stream_.Enqueue(Glyph("y"));
  EXPECT_TRUE(stream_.Advance(4ms, 10ms, Record()));
  EXPECT_THAT(revealed_, ElementsAre("a"));
  EXPECT_TRUE(stream_.Advance(6ms, 10ms, Record()));
  EXPECT_THAT(revealed_, ElementsAre("a", "x"));
  EXPECT_TRUE(stream_.Advance(4ms, 10ms, Record()));
  EXPECT_THAT(revealed_, ElementsAre("a", "x", "b"));
  EXPECT_FALSE(stream_.Empty());
  EXPECT_TRUE(stream_.Advance(6ms, 10ms, Record()));
  EXPECT_THAT(revealed_, ElementsAre("a", "x", "b", "y"));
  EXPECT_TRUE(stream_.Empty());
}

TEST_F(MessageStreamTest, RubyKeepsEntryOrderWithoutConsumingCharacterTime) {
  stream_.Enqueue(MessageStream::Ruby{});
  stream_.Enqueue(Glyph("a"));
  stream_.Enqueue(MessageStream::Ruby{});
  stream_.Enqueue(Glyph("b"));

  EXPECT_TRUE(stream_.Advance(0ms, 10ms, Record()));
  EXPECT_THAT(revealed_, ElementsAre("ruby"));
  EXPECT_FALSE(stream_.Advance(9ms, 10ms, Record()));
  EXPECT_TRUE(stream_.Advance(1ms, 10ms, Record()));
  EXPECT_THAT(revealed_, ElementsAre("ruby", "a", "ruby"));
  EXPECT_TRUE(stream_.Advance(10ms, 10ms, Record()));
  EXPECT_THAT(revealed_, ElementsAre("ruby", "a", "ruby", "b"));
  EXPECT_TRUE(stream_.Empty());
}

TEST_F(MessageStreamTest, ZeroDelayFlushesEveryStream) {
  stream_.Enqueue(Glyph("a"));
  stream_.Enqueue(MessageStream::Ruby{});
  stream_.NextStream();
  stream_.Enqueue(Glyph("b"));
  stream_.Enqueue(Glyph("c"));
  stream_.NextStream();

  EXPECT_TRUE(stream_.Advance(0ms, 0ms, Record()));
  EXPECT_THAT(revealed_, ElementsAre("a", "ruby", "b", "c"));
  EXPECT_TRUE(stream_.Empty());
}

TEST_F(MessageStreamTest, NegativeDurationsAreClampedToZero) {
  stream_.Enqueue(Glyph("a"));
  stream_.Enqueue(Glyph("b"));

  EXPECT_FALSE(stream_.Advance(5ms, 10ms, Record()));
  EXPECT_FALSE(stream_.Advance(-20ms, 10ms, Record()));
  EXPECT_TRUE(stream_.Advance(5ms, 10ms, Record()));
  EXPECT_THAT(revealed_, ElementsAre("a"));
  EXPECT_TRUE(stream_.Advance(-1ms, -10ms, Record()));
  EXPECT_THAT(revealed_, ElementsAre("a", "b"));
  EXPECT_TRUE(stream_.Empty());
}

TEST_F(MessageStreamTest, EnqueueAfterDrainingResetsElapsedTime) {
  stream_.Enqueue(Glyph("a"));
  EXPECT_TRUE(stream_.Advance(100ms, 10ms, Record()));
  EXPECT_TRUE(stream_.Empty());
  EXPECT_FALSE(stream_.Advance(100ms, 10ms, Record()));

  stream_.Enqueue(Glyph("b"));
  EXPECT_FALSE(stream_.Advance(9ms, 10ms, Record()));
  EXPECT_THAT(revealed_, ElementsAre("a"));
  EXPECT_TRUE(stream_.Advance(1ms, 10ms, Record()));
  EXPECT_THAT(revealed_, ElementsAre("a", "b"));
}

TEST_F(MessageStreamTest,
       CopiedStatePreservesProgressAndAdvancesIndependently) {
  MessageStreamState original;
  original.deferring = false;
  original.first_stream_chars = 7;
  original.stream.Enqueue(Glyph("a"));
  original.stream.Enqueue(Glyph("b"));
  EXPECT_TRUE(original.stream.Advance(15ms, 10ms, Record()));
  original.stream.NextStream();
  original.stream.Enqueue(Glyph("x"));

  auto copy = original;
  EXPECT_FALSE(copy.deferring);
  EXPECT_EQ(copy.first_stream_chars, 7);
  EXPECT_TRUE(copy.stream.Advance(5ms, 10ms, Record()));
  EXPECT_THAT(revealed_, ElementsAre("a", "b"));
  EXPECT_TRUE(copy.stream.Advance(5ms, 10ms, Record()));
  EXPECT_TRUE(copy.stream.Empty());
  EXPECT_FALSE(original.stream.Empty());

  revealed_.clear();
  EXPECT_FALSE(original.stream.Advance(4ms, 10ms, Record()));
  EXPECT_TRUE(original.stream.Advance(1ms, 10ms, Record()));
  EXPECT_THAT(revealed_, ElementsAre("b"));
  EXPECT_TRUE(original.stream.Advance(5ms, 10ms, Record()));
  EXPECT_THAT(revealed_, ElementsAre("b", "x"));
  EXPECT_TRUE(original.stream.Empty());
}

class MockMessageFont : public IFont {
 public:
  MOCK_METHOD(int, GetCharWidth, (uint16_t), (override));
  MOCK_METHOD(int, GetSize, (), (const, override));
};

class MockMessageRenderer : public ITextSystem {
 public:
  MOCK_METHOD(void, InitSystem, (), (override));
  MOCK_METHOD(void, QuitSystem, (), (override));
  MOCK_METHOD(std::shared_ptr<IFont>,
              GetFont,
              (const std::filesystem::path&, int, bool, int),
              (override));
  MOCK_METHOD(Size,
              RenderGlyphOnto,
              (const std::string&,
               FontFace,
               RGBColour,
               std::optional<RGBColour>,
               Point,
               std::shared_ptr<SDLSurface>),
              (override));
  MOCK_METHOD(std::shared_ptr<SDLSurface>,
              RenderText,
              (const std::string&, FontFace, RGBColour),
              (override));
};

TEST(MessageStreamDrawTest, ForwardsGlyphAttributesAndDestination) {
  StrictMock<MockMessageRenderer> renderer;
  const auto destination = std::make_shared<SDLSurface>(Size(8, 8));
  const MessageStream::Glyph glyph{
      "a",
      {.font = std::make_shared<StrictMock<MockMessageFont>>(),
       .is_italic = true,
       .is_shadow = false,
       .is_futidoru = true,
       .baseline_from_top = 17},
      RGBColour(12, 34, 56),
      RGBColour(65, 43, 21),
      Point(2, 3)};
  EXPECT_CALL(renderer,
              RenderGlyphOnto(glyph.text, _, glyph.colour, glyph.shadow,
                              glyph.position, destination))
      .WillOnce([&](const std::string&, FontFace font, RGBColour,
                    std::optional<RGBColour>, Point,
                    std::shared_ptr<SDLSurface>) {
        EXPECT_EQ(font.font, glyph.font.font);
        EXPECT_EQ(font.is_italic, glyph.font.is_italic);
        EXPECT_EQ(font.is_shadow, glyph.font.is_shadow);
        EXPECT_EQ(font.is_futidoru, glyph.font.is_futidoru);
        EXPECT_EQ(font.baseline_from_top, glyph.font.baseline_from_top);
        return Size(1, 1);
      });

  MessageStreamState::Draw(renderer, destination, glyph);
}

TEST(MessageStreamDrawTest, CopiedRubyEntriesDrawOntoIndependentDestinations) {
  StrictMock<MockMessageRenderer> renderer;
  const auto ruby = std::make_shared<SDLSurface>(Size(2, 1));
  const RGBAColour first(200, 10, 20, 255);
  const RGBAColour second(30, 210, 40, 255);
  ruby->Fill(first);
  ruby->Fill(second, Rect::REC(1, 0, 1, 1));

  const auto destination = std::make_shared<SDLSurface>(Size(6, 5));
  destination->Fill(RGBAColour::Clear());
  const auto copy_destination = destination->Clone();
  MessageStreamState original;
  original.stream.Enqueue(MessageStream::Ruby{ruby, Rect::REC(2, 3, 2, 1)});
  auto copy = original;

  EXPECT_TRUE(copy.stream.Advance(0ms, 10ms, [&](const auto& entry) {
    EXPECT_EQ(std::get<MessageStream::Ruby>(entry).surface, ruby);
    MessageStreamState::Draw(renderer, copy_destination, entry);
  }));
  EXPECT_TRUE(copy.stream.Empty());
  EXPECT_FALSE(original.stream.Empty());
  EXPECT_EQ(copy_destination->GetPixelAt(Point(2, 3)), first);
  EXPECT_EQ(copy_destination->GetPixelAt(Point(3, 3)), second);
  EXPECT_EQ(copy_destination->GetPixelAt(Point(1, 3)), RGBAColour::Clear());
  EXPECT_EQ(destination->GetPixelAt(Point(2, 3)), RGBAColour::Clear());

  EXPECT_TRUE(original.stream.Advance(0ms, 10ms, [&](const auto& entry) {
    MessageStreamState::Draw(renderer, destination, entry);
  }));
  EXPECT_EQ(destination->GetPixelAt(Point(2, 3)), first);
  EXPECT_EQ(destination->GetPixelAt(Point(3, 3)), second);
}

}  // namespace
