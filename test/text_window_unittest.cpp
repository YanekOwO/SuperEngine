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

#include "core/frame_action_manager.hpp"
#include "core/object_internal/objdrawer.hpp"
#include "headless_resource_host.hpp"
#include "image_archive_test_support.hpp"
#include "serialization_test_helpers.hpp"
#include "systems/text_waku.hpp"

namespace {
class TextWindowArchiveTest : public ::testing::Test {};

TEST_F(TextWindowArchiveTest, CompletedPageRestoresWithoutRevealing) {
  // A completed hidden page retains layout and metadata without queuing a new
  // reveal.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto source = host.system->text().GetMwnd(0);
    source->SubmitText({"complete", "", 8});
    source->FlushMessageText();
    source->SetVisible(false);
    source->SetCheckpointId(37);
    source->message_state()->current_koe = 12;
    source->message_state()->pending_ruby = "ruby";
    const auto insertion = source->Layout().GetInsertionPoint();
    std::shared_ptr<TextWindow> target;
    serialization_test::RoundTrip(source, target, format, {}, host.Context());
    ASSERT_TRUE(target);
    EXPECT_EQ(target->window_number(), 0);
    EXPECT_FALSE(target->history_view());
    EXPECT_FALSE(target->IsVisible());
    EXPECT_FALSE(target->HasPendingMessageText());
    EXPECT_EQ(target->Layout().GetInsertionPoint(), insertion);
    EXPECT_EQ(target->GetPage().checkpoint_id, 37u);
    EXPECT_EQ(target->message_state()->current_koe, 12);
    EXPECT_EQ(target->message_state()->pending_ruby, "ruby");
  });
}

TEST_F(TextWindowArchiveTest, RevealingPageRestartsReveal) {
  // A saved unfinished page restores a reveal stream that can finish normally.
  serialization_test::ForEachFormat([&](auto format) {
    using namespace std::chrono_literals;
    HeadlessResourceHost host;
    host.global.message_speed = 20;
    auto source = host.system->text().GetMwnd(0);
    source->PrepareMessageText(
        [](TextWindow& window) { window.SubmitText({"unfinished", "", 10}); });
    ASSERT_TRUE(source->HasPendingMessageText());
    std::shared_ptr<TextWindow> target;
    serialization_test::RoundTrip(source, target, format, {}, host.Context());
    ASSERT_TRUE(target);
    EXPECT_TRUE(target->HasPendingMessageText());
    target->AdvanceMessageText(1000ms);
    EXPECT_FALSE(target->HasPendingMessageText());
    EXPECT_EQ(target->GetCharacterCount(), source->GetCharacterCount());
  });
}

TEST_F(TextWindowArchiveTest, SharedMessageStateRemainsAliased) {
  // A window and an external reference retain one shared message-state object.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto window = host.system->text().GetMwnd(0);
    auto source = std::pair{window, window->message_state()};
    decltype(source) target;
    serialization_test::RoundTrip(source, target, format, {}, host.Context());
    ASSERT_TRUE(target.first);
    EXPECT_EQ(target.first->message_state(), target.second);
    EXPECT_NE(target.second, source.second);
  });
}

TEST_F(TextWindowArchiveTest, ClearsMouseCaptureDuringReconstruction) {
  // Reconstructing a window advances its capture generation to invalidate
  // pending mouse input.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto source = host.system->text().GetMwnd(0);
    std::shared_ptr<TextWindow> target;
    serialization_test::RoundTrip(source, target, format, {}, host.Context());
    ASSERT_TRUE(target);
    EXPECT_GT(target->capture_generation(), 0u);
  });
}

TEST_F(TextWindowArchiveTest, MissingTextSystemRejectsConstruction) {
  // A saved window cannot construct without a destination text system.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto source = host.system->text().GetMwnd(0);
    auto bytes = serialization_test::Save(source, format);
    std::shared_ptr<TextWindow> target;
    EXPECT_THROW(serialization_test::Load(bytes, target, format),
                 std::runtime_error);
  });
}

// ------------------------------------------------------------------------------

class TextWindowFrameArchiveTest : public ::testing::Test {};

TEST_F(TextWindowFrameArchiveTest, FrameResourcesRetainSharedIdentity) {
  // Window objects, faces and external handles restore one shared
  // resource.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    auto window = host.system->text().GetMwnd(0);
    auto* frame = window->GetTextboxWaku();
    ASSERT_NE(frame, nullptr);
    auto object = std::make_shared<GraphicsObject>();
    object->Param().SetX(37);
    frame->objects_ = {object};
    frame->faces_ = {object};
    frame->face_positions_ = {Point(8, 9)};
    auto source = std::pair{window, object};
    decltype(source) target;
    serialization_test::RoundTrip(source, target, format, {}, host.Context());
    ASSERT_TRUE(target.first);
    auto* restored = target.first->GetTextboxWaku();
    ASSERT_NE(restored, nullptr);
    ASSERT_EQ(restored->objects_.size(), 1u);
    ASSERT_EQ(restored->faces_.size(), 1u);
    EXPECT_EQ(restored->objects_[0], target.second);
    EXPECT_EQ(restored->faces_[0], target.second);
    EXPECT_EQ(restored->face_positions_, (std::vector<Point>{Point(8, 9)}));
    EXPECT_EQ(target.second->Param().x(), 37);
  });
}

// ------------------------------------------------------------------------------

