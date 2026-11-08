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

#include "headless_resource_host.hpp"
#include "image_archive_test_support.hpp"
#include "serialization_test_helpers.hpp"

namespace {
using namespace std::chrono_literals;
class TextSystemArchiveTest : public ::testing::Test {
 protected:
  void RoundTrip(HeadlessResourceHost& host,
                 serialization_test::Format format) {
    auto& text = host.system->text();
    auto bytes = serialization_test::Save(text, format);
    text.Reset();
    text.set_active_window(0);
    text.SetKeyCursor(-1);
    serialization_test::Load(bytes, text, format, host.Context());
  }

  void AddPage(TextSystem& text, std::uint64_t id) {
    auto window = text.GetMwnd(0);
    window->SubmitText({"page", "", 4});
    window->FlushMessageText();
    window->SetCheckpointId(id);
    text.Snapshot();
    text.NewPageOnWindow(0);
  }

  std::string InvalidState(serialization_test::Format format,
                           int cursor,
                           std::size_t history_index) {
    return serialization_test::Write(format, [&](auto& ar) {
      int active_window = 0;
      bool flag = false;
      std::chrono::milliseconds time{};
      TextSystem::WindowList windows;
      TextSystem::PageSet pages;
      std::list<TextSystem::PageSet> previous;
      std::map<int, bool> overrides;
      std::shared_ptr<TextWindow> selected;
      ar & active_window & cursor & flag & flag & flag & flag & flag & flag &
          flag & flag & time & windows & pages & previous & history_index &
          overrides & selected;
    });
  }
};

TEST_F(TextSystemArchiveTest, SavesCurrentWindow) {
  // Changing the active window after an earlier save affects the next save.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto& text = host.system->text();
    (void)serialization_test::Save(text, format);
    text.set_active_window(7);
    RoundTrip(host, format);
    EXPECT_EQ(text.active_window(), 7);
  });
}

TEST_F(TextSystemArchiveTest, SavesCurrentCursor) {
  // Changing the cursor after an earlier save affects the next save.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto& text = host.system->text();
    (void)serialization_test::Save(text, format);
    text.SetKeyCursor(3);
    RoundTrip(host, format);
    EXPECT_EQ(text.GetCursorNumber(), 3);
  });
}

TEST_F(TextSystemArchiveTest, RestoresVisibility) {
  // A hidden text system remains hidden after a direct archive round trip.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    host.system->text().set_system_visible(false);
    RoundTrip(host, format);
    EXPECT_FALSE(host.system->text().system_visible());
  });
}

TEST_F(TextSystemArchiveTest, RestoresSkipMode) {
  // Read text with active skip resumes skipping after restoration.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto& text = host.system->text();
    text.SetKidokuRead(true);
    text.SetSkipMode(true);
    RoundTrip(host, format);
    EXPECT_TRUE(text.CurrentlySkipping());
  });
}

TEST_F(TextSystemArchiveTest, RestoresAutoSkipPreference) {
  // An enabled auto-skip preference survives restoration and activates skip.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto& text = host.system->text();
    text.SetKidokuRead(true);
    text.SetAutoSkipMode(true);
    RoundTrip(host, format);
    text.ExecuteTextSystem();
    EXPECT_TRUE(text.auto_skip_mode());
    EXPECT_TRUE(text.CurrentlySkipping());
  });
}

TEST_F(TextSystemArchiveTest, RestoresAutoSkipSuspension) {
  // Unread text suspends auto-skip; loading must not restart it on read text.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto& text = host.system->text();
    text.SetAutoSkipMode(true);
    text.SetKidokuRead(false);
    text.SetKidokuRead(true);
    RoundTrip(host, format);
    text.ExecuteTextSystem();
    EXPECT_TRUE(text.auto_skip_mode());
    EXPECT_FALSE(text.skip_mode());
  });
}

TEST_F(TextSystemArchiveTest, RestoresSelectionMode) {
  // Selection mode prevents a click from cancelling saved skip mode.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto& text = host.system->text();
    text.SetKidokuRead(true);
    text.SetSkipMode(true);
    text.set_in_selection_mode(true);
    RoundTrip(host, format);
    text.OnEvent(std::make_shared<Event>(KeyDown{KeyCode::SPACE}));
    EXPECT_TRUE(text.skip_mode());
  });
}

TEST_F(TextSystemArchiveTest, RestoresPauseMode) {
  // A paused message accepts history navigation immediately after loading.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto& text = host.system->text();
    AddPage(text, 37);
    text.set_in_pause_state(true);
    RoundTrip(host, format);
    text.OnEvent(std::make_shared<Event>(KeyDown{KeyCode::UP}));
    EXPECT_TRUE(text.IsReadingBacklog());
  });
}

TEST_F(TextSystemArchiveTest, RestoresMessageTime) {
  // Saved elapsed message time resumes from its previous value.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto& text = host.system->text();
    text.AdvanceMessageText(123ms);
    RoundTrip(host, format);
    text.AdvanceMessageText(7ms);
    EXPECT_EQ(text.MessageTime(), 130ms);
  });
}

TEST_F(TextSystemArchiveTest, RestoresVisualOverrides) {
  // A per-window visibility override survives loading independently of modes.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    host.system->text().SetVisualOverride(0, false);
    RoundTrip(host, format);
    EXPECT_FALSE(host.system->text().ShowWindow(0));
  });
}

