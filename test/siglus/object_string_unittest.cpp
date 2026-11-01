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

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "core/object_internal/drawer/string.hpp"
#include "libsiglus/bindings/frame_action.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/util.hpp"
#include "vm/exception.hpp"
#include "vm/string.hpp"

namespace libsiglus::binding {

class SiglusObjectStringTest : public ::testing::Test {
 protected:
  SiglusMachine runtime;

  void SetUp() override {
    runtime.vm = std::make_unique<serilang::VM>(
        std::make_shared<serilang::GarbageCollector>());
    runtime.stage = std::make_unique<Stage>(3);
    runtime.gameexe = std::make_shared<Gameexe>();
    runtime.gameexe->SetIntAt("MWND.MOJI_COLOR", 0);
    runtime.gameexe->SetIntAt("MWND.SHADOW_COLOR", 1);
    runtime.gameexe->SetIntAt("MWND.FUCHI_COLOR", 1);
    runtime.counter_list = std::make_shared<CounterList>(0);
    runtime.frame_action_manager = std::make_shared<FrameActionManager>(
        *runtime.vm, *runtime.stage, runtime.local_config, 0);
    for (const auto* key :
         {"0_memory", "1_counter", "1_frame_action", "1_object"}) {
      auto* bind = SiglusBindingRegistry::Find(key);
      ASSERT_NE(bind, nullptr);
      (*bind)(runtime);
    }

    Run("parent = Object(); parent.child.resize(1); label = parent.child[0];");
  }

  inline void Run(const std::string& code) { Execute(*runtime.vm, code); }

  inline GraphicsObject& Object(int index = 0) {
    return *runtime.stage->objects.Get(kLayerFg, index);
  }

  inline GraphicsObject& Label() { return *Object().GetChild(0); }

  void ExpectReadback(std::string_view expected) {
    Run("actual_text = label.get_string(); width = label.get_size_x(); "
        "height = label.get_size_y();");
    const auto& globals = *runtime.vm->globals_;
    const auto* text = globals.at("actual_text").Get_if<serilang::String>();
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->str_, expected);
    EXPECT_EQ(globals.at("width"), serilang::Value(0));
    EXPECT_EQ(globals.at("height"), serilang::Value(0));
  }

  void ExpectRetainedChild() {
    ASSERT_TRUE(Label().HasChildren());
    const auto* child = Label().GetChild(0);
    ASSERT_NE(child, nullptr);
    ASSERT_NE(child->GetDrawer<StringDrawer>(), nullptr);
    EXPECT_EQ(child->GetDrawer<StringDrawer>()->text(), "child");
    EXPECT_EQ(child->Param().x(), 3);
    EXPECT_EQ(child->Param().y(), 4);
  }

  static void ExpectGlyphs(const std::vector<StringObjectGlyph>& actual,
                           std::initializer_list<StringObjectGlyph> expected) {
    ASSERT_EQ(actual.size(), expected.size());
    std::size_t i = 0;
    for (const auto& glyph : expected) {
      SCOPED_TRACE(i);
      EXPECT_EQ(actual[i].text, glyph.text);
      EXPECT_EQ(actual[i].position, glyph.position);
      EXPECT_EQ(actual[i].size, glyph.size);
      EXPECT_EQ(actual[i].color, glyph.color);
      ++i;
    }
  }
};

TEST_F(SiglusObjectStringTest, SupportsAllCreateFormsAndDefaults) {
  Run(R"(
    a = Object(0, 0); a.create_string("日本A");
    b = Object(0, 1); b.create_string("B", 1);
    c = Object(0, 2); c.create_string("C", 1, 40, 50);
    text = a.get_string(); width = c.get_size_x(); height = c.get_size_y();
  )");
  auto* a = Object().GetDrawer<StringDrawer>();
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->text(), "日本A");
  EXPECT_EQ(a->style().size, 12);
  EXPECT_EQ(a->style().color, 0);
  EXPECT_EQ(a->style().shadow_color, 1);
  EXPECT_EQ(a->style().outline_color, 1);
  EXPECT_EQ(a->style().shadow_mode, -1);
  EXPECT_EQ(Object().Param().visible(), 0);
  EXPECT_EQ(Object(1).Param().visible(), 1);
  EXPECT_EQ(Object(2).Param().x(), 40);
  EXPECT_EQ(Object(2).Param().y(), 50);
  EXPECT_EQ(runtime.vm->globals_->at("text").Get_if<serilang::String>()->str_,
            "日本A");
  EXPECT_EQ(runtime.vm->globals_->at("width"), serilang::Value(0));
  EXPECT_EQ(runtime.vm->globals_->at("height"), serilang::Value(0));
}

TEST_F(SiglusObjectStringTest,
       UpdatesNestedChildrenWithoutResettingTheirState) {
  Run(R"(
    parent = Object(); parent.child.resize(1);
    text = parent.child[0]; text.create_string("", 1, 30, 40);
    text.child.resize(1); text.child[0].create_string("child");
    text.set_string_param(24, 2, 3, 4, 0, 1, -1, 2);
    text.set_string("#110CNo. 1");
  )");
  auto* object = Object().GetChild(0);
  ASSERT_NE(object, nullptr);
  auto* drawer = object->GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->text(), "#110CNo. 1");
  EXPECT_EQ(drawer->style(), (StringObjectStyle{24, 2, 3, 4, 0, 1, 2, -1}));
  EXPECT_EQ(object->Param().x(), 30);
  EXPECT_EQ(object->Param().y(), 40);
  EXPECT_EQ(object->Param().visible(), 1);
  EXPECT_TRUE(object->HasChildren());
  Run("text.set_string_param(18, 0, 0, 0, 0, 1, 2);");
  EXPECT_EQ(drawer->style().outline_color, 1);
  EXPECT_EQ(drawer->style().shadow_mode, 2);
}

TEST_F(SiglusObjectStringTest, RecreatingIdenticalTextResetsObjectAndStyle) {
  Run(R"(
    a = Object(); a.create_string("A", 1, 20, 30);
    a.child.resize(1); a.child[0].create_string("child");
    a.set_string_param(20, 1, 2, 3, 0, 1, 0);
    a.create_string("A");
  )");
  EXPECT_FALSE(Object().HasChildren());
  EXPECT_EQ(Object().Param().visible(), 0);
  EXPECT_EQ(Object().Param().x(), 0);
  EXPECT_EQ(Object().Param().y(), 0);
  EXPECT_EQ(Object().GetDrawer<StringDrawer>()->style().size, 12);
}

