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

#include "core/message_page.hpp"
#include "headless_resource_host.hpp"
#include "serialization_test_helpers.hpp"

namespace {

class MessagePageArchiveTest : public ::testing::Test {};

TEST_F(MessagePageArchiveTest, RetainsPartialSubmissionAndCheckpoint) {
  // A page preserves its full text lookahead, accepted prefix, initial styling
  // and rollback identity.
  serialization_test::ForEachFormat([&](auto format) {
    MessagePage source, target;
    source.checkpoint_id = 713;
    source.number_of_chars_on_page = 2;
    source.initial_state.name = "speaker";
    source.initial_state.default_font_size = 18;
    source.initial_state.default_color = RGBColour(1, 2, 3);
    source.initial_state.color = RGBColour(4, 5, 6);
    source.initial_state.next_char_italic = true;
    source.initial_state.faces.push_back({"face", 2});
    source.history = {msgcmd::Text{"abcdef", "next", 2}, msgcmd::KoeMarker{17}};
    serialization_test::RoundTrip(source, target, format);
    EXPECT_EQ(target.checkpoint_id, 713u);
    EXPECT_EQ(target.number_of_chars_on_page, 2);
    EXPECT_EQ(target.initial_state.name, "speaker");
    EXPECT_EQ(target.initial_state.default_font_size, 18);
    EXPECT_EQ(target.initial_state.default_color, RGBColour(1, 2, 3));
    EXPECT_EQ(target.initial_state.color, RGBColour(4, 5, 6));
    EXPECT_TRUE(target.initial_state.next_char_italic);
    ASSERT_EQ(target.initial_state.faces.size(), 1u);
    EXPECT_EQ(target.initial_state.faces[0].filename, "face");
    EXPECT_EQ(target.initial_state.faces[0].index, 2);
    ASSERT_EQ(target.history.size(), 2u);
    const auto& text = std::get<msgcmd::Text>(target.history[0]);
    EXPECT_EQ(text.text, "abcdef");
    EXPECT_EQ(text.final_lookahead, "next");
    EXPECT_EQ(text.accepted_bytes, 2u);
    EXPECT_EQ(std::get<msgcmd::KoeMarker>(target.history[1]).id, 17);
  });
}

TEST_F(MessagePageArchiveTest, RestoredCommandsReplaySameLayout) {
  // Recorded formatting and text commands reproduce the same page layout after
  // an archive round trip.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto window = host.system->text().GetMwnd(0);
    window->Submit(msgcmd::SetNameMode{2});
    window->Submit(msgcmd::NameWithoutDisplay{"speaker"});
    window->Submit(msgcmd::SetFontSize{18});
    window->Submit(msgcmd::SetFontColor{RGBColour(100, 120, 140)});
    window->Submit(msgcmd::SetInsertion{Point(10, 5)});
    window->Submit(msgcmd::SetIndentation{});
    window->SubmitText({"AB", "", 2});
    window->Submit(msgcmd::HardBreak{});
    window->Submit(msgcmd::SetGlyphRenderOffset{Point(2, 3)});
    window->SubmitText({"CD", "", 2});
    window->FlushMessageText();
    auto source = window->GetPage();
    const auto insertion = window->Layout().GetInsertionPoint();
    MessagePage target;
    serialization_test::RoundTrip(source, target, format);
    window->ClearWin();
    window->Restore(target);
    EXPECT_EQ(window->Layout().GetInsertionPoint(), insertion);
    EXPECT_EQ(window->Layout().GetFontSize(), 18);
    EXPECT_EQ(window->Layout().GetIndentation(), 10);
    EXPECT_EQ(window->Layout().GetGlyphRenderOffset(), Point(2, 3));
    EXPECT_EQ(window->GetPage().history.size(), source.history.size());
  });
}

// ------------------------------------------------------------------------------

class MessageCommandArchiveTest : public ::testing::Test {};

TEST_F(MessageCommandArchiveTest, RetainsCommandAlternativesAndPayloads) {
  // Recorded name, ruby, face and control commands retain their alternatives
  // and replay data.
  serialization_test::ForEachFormat([&](auto format) {
    MessagePage source, target;
    auto layout = TextLayout::FromCells({.font_size = 18}, Size(12, 3));
    source.history = {msgcmd::Name{"speaker", "!"},
                      msgcmd::NameWithoutDisplay{"hidden"},
                      msgcmd::SetNameMode{2},
                      msgcmd::NextMessage{layout, RGBColour(1, 2, 3)},
                      msgcmd::RubyText{"annotation"},
                      msgcmd::FaceOpen{"portrait", 3},
                      msgcmd::FaceClose{2},
                      msgcmd::SetDefaultTextColor{RGBColour(4, 5, 6)},
                      msgcmd::SetFontColor{RGBColour(7, 8, 9)},
                      msgcmd::SetFontSize{23},
                      msgcmd::SetInsertion{Point(11, 12)},
                      msgcmd::SetGlyphRenderOffset{Point(13, 14)},
                      msgcmd::HardBreak{},
                      msgcmd::SetIndentation{},
                      msgcmd::ClearIndentation{},
                      msgcmd::RubyBegin{},
                      msgcmd::MarkItalic{}};
    serialization_test::RoundTrip(source, target, format);
    ASSERT_EQ(target.history.size(), source.history.size());
    for (std::size_t i = 0; i < source.history.size(); ++i)
      ASSERT_EQ(target.history[i].index(), source.history[i].index());
    EXPECT_EQ(std::get<msgcmd::Name>(target.history[0]).name, "speaker");
    EXPECT_EQ(std::get<msgcmd::Name>(target.history[0]).next_char, "!");
    EXPECT_EQ(std::get<msgcmd::NameWithoutDisplay>(target.history[1]).name,
              "hidden");
    EXPECT_EQ(std::get<msgcmd::SetNameMode>(target.history[2]).mode, 2);
    EXPECT_EQ(
        std::get<msgcmd::NextMessage>(target.history[3]).layout.GetFontSize(),
        18);
    EXPECT_EQ(std::get<msgcmd::NextMessage>(target.history[3]).color,
              RGBColour(1, 2, 3));
    EXPECT_EQ(std::get<msgcmd::RubyText>(target.history[4]).ruby, "annotation");
    EXPECT_EQ(std::get<msgcmd::FaceOpen>(target.history[5]).filename,
              "portrait");
    EXPECT_EQ(std::get<msgcmd::FaceOpen>(target.history[5]).index, 3);
    EXPECT_EQ(std::get<msgcmd::FaceClose>(target.history[6]).index, 2);
    EXPECT_EQ(std::get<msgcmd::SetDefaultTextColor>(target.history[7]).color,
              RGBColour(4, 5, 6));
    EXPECT_EQ(std::get<msgcmd::SetFontColor>(target.history[8]).color,
              RGBColour(7, 8, 9));
    EXPECT_EQ(std::get<msgcmd::SetFontSize>(target.history[9]).size, 23);
    EXPECT_EQ(std::get<msgcmd::SetInsertion>(target.history[10]).point,
              Point(11, 12));
    EXPECT_EQ(std::get<msgcmd::SetGlyphRenderOffset>(target.history[11]).offset,
              Point(13, 14));
  });
}

}  // namespace
