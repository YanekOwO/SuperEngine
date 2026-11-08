// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
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

#include "core/object_internal/object_parameter.hpp"
#include "core/object.hpp"
#include "serialization_test_helpers.hpp"

#include <gtest/gtest.h>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

namespace {

TEST(ObjectParameterTest, DefaultInit) {
  ObjectParameter param;

  EXPECT_FALSE(param.is_visible);
  EXPECT_EQ(param.position_x, 0);
  EXPECT_EQ(param.position_y, 0);
  EXPECT_EQ(param.GetXAdjustmentSum(), 0);
  EXPECT_EQ(param.GetYAdjustmentSum(), 0);
  EXPECT_EQ(param.adjustment_vertical, 0);
  EXPECT_EQ(param.origin_x, 0);
  EXPECT_EQ(param.origin_y, 0);
  EXPECT_EQ(param.repetition_origin_x, 0);
  EXPECT_EQ(param.repetition_origin_y, 0);
  EXPECT_EQ(param.scale_x_percent, 100);
  EXPECT_EQ(param.scale_y_percent, 100);
  EXPECT_EQ(param.high_quality_scale_x_percent, 1000);
  EXPECT_EQ(param.high_quality_scale_y_percent, 1000);
  EXPECT_EQ(param.rotation_div10, 0);
  EXPECT_EQ(param.pattern_number, 0);
  EXPECT_EQ(param.alpha_source, 255);
  EXPECT_EQ(param.adjustment_alphas,
            (std::vector<int>{255, 255, 255, 255, 255, 255, 255, 255}));
  EXPECT_FALSE(param.has_clip_rect());
  EXPECT_FALSE(param.has_own_clip_rect());
  EXPECT_EQ(param.monochrome_transform, 0);
  EXPECT_EQ(param.invert_transform, 0);
  EXPECT_EQ(param.light(), 0);
  EXPECT_EQ(param.bright, 0);
  EXPECT_EQ(param.dark, 0);
  EXPECT_EQ(param.tint_colour, RGBColour(0, 0, 0));
  EXPECT_EQ(param.blend_colour, RGBAColour(0, 0, 0, 0));
  EXPECT_EQ(param.mask_no, -1);
  EXPECT_EQ(param.tonecurve_no, -1);
  EXPECT_EQ(param.culling, 0);
  EXPECT_EQ(param.alpha_test, 1);
  EXPECT_EQ(param.alpha_blend, 1);
  EXPECT_EQ(param.composite_mode, 0);
  EXPECT_EQ(param.light_no, -1);
  EXPECT_EQ(param.fog_use, 0);
  EXPECT_EQ(param.scroll_rate_x, 0);
  EXPECT_EQ(param.scroll_rate_y, 0);
  EXPECT_EQ(param.z_order, 0);
  EXPECT_EQ(param.z_layer, 0);
  EXPECT_EQ(param.z_depth, 0);
  EXPECT_EQ(param.F.size(), 0);
  EXPECT_EQ(param.wipe_copy, 0);
  EXPECT_EQ(param.wipe_erase, 0);
  EXPECT_EQ(param.click_disable, 0);
}

TEST(ObjectParameterTest, AccessorsUpdateTypedFields) {
  ObjectParameter param;

  param.SetVisible(1);
  param.SetX(50);
  param.SetY(100);
  param.SetXAdjustment(0, 5);
  param.SetYAdjustment(1, -10);
  param.SetVert(1);
  param.SetOriginX(25);
  param.SetOriginY(30);
  param.SetRepOriginX(15);
  param.SetRepOriginY(20);
  param.SetScaleX(80);
  param.SetScaleY(90);
  param.SetHqScaleX(800);
  param.SetHqScaleY(900);
  param.SetRotation(45);
  param.SetPattNo(5);
  param.SetMono(1);
  param.SetInvert(1);
  param.SetBright(64);
  param.SetDark(32);
  param.SetTint(RGBColour(100, 150, 200));
  param.SetTintRed(110);
  param.SetTintGreen(160);
  param.SetTintBlue(210);
  param.SetColour(RGBAColour(50, 60, 70, 80));
  param.SetColourRed(55);
  param.SetColourGreen(65);
  param.SetColourBlue(75);
  param.SetColourLevel(85);
  param.SetMaskNo(4);
  param.SetTonecurveNo(5);
  param.SetCulling(7);
  param.SetAlphaTest(0);
  param.SetAlphaBlend(0);
  param.SetCompositeMode(2);
  param.SetLightNo(6);
  param.SetFogUse(8);
  param.SetScrollRateX(5);
  param.SetScrollRateY(-5);
  param.SetZOrder(1);
  param.SetZLayer(2);
  param.SetZDepth(3);
  param.SetAlpha(128);
  param.SetAlphaAdjustment(3, -20);
  param.SetClipRect(Rect::GRP(0, 0, 100, 100));
  param.SetOwnClipRect(Rect::GRP(10, 10, 80, 80));
  param.SetWipeCopy(1);
  param.SetWipeErase(2);
  param.SetClickDisable(3);

  EXPECT_EQ(param.visible(), 1);
  EXPECT_EQ(param.x(), 50);
  EXPECT_EQ(param.y(), 100);
  EXPECT_EQ(param.x_adjustment(0), 5);
  EXPECT_EQ(param.y_adjustment(1), -10);
  EXPECT_EQ(param.vert(), 1);
  EXPECT_EQ(param.origin_x, 25);
  EXPECT_EQ(param.origin_y, 30);
  EXPECT_EQ(param.rep_origin_x(), 15);
  EXPECT_EQ(param.rep_origin_y(), 20);
  EXPECT_EQ(param.ScaleX(), 80);
  EXPECT_EQ(param.ScaleY(), 90);
  EXPECT_EQ(param.HqScaleX(), 800);
  EXPECT_EQ(param.HqScaleY(), 900);
  EXPECT_EQ(param.rotation(), 45);
  EXPECT_EQ(param.GetPattNo(), 5);
  EXPECT_EQ(param.mono(), 1);
  EXPECT_EQ(param.invert(), 1);
  EXPECT_EQ(param.light(), 32);
  EXPECT_EQ(param.Bright(), 64);
  EXPECT_EQ(param.Dark(), 32);
  EXPECT_EQ(param.tint(), RGBColour(110, 160, 210));
  EXPECT_EQ(param.colour(), RGBAColour(55, 65, 75, 85));
  EXPECT_EQ(param.mask_no, 4);
  EXPECT_EQ(param.tonecurve_no, 5);
  EXPECT_EQ(param.culling, 1);
  EXPECT_EQ(param.alpha_test, 0);
  EXPECT_EQ(param.alpha_blend, 0);
  EXPECT_EQ(param.composite_mode, 2);
  EXPECT_EQ(param.light_no, 6);
  EXPECT_EQ(param.fog_use, 1);
  EXPECT_EQ(param.scroll_rate_x, 5);
  EXPECT_EQ(param.scroll_rate_y, -5);
  EXPECT_EQ(param.z_order, 1);
  EXPECT_EQ(param.z_layer, 2);
  EXPECT_EQ(param.z_depth, 3);
  EXPECT_EQ(param.raw_alpha(), 128);
  EXPECT_EQ(param.alpha_adjustment(3), -20);
  EXPECT_TRUE(param.has_clip_rect());
  EXPECT_TRUE(param.has_own_clip_rect());
  EXPECT_EQ(param.wipe_copy, 1);
  EXPECT_EQ(param.wipe_erase, 1);
  EXPECT_EQ(param.click_disable, 1);
}

TEST(ObjectParameterTest, LightAccessorUsesBrightDark) {
  ObjectParameter param;

  param.SetLight(40);
  EXPECT_EQ(param.light(), 40);
  EXPECT_EQ(param.Bright(), 40);
  EXPECT_EQ(param.Dark(), 0);

  param.SetLight(-50);
  EXPECT_EQ(param.light(), -50);
  EXPECT_EQ(param.Bright(), 0);
  EXPECT_EQ(param.Dark(), 50);

  param.SetLight(0);
  EXPECT_EQ(param.light(), 0);
  EXPECT_EQ(param.Bright(), 0);
  EXPECT_EQ(param.Dark(), 0);

  param.SetLight(300);
  EXPECT_EQ(param.light(), 255);
  EXPECT_EQ(param.Bright(), 255);
  EXPECT_EQ(param.Dark(), 0);

  param.SetLight(-300);
  EXPECT_EQ(param.light(), -255);
  EXPECT_EQ(param.Bright(), 0);
  EXPECT_EQ(param.Dark(), 255);

  param.SetBright(120);
  param.SetDark(20);
  EXPECT_EQ(param.light(), 100);

  param.bright = 400;
  param.dark = -2;
  EXPECT_EQ(param.light(), 255);
}

TEST(ObjectParameterTest, BrightDarkClampAndComposeWithParent) {
  ObjectParameter child;
  ObjectParameter parent;

  child.SetBright(-1);
  child.SetDark(-10);
  EXPECT_EQ(child.Bright(), 0);
  EXPECT_EQ(child.Dark(), 0);

  child.SetBright(300);
  child.SetDark(512);
  EXPECT_EQ(child.Bright(), 255);
  EXPECT_EQ(child.Dark(), 255);

  child.SetBright(64);
  parent.SetBright(128);
  child.SetDark(32);
  parent.SetDark(64);

  EXPECT_EQ(child.EffectiveBright(), 64);
  EXPECT_EQ(child.EffectiveDark(), 32);
  EXPECT_EQ(child.EffectiveBright(&parent), 160);
  EXPECT_EQ(child.EffectiveDark(&parent), 88);
  EXPECT_EQ(ObjectParameter::ComposeEffectLevel(0, 0), 0);
  EXPECT_EQ(ObjectParameter::ComposeEffectLevel(255, 12), 255);
  EXPECT_EQ(ObjectParameter::ComposeEffectLevel(12, 255), 255);

  child.bright = -12;
  child.dark = 300;
  EXPECT_EQ(child.EffectiveBright(), 0);
  EXPECT_EQ(child.EffectiveDark(), 255);
}

TEST(ObjectParameterTest, CompoundProperties) {
  ObjectParameter param;

  param.SetTextText("Hello World");
  param.SetTextOps(12, 2, 3, 5, 255, 128);
  EXPECT_EQ(param.text.ToString(),
            "value=\"Hello World\", text_size=12, xspace=2, yspace=3, "
            "char_count=5, colour=255, shadow_colour=128");

  Rect drift_area = Rect::GRP(0, 0, 100, 100);
  param.SetDriftOpts(10, 1, 0, 5, 1000, 2, 50, 10, 1, 0, 1, drift_area);
  EXPECT_EQ(param.drift.ToString(),
            "count=10, use_animation=1, start_pattern=0, end_pattern=5, "
            "total_animation_time_ms=1000, yspeed=2, period=50, amplitude=10, "
            "use_drift=1, unknown_drift_property=0, driftspeed=1, "
            "drift_area={Rect(0, 0, Size(100, 100))}");

  param.SetDigitValue(12345);
  param.SetDigitOpts(5, 1, 1, 0, 2);
  EXPECT_EQ(param.digit.ToString(),
            "value=12345, digits=5, zero=1, sign=1, pack=0, space=2");

  param.SetButtonOpts(1, 10, 2, 3);
  param.SetButtonState(1);
  param.SetButtonOverrides(5, 10, 15, 128, 64, 32);
  EXPECT_EQ(param.button.ToString(),
            "is_button=1, action=1, se=10, group=2, button_number=3, state=1, "
            "using_overides=true, pattern_override=5, x_offset_override=10, "
            "y_offset_override=15, rep_tr_override=128, "
            "rep_bright_override=64, rep_dark_override=32");
  EXPECT_EQ(param.GetComputedAlpha(), 128);
  EXPECT_FLOAT_EQ(param.GetNormalizedBright(), 64.0f / 255.0f);
  EXPECT_FLOAT_EQ(param.GetNormalizedDark(), 32.0f / 255.0f);

  param.ClearButtonOverrides();
  EXPECT_FALSE(param.button.using_overides);
  EXPECT_EQ(param.GetComputedAlpha(), 255);
  EXPECT_FLOAT_EQ(param.GetNormalizedBright(), 0.0f);
  EXPECT_FLOAT_EQ(param.GetNormalizedDark(), 0.0f);
}

TEST(ObjectParameterTest, GetterProxy) {
  ObjectParameter param;
  auto repno_getter = CreateGetter<&ObjectParameter::adjustment_offsets_x>();
  auto int_getter = CreateGetter<&ObjectParameter::alpha_source>();

  param.adjustment_offsets_x = {1, 2, 3, 4, 5, 6, 7, 8};
  EXPECT_EQ(repno_getter(param, 1), 2);
  param.adjustment_offsets_x = {10, -10, 20, -20};
  EXPECT_EQ(repno_getter(param, 2), 20);

  param.alpha_source = 128;
  EXPECT_EQ(int_getter(param), 128);
  param.alpha_source = 255;
  EXPECT_EQ(int_getter(param), 255);
}

TEST(ObjectParameterTest, SetterProxy) {
  ObjectParameter param;

  auto int_setter = CreateSetter<&ObjectParameter::composite_mode>();
  int_setter(param, 12);
  EXPECT_EQ(param.composite_mode, 12);
  int_setter(param, 36);
  EXPECT_EQ(param.composite_mode, 36);

  auto repno_setter = CreateSetter<&ObjectParameter::adjustment_offsets_y>();
  repno_setter(param, 2, 24);
  repno_setter(param, 3, -12);
  EXPECT_EQ(param.adjustment_offsets_y,
            (std::vector<int>{0, 0, 24, -12, 0, 0, 0, 0}));
}

// ------------------------------------------------------------------------------

class ObjectParameterArchiveTest : public ::testing::Test {};

TEST_F(ObjectParameterArchiveTest, RetainsConfiguredButtonInteraction) {
  // A configured waku button preserves its callback, visual overrides and
  // interaction state.
  serialization_test::ForEachFormat([&](auto format) {
    ObjectParameter source, target;
    source.button.cut_no = 7;
    source.button.config.emplace();
    source.button.config->name = "LOAD";
    source.button.config->slot_id = 12;
    source.button.call = CmdTarget{17, 4};
    source.button.hit_state = ButtonState::Push;
    source.button.real_state = ButtonState::Disable;
    source.button.visible = false;
    source.button.using_overides = true;
    source.button.x_offset_override = -5;
    serialization_test::RoundTrip(source, target, format);
    EXPECT_EQ(target.button, source.button);
  });
}

}  // namespace