TEST_F(SiglusObjectStringTest, InvalidCallsPreserveExistingObject) {
  Run("a = Object(); a.create_string(\"original\", 1, 10, 20);");
  auto* original = Object().GetDrawer<StringDrawer>();
  for (const auto* code :
       {"a.create_string();", "a.create_string(12);",
        "a.create_string(\"bad\", 1, 2);",
        "a.create_string(\"bad\", 1, 2, \"y\");", "a.set_string_param(1);",
        "a.set_string_param(12, 0, 0, 0, 0, 1, \"mode\");",
        "a.set_string_param(0, 0, 0, 0, 0, 1, 2);"}) {
    SCOPED_TRACE(code);
    Run("caught = 0; try { " + std::string(code) +
        " } catch (e) { caught = 1; }");
    EXPECT_EQ(runtime.vm->globals_->at("caught"), serilang::Value(1));
    ASSERT_EQ(Object().GetDrawer<StringDrawer>(), original);
    EXPECT_EQ(original->text(), "original");
    EXPECT_EQ(original->style().size, 12);
    EXPECT_EQ(Object().Param().x(), 10);
  }
}

TEST_F(SiglusObjectStringTest, SettersIgnoreNonStringObjectsAndFreeClearsText) {
  Run(R"(
    a = Object(); a.create_rect(0, 0, 10, 10, 0, 0, 0, 255);
    a.set_string("ignored"); a.set_string_param(12, 0, 0, 0, 0, 1, 2);
    empty = a.get_string();
    a.create_string("A"); a.free(); cleared = a.get_string();
  )");
  EXPECT_FALSE(Object().HasDrawer());
  for (auto key : {"empty", "cleared"})
    EXPECT_EQ(runtime.vm->globals_->at(key).Get_if<serilang::String>()->str_,
              "");
}

TEST_F(SiglusObjectStringTest, CopyFromKeepsStringStateIndependent) {
  Run(R"(
    a = Object(0, 0); a.create_string("A", 1);
    b = Object(0, 1); b.copy_from(a);
    b.set_string("B"); b.set_string_param(20, 0, 0, 0, 0, 1, 0);
  )");
  EXPECT_EQ(Object(0).GetDrawer<StringDrawer>()->text(), "A");
  EXPECT_EQ(Object(0).GetDrawer<StringDrawer>()->style().size, 12);
  EXPECT_EQ(Object(1).GetDrawer<StringDrawer>()->text(), "B");
  EXPECT_EQ(Object(1).GetDrawer<StringDrawer>()->style().size, 20);
}

TEST_F(SiglusObjectStringTest, AbOtonashiAward) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(24,0,0,0,0,1,-1,1);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("称号");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{24, 0, 0, 0, 0, 1, 1, -1}));
  ExpectReadback("称号");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"称", Point(0, 0), 24, 0}, {"号", Point(24, 0), 24, 0}});
}

TEST_F(SiglusObjectStringTest, AbAchievementToast) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("実績",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(12,0,0,2,0,0,2);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{12, 0, 0, 2, 0, 0, 1, 2}));
  ExpectReadback("実績");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"実", Point(0, 0), 12, 0}, {"績", Point(12, 0), 12, 0}});
}

TEST_F(SiglusObjectStringTest, AbAchievementGallery) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("#110CNo. 1",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(18,0,0,12,0,0,0);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{18, 0, 0, 12, 0, 0, 1, 0}));
  ExpectReadback("#110CNo. 1");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"N", Point(0, 0), 18, 110},
                {"o", Point(9, 0), 18, 110},
                {".", Point(18, 0), 18, 110},
                {" ", Point(27, 0), 18, 110},
                {"1", Point(36, 0), 18, 110}});
}

TEST_F(SiglusObjectStringTest, AbDayLabel) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("現在の",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(18,0,0,0,0,1,2);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{18, 0, 0, 0, 0, 1, 1, 2}));
  ExpectReadback("現在の");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"現", Point(0, 0), 18, 0},
                {"在", Point(18, 0), 18, 0},
                {"の", Point(36, 0), 18, 0}});
}

TEST_F(SiglusObjectStringTest, AbConfigPreview) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("Thi",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(26,0,0,0,0,1,-1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{26, 0, 0, 0, 0, 1, 1, -1}));
  ExpectReadback("Thi");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"T", Point(0, 0), 26, 0},
                {"h", Point(13, 0), 26, 0},
                {"i", Point(26, 0), 26, 0}});
}

TEST_F(SiglusObjectStringTest, AbSaveTimestamp) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("stale",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(41,2,3,4,5,6,0,7);
    label.set_pos(91,92);
  )");

  // Target: recreate stale slot text before applying its source style.
  Run(R"(
    label.create_string("10/27",1,30,40);
  )");
  ASSERT_NE(Label().GetDrawer<StringDrawer>(), nullptr);
  EXPECT_EQ(Label().GetDrawer<StringDrawer>()->style(),
            (StringObjectStyle{12, 0, 0, 0, 0, 1, 1, -1}));
  EXPECT_FALSE(Label().HasChildren());
  Run(R"(
    label.set_string_param(15,0,0,0,0,1,0);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{15, 0, 0, 0, 0, 1, 1, 0}));
  ExpectReadback("10/27");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  EXPECT_FALSE(Label().HasChildren());
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"1", Point(0, 0), 15, 0},
                {"0", Point(8, 0), 15, 0},
                {"/", Point(16, 0), 15, 0},
                {"2", Point(24, 0), 15, 0},
                {"7", Point(32, 0), 15, 0}});
}

TEST_F(SiglusObjectStringTest, HarmoniaJapaneseFontPreview) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("あ",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(22,0,0,0,0,1,-1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{22, 0, 0, 0, 0, 1, 1, -1}));
  ExpectReadback("あ");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"あ", Point(0, 0), 22, 0}});

  // Each preview character is a separate child string.
  Run("parent.child.resize(3); label.set_pos(765,534); "
      "parent.child[1].create_string(\"い\",1,787,534); "
      "parent.child[1].set_string_param(22,0,0,0,0,1,-1); "
      "parent.child[2].create_string(\"う\",1,809,534); "
      "parent.child[2].set_string_param(22,0,0,0,0,1,-1);");
  const int expected_x[] = {765, 787, 809};
  const char* expected_text[] = {"あ", "い", "う"};
  const auto parent = runtime.stage->objects.Get(kLayerFg, 0);
  for (std::size_t i = 0; i < 3; ++i) {
    const auto* child = parent->GetChild(i);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->Param().x(), expected_x[i]);
    EXPECT_EQ(child->Param().y(), 534);
    const auto* text = child->GetDrawer<StringDrawer>();
    ASSERT_NE(text, nullptr);
    const auto character = LayoutStringObject(text->text(), text->style());
    ASSERT_EQ(character.size(), 1);
    EXPECT_EQ(character[0].text, expected_text[i]);
    EXPECT_EQ(character[0].position, Point(0, 0));
    EXPECT_EQ(character[0].size, 22);
  }
}