TEST_F(TextSystemArchiveTest, RestoresHistoryNavigationPosition) {
  // Saving on an older page retains its position and the next history page.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto& text = host.system->text();
    AddPage(text, 37);
    AddPage(text, 73);
    text.BackPage();
    text.BackPage();
    RoundTrip(host, format);
    ASSERT_TRUE(text.IsReadingBacklog());
    ASSERT_TRUE(text.GetDisplayedWindows(kLayerFg).at(0));
    EXPECT_EQ(text.GetDisplayedWindows(kLayerFg)[0]->GetPage().checkpoint_id,
              37u);
    text.ForwardPage();
    EXPECT_EQ(text.GetDisplayedWindows(kLayerFg)[0]->GetPage().checkpoint_id,
              73u);
  });
}

TEST_F(TextSystemArchiveTest, RestoresCurrentPageAfterHistory) {
  // Returning past the last history entry restores the saved current page.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto& text = host.system->text();
    AddPage(text, 37);
    text.GetMwnd(0)->SubmitText({"current", "", 7});
    text.GetMwnd(0)->SetCheckpointId(99);
    text.BackPage();
    RoundTrip(host, format);
    text.ForwardPage();
    EXPECT_EQ(text.GetDisplayedWindows(kLayerFg).at(0)->GetPage().checkpoint_id,
              99u);
  });
}

TEST_F(TextSystemArchiveTest, RetainsSelectedHistoryWindowAlias) {
  // History, the selected window, and an external handle share one allocation.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto& text = host.system->text();
    AddPage(text, 37);
    text.BackPage();
    auto window = text.GetDisplayedWindows(kLayerFg).at(0);
    text.SetCurrentMwnd(window);
    auto bytes = serialization_test::Write(
        format, [&](auto& ar) { ar << text << window; });
    text.Reset();
    std::shared_ptr<TextWindow> restored;
    serialization_test::Read(
        bytes, format, [&](auto& ar) { ar >> text >> restored; },
        host.Context());
    EXPECT_EQ(text.GetCurrentMwnd(), restored);
    EXPECT_EQ(text.GetDisplayedWindows(kLayerFg).at(0), restored);
  });
}

TEST_F(TextSystemArchiveTest, DeferredLoadLeavesLiveModesUnchanged) {
  // A deferred load installs visibility only when its receiver commits it.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto& text = host.system->text();
    text.set_system_visible(false);
    auto bytes = serialization_test::Save(text, format);
    text.set_system_visible(true);
    auto context = host.Context();
    SerializationContext::Commit commit;
    context.defer_commit = [&](auto install) { commit = std::move(install); };
    serialization_test::Load(bytes, text, format, context);
    ASSERT_TRUE(commit);
    EXPECT_TRUE(text.system_visible());
    commit();
    EXPECT_FALSE(text.system_visible());
  });
}

TEST_F(TextSystemArchiveTest, ClearsTransientInputCapture) {
  // A live window capture is discarded when text state is installed.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto& text = host.system->text();
    auto bytes = serialization_test::Save(text, format);
    text.mwnd_capture_window_ = text.GetMwnd(0);
    serialization_test::Load(bytes, text, format, host.Context());
    EXPECT_FALSE(text.mwnd_capture_window_);
  });
}

TEST_F(TextSystemArchiveTest, RejectsHistoryPositionPastEnd) {
  // An out-of-range history position rejects loading before visibility changes.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto bytes = InvalidState(format, -1, 1);
    EXPECT_THROW(serialization_test::Read(
                     bytes, format,
                     [&](auto& ar) {
                       boost::serialization::access::member_load(
                           ar, host.system->text(), 0);
                     },
                     host.Context()),
                 std::runtime_error);
    EXPECT_TRUE(host.system->text().system_visible());
  });
}

TEST_F(TextSystemArchiveTest, RejectsInvalidCursor) {
  // An invalid cursor rejects loading before any decoded text state is applied.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto bytes = InvalidState(format, -2, 0);
    EXPECT_THROW(serialization_test::Read(
                     bytes, format,
                     [&](auto& ar) {
                       boost::serialization::access::member_load(
                           ar, host.system->text(), 0);
                     },
                     host.Context()),
                 std::runtime_error);
    EXPECT_TRUE(host.system->text().system_visible());
  });
}

TEST_F(TextSystemArchiveTest, TruncatedArchiveLeavesLiveModesUnchanged) {
  // A truncated text archive must fail without replacing the live visibility.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto& text = host.system->text();
    text.set_system_visible(false);
    auto bytes = serialization_test::Save(text, format);
    bytes.resize(bytes.size() / 2);
    text.set_system_visible(true);
    EXPECT_THROW(serialization_test::Load(bytes, text, format, host.Context()),
                 std::exception);
    EXPECT_TRUE(text.system_visible());
  });
}

TEST_F(TextSystemArchiveTest, MissingCursorAssetLeavesLiveStateUnchanged) {
  // A cursor that cannot be reconstructed must not install decoded visibility
  // or replace the live cursor.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto& text = host.system->text();
    text.SetKeyCursor(3);
    text.set_system_visible(false);
    auto bytes = serialization_test::Save(text, format);
    text.SetKeyCursor(5);
    text.set_system_visible(true);
    host.gameexe.parseLine("CURSOR.003.NAME = \"missing\"");
    auto assets = std::make_shared<ImageArchiveHost>();
    assets->fail = true;
    text.BindGraphics(
        {.images = std::shared_ptr<ImageResources>(assets, &assets->images)});
    EXPECT_THROW(serialization_test::Load(bytes, text, format, host.Context()),
                 std::runtime_error);
    EXPECT_EQ(text.GetCursorNumber(), 5);
    EXPECT_TRUE(text.system_visible());
  });
}

}  // namespace