class TextWindowButtonArchiveTest : public ::testing::Test {
 protected:
  void Configure(HeadlessResourceHost& host) {
    auto& text = host.system->text();
    auto& window = text.mwnd_config_.windows_.at(0);
    window.name_mod = 1;
    window.namebox.has_waku = true;
    window.namebox.waku_set = 0;
    auto& frame = text.mwnd_config_.wakus_.at({0, 0});
    frame.button_count = 1;
    MwndConfig::Button button;
    button.file = "button";
    button.slot_id = 0;
    button.cut_no = 3;
    frame.buttons = {button};
    auto assets = std::make_shared<ImageArchiveHost>();
    text.BindGraphics(
        {.images = std::shared_ptr<ImageResources>(assets, &assets->images)});
  }
};

TEST_F(TextWindowButtonArchiveTest, ButtonChangesDoNotAffectArchive) {
  // Both frames' button lists and visibility are omitted from saved bytes.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    Configure(host);
    auto window = host.system->text().GetMwnd(0);
    auto original = serialization_test::Save(window, format);
    for (auto* frame : {window->GetTextboxWaku(), window->GetNameboxWaku()}) {
      ASSERT_TRUE(frame);
      frame->button_objects_[0]->Param().SetX(91);
      frame->button_objects_[0]->GetFrameAction()->GetCounter()->Set(999);
      frame->button_objects_.push_back(std::make_shared<GraphicsObject>());
      frame->button_visible_ = {false, false};
    }
    EXPECT_EQ(serialization_test::Save(window, format), original);
  });
}

TEST_F(TextWindowButtonArchiveTest, RebuildsButtonsFromDestinationTemplates) {
  // Template edits apply on loading; saved button mutations are discarded.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    Configure(host);
    auto& text = host.system->text();
    auto window = text.GetMwnd(0);
    window->GetTextboxWaku()->button_objects_[0]->Param().SetX(91);
    window->GetNameboxWaku()->button_objects_[0]->Param().SetX(92);
    auto bytes = serialization_test::Save(window, format);
    text.mwnd_config_.wakus_.at({0, 0}).buttons[0].cut_no = 7;
    std::shared_ptr<TextWindow> restored;
    serialization_test::Load(bytes, restored, format, host.Context());
    for (auto* frame :
         {restored->GetTextboxWaku(), restored->GetNameboxWaku()}) {
      ASSERT_TRUE(frame);
      ASSERT_EQ(frame->button_objects_.size(), 1u);
      ASSERT_TRUE(frame->button_objects_[0]);
      EXPECT_EQ(frame->button_objects_[0]->Param().x(), 0);
      EXPECT_EQ(frame->button_objects_[0]->Param().button.cut_no, 7);
    }
  });
}

TEST_F(TextWindowButtonArchiveTest, ResetsCachedButtonVisibility) {
  // Loading rebuilds the visibility cache instead of retaining hidden buttons.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    Configure(host);
    auto window = host.system->text().GetMwnd(0);
    window->GetTextboxWaku()->button_visible_ = {false};
    window->GetNameboxWaku()->button_visible_ = {false};
    std::shared_ptr<TextWindow> restored;
    serialization_test::RoundTrip(window, restored, format, {}, host.Context());
    EXPECT_EQ(restored->GetTextboxWaku()->button_visible_,
              (std::vector<bool>{true}));
    EXPECT_EQ(restored->GetNameboxWaku()->button_visible_,
              (std::vector<bool>{true}));
  });
}

TEST_F(TextWindowButtonArchiveTest, ReplacesButtonsInExistingWindow) {
  // Loading into an existing window must not copy its old buttons forward.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    Configure(host);
    auto window = host.system->text().GetMwnd(0);
    auto bytes = serialization_test::Save(*window, format);
    auto old_text = window->GetTextboxWaku()->button_objects_[0];
    auto old_name = window->GetNameboxWaku()->button_objects_[0];
    old_text->Param().SetX(91);
    old_name->Param().SetX(92);
    serialization_test::Load(bytes, *window, format, host.Context());
    EXPECT_NE(window->GetTextboxWaku()->button_objects_[0], old_text);
    EXPECT_NE(window->GetNameboxWaku()->button_objects_[0], old_name);
    EXPECT_EQ(window->GetTextboxWaku()->button_objects_[0]->Param().x(), 0);
    EXPECT_EQ(window->GetNameboxWaku()->button_objects_[0]->Param().x(), 0);
  });
}

TEST_F(TextWindowButtonArchiveTest, RestartsConfiguredActionsWithoutDispatch) {
  // Reconstructed buttons start their configured actions at zero, without
  // invoking either the saved action's final callback or the new action.
  serialization_test::ForEachFormat([&](auto format) {
    HeadlessResourceHost host;
    Configure(host);
    auto& text = host.system->text();
    text.mwnd_config_.wakus_.at({0, 0}).buttons[0].frame_action =
        MwndConfig::CallTarget{};
    FrameActionManager actions(host.stage, host.local, 0);
    int dispatched = 0;
    actions.SetExec([&](const auto&, auto) { ++dispatched; });
    text.SetMwndFrameActionHandler([&](auto object, const auto&) {
      actions.Start({object->GetFrameAction(), object}, -1, false, {});
    });
    auto window = text.GetMwnd(0);
    auto bytes = serialization_test::Save(*window, format);
    for (auto* frame : {window->GetTextboxWaku(), window->GetNameboxWaku()})
      frame->button_objects_[0]->GetFrameAction()->GetCounter()->Set(999);
    serialization_test::Load(bytes, *window, format, host.Context());
    for (auto* frame : {window->GetTextboxWaku(), window->GetNameboxWaku()}) {
      auto action = frame->button_objects_[0]->GetFrameAction();
      EXPECT_TRUE(action->IsActive());
      EXPECT_EQ(action->GetCounter()->Get(), 0);
    }
    EXPECT_EQ(dispatched, 0);
  });
}

}  // namespace