TEST_F(SiglusObjectStringTest, HarmoniaSaveExcerpt) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("stale",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(41,2,3,4,5,6,0,7);
    label.set_pos(91,92);
  )");

  // Target: recreate stale slot text before applying its source style.
  Run(R"(
    label.create_string("AB",1,30,40);
  )");
  ASSERT_NE(Label().GetDrawer<StringDrawer>(), nullptr);
  EXPECT_EQ(Label().GetDrawer<StringDrawer>()->style(),
            (StringObjectStyle{12, 0, 0, 0, 0, 1, 1, -1}));
  EXPECT_FALSE(Label().HasChildren());
  Run(R"(
    label.set_string_param(12,1,2,30,1,1,0);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{12, 1, 2, 30, 1, 1, 1, 0}));
  ExpectReadback("AB");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  EXPECT_FALSE(Label().HasChildren());
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"A", Point(0, 0), 12, 1}, {"B", Point(7, 0), 12, 1}});

  // Japanese excerpt lines are positioned by the script.
  Run("parent.child.resize(2); label.set_pos(290,333); "
      "parent.child[1].create_string(\"AB\",1,276,333); "
      "parent.child[1].set_string_param(12,1,2,30,1,1,0);");
  EXPECT_EQ(Label().Param().x(), 290);
  EXPECT_EQ(Label().Param().y(), 333);
  const auto* second = runtime.stage->objects.Get(kLayerFg, 0)->GetChild(1);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second->Param().x(), 276);
  EXPECT_EQ(second->Param().y(), 333);
  const auto* text = second->GetDrawer<StringDrawer>();
  ASSERT_NE(text, nullptr);
  const auto second_line = LayoutStringObject(text->text(), text->style());
  ASSERT_EQ(second_line.size(), 2);
  EXPECT_EQ(second_line[0].position, Point(0, 0));
  EXPECT_EQ(second_line[1].position, Point(7, 0));
}

TEST_F(SiglusObjectStringTest, HarmoniaSaveTitle) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("stale",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(41,2,3,4,5,6,0,7);
    label.set_pos(91,92);
  )");

  // Target: recreate stale slot text before applying its source style.
  Run(R"(
    label.create_string("章",1,30,40);
  )");
  ASSERT_NE(Label().GetDrawer<StringDrawer>(), nullptr);
  EXPECT_EQ(Label().GetDrawer<StringDrawer>()->style(),
            (StringObjectStyle{12, 0, 0, 0, 0, 1, 1, -1}));
  EXPECT_FALSE(Label().HasChildren());
  Run(R"(
    label.set_string_param(13,0,0,0,1,1,0);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{13, 0, 0, 0, 1, 1, 1, 0}));
  ExpectReadback("章");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  EXPECT_FALSE(Label().HasChildren());
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"章", Point(0, 0), 13, 1}});
}

TEST_F(SiglusObjectStringTest, LoopersConfigPreview) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("テキスト",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(36,0,0,0,0,1,-1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{36, 0, 0, 0, 0, 1, 1, -1}));
  ExpectReadback("テキスト");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"テ", Point(0, 0), 36, 0},
                {"キ", Point(36, 0), 36, 0},
                {"ス", Point(72, 0), 36, 0},
                {"ト", Point(108, 0), 36, 0}});
}

TEST_F(SiglusObjectStringTest, LoopersVoiceNames) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("AA",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(17,0,0,0,1,-1,0,-1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{17, 0, 0, 0, 1, -1, -1, 0}));
  ExpectReadback("AA");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"A", Point(0, 0), 17, 1}, {"A", Point(9, 0), 17, 1}});
}

TEST_F(SiglusObjectStringTest, LoopersSaveTimestamp) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("stale",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(41,2,3,4,5,6,0,7);
    label.set_pos(91,92);
  )");

  // Target: recreate stale slot text before applying its source style.
  Run(R"(
    label.create_string("10/27",1,30,40);
  )");
  ASSERT_NE(Label().GetDrawer<StringDrawer>(), nullptr);
  EXPECT_EQ(Label().GetDrawer<StringDrawer>()->style(),
            (StringObjectStyle{12, 0, 0, 0, 0, 1, 1, -1}));
  EXPECT_FALSE(Label().HasChildren());
  Run(R"(
    label.set_string_param(18,0,0,0,0,1,0);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{18, 0, 0, 0, 0, 1, 1, 0}));
  ExpectReadback("10/27");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  EXPECT_FALSE(Label().HasChildren());
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"1", Point(0, 0), 18, 0},
                {"0", Point(9, 0), 18, 0},
                {"/", Point(18, 0), 18, 0},
                {"2", Point(27, 0), 18, 0},
                {"7", Point(36, 0), 18, 0}});
}

TEST_F(SiglusObjectStringTest, LoopersDayLabel) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("現在の",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(18,0,0,0,0,1,2);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{18, 0, 0, 0, 0, 1, 1, 2}));
  ExpectReadback("現在の");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"現", Point(0, 0), 18, 0},
                {"在", Point(18, 0), 18, 0},
                {"の", Point(36, 0), 18, 0}});
}

TEST_F(SiglusObjectStringTest, LoopersTodoPanel) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("A#DB",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(34,1,5,30,0,1,2,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{34, 1, 5, 30, 0, 1, 1, 2}));
  ExpectReadback("A#DB");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"A", Point(0, 0), 34, 0}, {"B", Point(0, 39), 34, 0}});
}

TEST_F(SiglusObjectStringTest, LoopersStatusCounter) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("X:1",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(30,1,5,30,0,1,2,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{30, 1, 5, 30, 0, 1, 1, 2}));
  ExpectReadback("X:1");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"X", Point(0, 0), 30, 0},
                {":", Point(16, 0), 30, 0},
                {"1", Point(32, 0), 30, 0}});
}

TEST_F(SiglusObjectStringTest, LoopersShortcutLegend) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("Q",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(15,1,5,30,0,1,2,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{15, 1, 5, 30, 0, 1, 1, 2}));
  ExpectReadback("Q");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"Q", Point(0, 0), 15, 0}});
}

TEST_F(SiglusObjectStringTest, LoopersChapterTitle) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("第一章",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(28,0,0,0,160,0,0);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{28, 0, 0, 0, 160, 0, 1, 0}));
  ExpectReadback("第一章");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"第", Point(0, 0), 28, 160},
                {"一", Point(28, 0), 28, 160},
                {"章", Point(56, 0), 28, 160}});
}

TEST_F(SiglusObjectStringTest, RewriteConfigSample) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(24,0,0,0,0,1,-1);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("AB");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{24, 0, 0, 0, 0, 1, 1, -1}));
  ExpectReadback("AB");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"A", Point(0, 0), 24, 0}, {"B", Point(12, 0), 24, 0}});
}

TEST_F(SiglusObjectStringTest, RewriteSaveTimestamp) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(12,2,0,0,10,0,0);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("10/27");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{12, 2, 0, 0, 10, 0, 1, 0}));
  ExpectReadback("10/27");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"1", Point(0, 0), 12, 10},
                {"0", Point(8, 0), 12, 10},
                {"/", Point(16, 0), 12, 10},
                {"2", Point(24, 0), 12, 10},
                {"7", Point(32, 0), 12, 10}});
}

TEST_F(SiglusObjectStringTest, RewriteSaveTitle) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(15,0,0,0,10,0,0);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("章");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{15, 0, 0, 0, 10, 0, 1, 0}));
  ExpectReadback("章");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"章", Point(0, 0), 15, 10}});
}

TEST_F(SiglusObjectStringTest, RewriteSaveExcerpt) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(11,0,0,0,10,0,0);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("A...");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{11, 0, 0, 0, 10, 0, 1, 0}));
  ExpectReadback("A...");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"A", Point(0, 0), 11, 10},
                {".", Point(6, 0), 11, 10},
                {".", Point(12, 0), 11, 10},
                {".", Point(18, 0), 11, 10}});
}

TEST_F(SiglusObjectStringTest, RewriteSaveComment) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(14,0,0,0,10,0,0);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("memo");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{14, 0, 0, 0, 10, 0, 1, 0}));
  ExpectReadback("memo");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"m", Point(0, 0), 14, 10},
                {"e", Point(7, 0), 14, 10},
                {"m", Point(14, 0), 14, 10},
                {"o", Point(21, 0), 14, 10}});

  // Target: clear the same save-comment object.
  Run("label.set_string(\"\");");
  ExpectReadback("");
  EXPECT_TRUE(LayoutStringObject(drawer->text(), drawer->style()).empty());
  EXPECT_EQ(drawer->style(), (StringObjectStyle{14, 0, 0, 0, 10, 0, 1, 0}));
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_TRUE(Label().HasChildren());
}

TEST_F(SiglusObjectStringTest, RewriteMapLabel) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(16,0,0,0,1,0,2,0);
    label.set_clip(1,284,25,964,435);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("地名");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{16, 0, 0, 0, 1, 0, 0, 2}));
  ExpectReadback("地名");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"地", Point(0, 0), 16, 1}, {"名", Point(16, 0), 16, 1}});

  EXPECT_EQ(Label().Param().clip_rect(), Rect::REC(284, 25, 680, 410));
}

TEST_F(SiglusObjectStringTest, RewriteCluePopup) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("達成",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(30,0,0,0,0,1,-1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{30, 0, 0, 0, 0, 1, 1, -1}));
  ExpectReadback("達成");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"達", Point(0, 0), 30, 0}, {"成", Point(30, 0), 30, 0}});
}

TEST_F(SiglusObjectStringTest, RewriteGameOverPopup) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("Game Over",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(30,0,0,0,0,1,-1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{30, 0, 0, 0, 0, 1, 1, -1}));
  ExpectReadback("Game Over");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"G", Point(0, 0), 30, 0},
                {"a", Point(15, 0), 30, 0},
                {"m", Point(30, 0), 30, 0},
                {"e", Point(45, 0), 30, 0},
                {" ", Point(60, 0), 30, 0},
                {"O", Point(75, 0), 30, 0},
                {"v", Point(90, 0), 30, 0},
                {"e", Point(105, 0), 30, 0},
                {"r", Point(120, 0), 30, 0}});
}

TEST_F(SiglusObjectStringTest, RewriteYoshinoCaption) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(30,0,12,26,0,1,-1);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("A#DB");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{30, 0, 12, 26, 0, 1, 1, -1}));
  ExpectReadback("A#DB");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"A", Point(0, 0), 30, 0}, {"B", Point(0, 42), 30, 0}});
}

TEST_F(SiglusObjectStringTest, RewriteLuciaCharacterFade) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("T",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(20,0,0,0,0,1,2);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{20, 0, 0, 0, 0, 1, 1, 2}));
  ExpectReadback("T");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"T", Point(0, 0), 20, 0}});

  // Each line contains character objects with independent opacity.
  Run("parent.child.resize(2); line = parent.child[1]; line.child.resize(2); "
      "line.child[0].create_string(\"T\",1,0,0); "
      "line.child[0].set_string_param(20,0,0,0,0,1,2); "
      "line.child[1].create_string(\"h\",1,10,0); "
      "line.child[1].set_string_param(20,0,0,0,0,1,2); "
      "line.child[0].tr = 0; line.child[1].tr = 255; "
      "line.child[0].set_string(\"T\"); line.child[1].set_string(\"h\");");
  const auto* line = runtime.stage->objects.Get(kLayerFg, 0)->GetChild(1);
  ASSERT_NE(line, nullptr);
  const int expected_alpha[] = {0, 255};
  const int expected_x[] = {0, 10};
  const char* expected_text[] = {"T", "h"};
  for (std::size_t i = 0; i < 2; ++i) {
    const auto* character = line->GetChild(i);
    ASSERT_NE(character, nullptr);
    EXPECT_EQ(character->Param().raw_alpha(), expected_alpha[i]);
    EXPECT_EQ(character->Param().x(), expected_x[i]);
    const auto* text = character->GetDrawer<StringDrawer>();
    ASSERT_NE(text, nullptr);
    const auto glyph = LayoutStringObject(text->text(), text->style());
    ASSERT_EQ(glyph.size(), 1);
    EXPECT_EQ(glyph[0].text, expected_text[i]);
    EXPECT_EQ(glyph[0].position, Point(0, 0));
    EXPECT_EQ(glyph[0].size, 20);
  }
}

TEST_F(SiglusObjectStringTest, RewriteFakeMessageWindow) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("A#DB",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(26,0,12,26,0,1,2);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{26, 0, 12, 26, 0, 1, 1, 2}));
  ExpectReadback("A#DB");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"A", Point(0, 0), 26, 0}, {"B", Point(0, 38), 26, 0}});
}

TEST_F(SiglusObjectStringTest, RewriteEmptyEndingDebugOverlay) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{12, 0, 0, 0, 0, 1, 1, -1}));
  ExpectReadback("");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()), {});
}

TEST_F(SiglusObjectStringTest, SprbTitleBanner) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("称号",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(38,0,0,99,0,1,-1,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{38, 0, 0, 99, 0, 1, 1, -1}));
  ExpectReadback("称号");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"称", Point(0, 0), 38, 0}, {"号", Point(38, 0), 38, 0}});
}

TEST_F(SiglusObjectStringTest, SprbEditableGalleryCaption) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",0,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(36,0,10,10,0,1,-1);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("A漢B");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{36, 0, 10, 10, 0, 1, 1, -1}));
  ExpectReadback("A漢B");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 0);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"A", Point(0, 0), 36, 0},
                {"漢", Point(18, 0), 36, 0},
                {"B", Point(54, 0), 36, 0}});
}

TEST_F(SiglusObjectStringTest, SprbLockedRecordList) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("???",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(21,-2,0,0,110,-1,0,-1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{21, -2, 0, 0, 110, -1, -1, 0}));
  ExpectReadback("???");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"?", Point(0, 0), 21, 110},
                {"?", Point(9, 0), 21, 110},
                {"?", Point(18, 0), 21, 110}});
}

TEST_F(SiglusObjectStringTest, SprbConfigSpeedPreview) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("テキスト",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(36,0,0,0,0,1,-1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{36, 0, 0, 0, 0, 1, 1, -1}));
  ExpectReadback("テキスト");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"テ", Point(0, 0), 36, 0},
                {"キ", Point(36, 0), 36, 0},
                {"ス", Point(72, 0), 36, 0},
                {"ト", Point(108, 0), 36, 0}});
}

TEST_F(SiglusObjectStringTest, SprbVoiceVolumeNames) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("AA",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(14,0,0,0,200,-1,0,-1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{14, 0, 0, 0, 200, -1, -1, 0}));
  ExpectReadback("AA");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"A", Point(0, 0), 14, 200}, {"A", Point(7, 0), 14, 200}});
}

TEST_F(SiglusObjectStringTest, SprbRecordDescription) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("A漢B",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(21,-2,0,0,110,-1,0,-1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{21, -2, 0, 0, 110, -1, -1, 0}));
  ExpectReadback("A漢B");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"A", Point(0, 0), 21, 110},
                {"漢", Point(9, 0), 21, 110},
                {"B", Point(28, 0), 21, 110}});
}

TEST_F(SiglusObjectStringTest, SprbRecordAcquiredToast) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("レコード",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(16,0,0,0,0,0,0);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{16, 0, 0, 0, 0, 0, 1, 0}));
  ExpectReadback("レコード");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"レ", Point(0, 0), 16, 0},
                {"コ", Point(16, 0), 16, 0},
                {"ー", Point(32, 0), 16, 0},
                {"ド", Point(48, 0), 16, 0}});
}

TEST_F(SiglusObjectStringTest, SprbSaveGridTimestamp) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("stale",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(41,2,3,4,5,6,0,7);
    label.set_pos(91,92);
  )");

  // Target: recreate stale slot text before applying its source style.
  Run(R"(
    label.create_string("10/27",1,30,40);
  )");
  ASSERT_NE(Label().GetDrawer<StringDrawer>(), nullptr);
  EXPECT_EQ(Label().GetDrawer<StringDrawer>()->style(),
            (StringObjectStyle{12, 0, 0, 0, 0, 1, 1, -1}));
  EXPECT_FALSE(Label().HasChildren());
  Run(R"(
    label.set_string_param(19,0,0,0,0,1,0);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{19, 0, 0, 0, 0, 1, 1, 0}));
  ExpectReadback("10/27");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  EXPECT_FALSE(Label().HasChildren());
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"1", Point(0, 0), 19, 0},
                {"0", Point(10, 0), 19, 0},
                {"/", Point(20, 0), 19, 0},
                {"2", Point(30, 0), 19, 0},
                {"7", Point(40, 0), 19, 0}});
}

TEST_F(SiglusObjectStringTest, SprbSaveGridTitle) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("stale",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(41,2,3,4,5,6,0,7);
    label.set_pos(91,92);
  )");

  // Target: recreate stale slot text before applying its source style.
  Run(R"(
    label.create_string("章",1,30,40);
  )");
  ASSERT_NE(Label().GetDrawer<StringDrawer>(), nullptr);
  EXPECT_EQ(Label().GetDrawer<StringDrawer>()->style(),
            (StringObjectStyle{12, 0, 0, 0, 0, 1, 1, -1}));
  EXPECT_FALSE(Label().HasChildren());
  Run(R"(
    label.set_string_param(18,0,0,0,100,1,0);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{18, 0, 0, 0, 100, 1, 1, 0}));
  ExpectReadback("章");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  EXPECT_FALSE(Label().HasChildren());
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"章", Point(0, 0), 18, 100}});
}

TEST_F(SiglusObjectStringTest, SprbSaveGridExcerpt) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("stale",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(41,2,3,4,5,6,0,7);
    label.set_pos(91,92);
  )");

  // Target: recreate stale slot text before applying its source style.
  Run(R"(
    label.create_string("A...",1,30,40);
  )");
  ASSERT_NE(Label().GetDrawer<StringDrawer>(), nullptr);
  EXPECT_EQ(Label().GetDrawer<StringDrawer>()->style(),
            (StringObjectStyle{12, 0, 0, 0, 0, 1, 1, -1}));
  EXPECT_FALSE(Label().HasChildren());
  Run(R"(
    label.set_string_param(17,0,0,0,101,1,0);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{17, 0, 0, 0, 101, 1, 1, 0}));
  ExpectReadback("A...");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  EXPECT_FALSE(Label().HasChildren());
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"A", Point(0, 0), 17, 101},
                {".", Point(9, 0), 17, 101},
                {".", Point(18, 0), 17, 101},
                {".", Point(27, 0), 17, 101}});
}

TEST_F(SiglusObjectStringTest, SprbSaveGridComment) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("stale",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(41,2,3,4,5,6,0,7);
    label.set_pos(91,92);
  )");

  // Target: recreate stale slot text before applying its source style.
  Run(R"(
    label.create_string("memo",1,30,40);
  )");
  ASSERT_NE(Label().GetDrawer<StringDrawer>(), nullptr);
  EXPECT_EQ(Label().GetDrawer<StringDrawer>()->style(),
            (StringObjectStyle{12, 0, 0, 0, 0, 1, 1, -1}));
  EXPECT_FALSE(Label().HasChildren());
  Run(R"(
    label.set_string_param(17,0,0,0,101,1,0);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{17, 0, 0, 0, 101, 1, 1, 0}));
  ExpectReadback("memo");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  EXPECT_FALSE(Label().HasChildren());
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"m", Point(0, 0), 17, 101},
                {"e", Point(9, 0), 17, 101},
                {"m", Point(18, 0), 17, 101},
                {"o", Point(27, 0), 17, 101}});
}

TEST_F(SiglusObjectStringTest, SprbShortcutSaveMetadata) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("stale",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(41,2,3,4,5,6,0,7);
    label.set_pos(91,92);
  )");

  // Target: recreate stale slot text before applying its source style.
  Run(R"(
    label.create_string("10/27",1,30,40);
  )");
  ASSERT_NE(Label().GetDrawer<StringDrawer>(), nullptr);
  EXPECT_EQ(Label().GetDrawer<StringDrawer>()->style(),
            (StringObjectStyle{12, 0, 0, 0, 0, 1, 1, -1}));
  EXPECT_FALSE(Label().HasChildren());
  Run(R"(
    label.set_string_param(18,0,0,0,0,1,0);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{18, 0, 0, 0, 0, 1, 1, 0}));
  ExpectReadback("10/27");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  EXPECT_FALSE(Label().HasChildren());
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"1", Point(0, 0), 18, 0},
                {"0", Point(9, 0), 18, 0},
                {"/", Point(18, 0), 18, 0},
                {"2", Point(27, 0), 18, 0},
                {"7", Point(36, 0), 18, 0}});
}

TEST_F(SiglusObjectStringTest, SprbDayLabel) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("現在の",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(18,0,0,0,0,1,2);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{18, 0, 0, 0, 0, 1, 1, 2}));
  ExpectReadback("現在の");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"現", Point(0, 0), 18, 0},
                {"在", Point(18, 0), 18, 0},
                {"の", Point(36, 0), 18, 0}});
}

TEST_F(SiglusObjectStringTest, SprbTodoAndItemPanel) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("A#DB",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(34,1,5,30,0,1,2,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{34, 1, 5, 30, 0, 1, 1, 2}));
  ExpectReadback("A#DB");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"A", Point(0, 0), 34, 0}, {"B", Point(0, 39), 34, 0}});
}

TEST_F(SiglusObjectStringTest, SprbDebugShortcutStatus) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("Q",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(15,1,5,30,0,1,2,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{15, 1, 5, 30, 0, 1, 1, 2}));
  ExpectReadback("Q");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"Q", Point(0, 0), 15, 0}});
}

TEST_F(SiglusObjectStringTest, SprbGenericPopup) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("通知",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(42,0,0,68,0,1,-1,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{42, 0, 0, 68, 0, 1, 1, -1}));
  ExpectReadback("通知");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"通", Point(0, 0), 42, 0}, {"知", Point(42, 0), 42, 0}});
}

TEST_F(SiglusObjectStringTest, SprbEndingCardStatistics) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(50,0,0,99,0,1,-1,1);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("勝利:1");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{50, 0, 0, 99, 0, 1, 1, -1}));
  ExpectReadback("勝利:1");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"勝", Point(0, 0), 50, 0},
                {"利", Point(50, 0), 50, 0},
                {":", Point(100, 0), 50, 0},
                {"1", Point(125, 0), 50, 0}});
}

TEST_F(SiglusObjectStringTest, SprbShimamonDebugButton) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("次へ",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(20,0,0,6,0,0,2,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{20, 0, 0, 6, 0, 0, 1, 2}));
  ExpectReadback("次へ");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"次", Point(0, 0), 20, 0}, {"へ", Point(20, 0), 20, 0}});
}

TEST_F(SiglusObjectStringTest, SprbBattleFighterName) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("A漢B",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(21,0,0,13,0,0,2,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{21, 0, 0, 13, 0, 0, 1, 2}));
  ExpectReadback("A漢B");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"A", Point(0, 0), 21, 0},
                {"漢", Point(11, 0), 21, 0},
                {"B", Point(32, 0), 21, 0}});
}

TEST_F(SiglusObjectStringTest, SprbBattleMessagePoolSetter) {
  // Setup: the script resizes its message pool without creating strings.
  Run(R"(
    label.child.resize(1);
  )");

  // Target: update an uninitialized pooled child.
  Run(R"(
    label.set_string("hit");
  )");

  // Expected: the native setter does not allocate a string drawer.
  EXPECT_FALSE(Label().HasDrawer());
  EXPECT_TRUE(Label().HasChildren());
  ExpectReadback("");
}

TEST_F(SiglusObjectStringTest, SprbDebuffDebugText) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(30,0,0,15,6,0,2,1);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("弱体");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{30, 0, 0, 15, 6, 0, 1, 2}));
  ExpectReadback("弱体");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"弱", Point(0, 0), 30, 6}, {"体", Point(30, 0), 30, 6}});
}

TEST_F(SiglusObjectStringTest, SprbBattleDebugPanel) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(16,0,0,30,0,0,2,1);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("A#DB");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{16, 0, 0, 30, 0, 0, 1, 2}));
  ExpectReadback("A#DB");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"A", Point(0, 0), 16, 0}, {"B", Point(0, 16), 16, 0}});
}

TEST_F(SiglusObjectStringTest, SprbVersusAndResultRank) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("1位",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(55,0,0,12,0,0,2,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{55, 0, 0, 12, 0, 0, 1, 2}));
  ExpectReadback("1位");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"1", Point(0, 0), 55, 0}, {"位", Point(28, 0), 55, 0}});
}

TEST_F(SiglusObjectStringTest, SprbCardStatusAndThumbnail) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(20,0,0,20,0,0,2,1);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("HP:1");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{20, 0, 0, 20, 0, 0, 1, 2}));
  ExpectReadback("HP:1");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"H", Point(0, 0), 20, 0},
                {"P", Point(10, 0), 20, 0},
                {":", Point(20, 0), 20, 0},
                {"1", Point(30, 0), 20, 0}});
}

TEST_F(SiglusObjectStringTest, SprbEditableDeckName) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("Deck",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(46,0,0,10,50,0,0);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{46, 0, 0, 10, 50, 0, 1, 0}));
  ExpectReadback("Deck");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"D", Point(0, 0), 46, 50},
                {"e", Point(23, 0), 46, 50},
                {"c", Point(46, 0), 46, 50},
                {"k", Point(69, 0), 46, 50}});
}

TEST_F(SiglusObjectStringTest, SprbLibraryDetails) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(50,0,0,100,50,0,0);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("図鑑");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{50, 0, 0, 100, 50, 0, 1, 0}));
  ExpectReadback("図鑑");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"図", Point(0, 0), 50, 50}, {"鑑", Point(50, 0), 50, 50}});
}

TEST_F(SiglusObjectStringTest, SprbMapOpponentRank) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("1位",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(44,0,0,2,0,0,2,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{44, 0, 0, 2, 0, 0, 1, 2}));
  ExpectReadback("1位");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"1", Point(0, 0), 44, 0}, {"位", Point(22, 0), 44, 0}});

  // The selected-opponent rank lives above its animated parent.
  Run("label.set_pos(180,-310); label.set_string(\"1位\");");
  EXPECT_EQ(Label().Param().x(), 180);
  EXPECT_EQ(Label().Param().y(), -310);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{44, 0, 0, 2, 0, 0, 1, 2}));
  const auto local_glyphs = LayoutStringObject(drawer->text(), drawer->style());
  ASSERT_EQ(local_glyphs.size(), 2);
  EXPECT_EQ(local_glyphs[0].position, Point(0, 0));
  EXPECT_EQ(local_glyphs[1].position, Point(22, 0));
}

TEST_F(SiglusObjectStringTest, SprbDebugNavigationButton) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("次へ",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(20,0,0,6,0,0,2,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{20, 0, 0, 6, 0, 0, 1, 2}));
  ExpectReadback("次へ");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"次", Point(0, 0), 20, 0}, {"へ", Point(20, 0), 20, 0}});
}

TEST_F(SiglusObjectStringTest, SprbRankingTitleRow) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("称号",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(30,0,0,25,50,0,0);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{30, 0, 0, 25, 50, 0, 1, 0}));
  ExpectReadback("称号");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"称", Point(0, 0), 30, 50}, {"号", Point(30, 0), 30, 50}});
}

TEST_F(SiglusObjectStringTest, SprbCaptureSpotInformation) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(28,0,0,20,0,0,2,1);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("場所");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{28, 0, 0, 20, 0, 0, 1, 2}));
  ExpectReadback("場所");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"場", Point(0, 0), 28, 0}, {"所", Point(28, 0), 28, 0}});
}

TEST_F(SiglusObjectStringTest, SprbFortuneSlip) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("吉#DA",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(40,0,10,60,0,0,2,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{40, 0, 10, 60, 0, 0, 1, 2}));
  ExpectReadback("吉#DA");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"吉", Point(0, 0), 40, 0}, {"A", Point(0, 50), 40, 0}});
}

TEST_F(SiglusObjectStringTest, SprbTutorialSeedCounter) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(18,0,0,100,0,0,2,1);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("seed:1");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{18, 0, 0, 100, 0, 0, 1, 2}));
  ExpectReadback("seed:1");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"s", Point(0, 0), 18, 0},
                {"e", Point(9, 0), 18, 0},
                {"e", Point(18, 0), 18, 0},
                {"d", Point(27, 0), 18, 0},
                {":", Point(36, 0), 18, 0},
                {"1", Point(45, 0), 18, 0}});
}

TEST_F(SiglusObjectStringTest, SprbOriginalTennisDebug) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(18,0,0,30,0,0,2,1);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("#2CA#DB");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{18, 0, 0, 30, 0, 0, 1, 2}));
  ExpectReadback("#2CA#DB");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"A", Point(0, 0), 18, 2}, {"B", Point(0, 18), 18, 2}});
}

TEST_F(SiglusObjectStringTest, SprbOriginalTennisSpeech) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(38,0,0,28,0,1,2,1);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("A#DB");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{38, 0, 0, 28, 0, 1, 1, 2}));
  ExpectReadback("A#DB");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"A", Point(0, 0), 38, 0}, {"B", Point(0, 38), 38, 0}});
}

TEST_F(SiglusObjectStringTest, SprbOriginalTennisResult) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("勝利",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(40,0,0,15,0,1,2,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{40, 0, 0, 15, 0, 1, 1, 2}));
  ExpectReadback("勝利");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"勝", Point(0, 0), 40, 0}, {"利", Point(40, 0), 40, 0}});
}

TEST_F(SiglusObjectStringTest, SprbRbTennisDebugReadback) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(17,0,0,60,0,0,2,1);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    updates = 0;
    if (label.get_string() != "A#DB") { label.set_string("A#DB"); updates = updates + 1; }
    if (label.get_string() != "A#DB") { label.set_string("A#DB"); updates = updates + 1; }
  )");
  EXPECT_EQ(runtime.vm->globals_->at("updates"), serilang::Value(1));

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{17, 0, 0, 60, 0, 0, 1, 2}));
  ExpectReadback("A#DB");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"A", Point(0, 0), 17, 0}, {"B", Point(0, 17), 17, 0}});
}

TEST_F(SiglusObjectStringTest, SprbRbTennisSpeech) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(38,0,0,28,0,1,2,1);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("A#DB");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{38, 0, 0, 28, 0, 1, 1, 2}));
  ExpectReadback("A#DB");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"A", Point(0, 0), 38, 0}, {"B", Point(0, 38), 38, 0}});
}

TEST_F(SiglusObjectStringTest, SprbRbTennisResultPlaceholder) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("-",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(42,0,0,0,0,0,2,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{42, 0, 0, 0, 0, 0, 1, 2}));
  ExpectReadback("-");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"-", Point(0, 0), 42, 0}});
}

TEST_F(SiglusObjectStringTest, SprbRbTennisPlaylog) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("Team",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(42,0,0,0,0,0,2,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{42, 0, 0, 0, 0, 0, 1, 2}));
  ExpectReadback("Team");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"T", Point(0, 0), 42, 0},
                {"e", Point(21, 0), 42, 0},
                {"a", Point(42, 0), 42, 0},
                {"m", Point(63, 0), 42, 0}});
}

TEST_F(SiglusObjectStringTest, SprbStoryItemPickup) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("米水飴",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(42,0,0,68,0,1,-1,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{42, 0, 0, 68, 0, 1, 1, -1}));
  ExpectReadback("米水飴");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"米", Point(0, 0), 42, 0},
                {"水", Point(42, 0), 42, 0},
                {"飴", Point(84, 0), 42, 0}});
}

TEST_F(SiglusObjectStringTest, SprbStoryFeedPickup) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("飼料",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(42,0,0,68,0,1,-1,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{42, 0, 0, 68, 0, 1, 1, -1}));
  ExpectReadback("飼料");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"飼", Point(0, 0), 42, 0}, {"料", Point(42, 0), 42, 0}});
}

TEST_F(SiglusObjectStringTest, SprbStoryTitleAward) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("称号",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(38,0,0,99,0,1,-1,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{38, 0, 0, 99, 0, 1, 1, -1}));
  ExpectReadback("称号");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"称", Point(0, 0), 38, 0}, {"号", Point(38, 0), 38, 0}});
}

TEST_F(SiglusObjectStringTest, SprbStoryLearnedTechnique) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("習得",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(38,0,0,99,0,1,-1,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{38, 0, 0, 99, 0, 1, 1, -1}));
  ExpectReadback("習得");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"習", Point(0, 0), 38, 0}, {"得", Point(38, 0), 38, 0}});
}

TEST_F(SiglusObjectStringTest, SprbStoryRankPromotion) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("10位",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(42,0,0,68,0,1,-1,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{42, 0, 0, 68, 0, 1, 1, -1}));
  ExpectReadback("10位");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"1", Point(0, 0), 42, 0},
                {"0", Point(21, 0), 42, 0},
                {"位", Point(42, 0), 42, 0}});
}

TEST_F(SiglusObjectStringTest, SprbStoryCaptureSpotUnlock) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("場所",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(42,0,0,68,0,1,-1,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{42, 0, 0, 68, 0, 1, 1, -1}));
  ExpectReadback("場所");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"場", Point(0, 0), 42, 0}, {"所", Point(42, 0), 42, 0}});
}

TEST_F(SiglusObjectStringTest, SprbStoryBattleState) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("弱体",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(42,0,0,68,0,1,-1,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{42, 0, 0, 68, 0, 1, 1, -1}));
  ExpectReadback("弱体");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"弱", Point(0, 0), 42, 0}, {"体", Point(42, 0), 42, 0}});
}

TEST_F(SiglusObjectStringTest, SprbStoryFeedCapacityUpgrade) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("１→２",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(42,0,0,68,0,1,-1,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{42, 0, 0, 68, 0, 1, 1, -1}));
  ExpectReadback("１→２");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"１", Point(0, 0), 42, 0},
                {"→", Point(42, 0), 42, 0},
                {"２", Point(84, 0), 42, 0}});
}

TEST_F(SiglusObjectStringTest, SprbStoryTitleRevocation) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("称号",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(42,0,0,68,0,1,-1,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{42, 0, 0, 68, 0, 1, 1, -1}));
  ExpectReadback("称号");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"称", Point(0, 0), 42, 0}, {"号", Point(42, 0), 42, 0}});
}

TEST_F(SiglusObjectStringTest, SprbStoryDoublesUnlock) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("解放",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(38,0,0,99,0,1,-1,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{38, 0, 0, 99, 0, 1, 1, -1}));
  ExpectReadback("解放");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"解", Point(0, 0), 38, 0}, {"放", Point(38, 0), 38, 0}});
}

TEST_F(SiglusObjectStringTest, SprbStoryRacketReward) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("ラケット",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(42,0,0,68,0,1,-1,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{42, 0, 0, 68, 0, 1, 1, -1}));
  ExpectReadback("ラケット");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"ラ", Point(0, 0), 42, 0},
                {"ケ", Point(42, 0), 42, 0},
                {"ッ", Point(84, 0), 42, 0},
                {"ト", Point(126, 0), 42, 0}});
}

TEST_F(SiglusObjectStringTest, StellaDialogText) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(32,0,0,0,0,1,0);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("OK?");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{32, 0, 0, 0, 0, 1, 1, 0}));
  ExpectReadback("OK?");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"O", Point(0, 0), 32, 0},
                {"K", Point(16, 0), 32, 0},
                {"?", Point(32, 0), 32, 0}});
}

TEST_F(SiglusObjectStringTest, StellaSaveTitleAndExcerpt) {
  // Setup: nested label, controlled INI colors, and independent child state.
  Run(R"(
    label.create_string("",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(20,0,0,0,1,1,0);
  )");

  // Target: update the existing label while preserving object state.
  Run(R"(
    label.set_string("章");
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{20, 0, 0, 0, 1, 1, 1, 0}));
  ExpectReadback("章");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"章", Point(0, 0), 20, 1}});
}

TEST_F(SiglusObjectStringTest, StellaDebugShortcut) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("Q",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(15,1,5,30,0,1,2,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{15, 1, 5, 30, 0, 1, 1, 2}));
  ExpectReadback("Q");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"Q", Point(0, 0), 15, 0}});
}

TEST_F(SiglusObjectStringTest, StellaAnimatedDebugMessage) {
  // Setup: nested label, controlled INI colors, and independent child state.
  // Target: create and style the representative label.
  Run(R"(
    label.create_string("A#DB",1,30,40);
    label.child.resize(1);
    label.child[0].create_string("child",1,3,4);
    label.set_string_param(34,0,0,30,0,1,2,1);
  )");

  // Expected: source-derived glyphs, style, readback, and retained state.
  auto* drawer = Label().GetDrawer<StringDrawer>();
  ASSERT_NE(drawer, nullptr);
  EXPECT_EQ(drawer->style(), (StringObjectStyle{34, 0, 0, 30, 0, 1, 1, 2}));
  ExpectReadback("A#DB");
  EXPECT_EQ(Label().Param().x(), 30);
  EXPECT_EQ(Label().Param().y(), 40);
  EXPECT_EQ(Label().Param().visible(), 1);
  ExpectRetainedChild();
  ExpectGlyphs(LayoutStringObject(drawer->text(), drawer->style()),
               {{"A", Point(0, 0), 34, 0}, {"B", Point(0, 34), 34, 0}});
}

}  // namespace libsiglus::binding
