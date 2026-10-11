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
//
// -----------------------------------------------------------------------

#pragma once

#include "core/button_action_table.hpp"
#include "core/colour.hpp"
#include "core/mwnd_config.hpp"
#include "core/rect.hpp"

#include <algorithm>
#include <functional>
#include <numeric>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include <boost/serialization/array.hpp>
#include <boost/serialization/serialization.hpp>

extern const Rect EMPTY_RECT;

namespace serilang {
struct Function;
}

struct TextProperties {
  std::string value = "";

  int text_size = 14;
  int xspace = 0, yspace = 0;

  int char_count = 0;
  int colour = 0;
  int shadow_colour = -1;

  bool operator==(const TextProperties& rhs) const = default;

  // for debug
  std::string ToString() const;

  // boost::serialization support
  template <class Archive>
  void serialize(Archive& ar, unsigned int version) {
    ar & value & text_size & xspace & yspace & char_count & colour &
        shadow_colour;
  }
};

struct DriftProperties {
  int count = 1;

  int use_animation = 0;
  int start_pattern = 0;
  int end_pattern = 0;
  int total_animation_time_ms = 0;

  int yspeed = 1000;

  int period = 0;
  int amplitude = 0;

  int use_drift = 0;
  int unknown_drift_property = 0;
  int driftspeed = 0;

  Rect drift_area = Rect(Point(-1, -1), Size(-1, -1));

  bool operator==(const DriftProperties& rhs) const = default;

  // for debug
  std::string ToString() const;

  // boost::serialization support
  template <class Archive>
  void serialize(Archive& ar, unsigned int version) {
    ar & count & use_animation & start_pattern & end_pattern &
        total_animation_time_ms & yspeed & period & amplitude & use_drift &
        unknown_drift_property & driftspeed & drift_area;
  }
};

struct DigitProperties {
  int value = 0;

  int digits = 0;
  int zero = 0;
  int sign = 0;
  int pack = 0;
  int space = 0;

  bool operator==(const DigitProperties& rhs) const = default;

  // for debug
  std::string ToString() const;

  // boost::serialization support
  template <class Archive>
  void serialize(Archive& ar, unsigned int version) {
    ar & value & digits & zero & sign & pack & space;
  }
};

struct ButtonProperties {
  int is_button = 0;

  // Siglus base cut; separate from pat_no and outside RealLive save archives.
  int cut_no = 0;

  int action = 0;
  int se = -1;
  int group = 0;
  int button_number = 0;

  int state = 0;

  bool using_overides = false;
  int pattern_override = 0;
  int x_offset_override = 0;
  int y_offset_override = 0;
  int rep_tr_override = 255;
  int rep_bright_override = 0;
  int rep_dark_override = 0;

  // WAKU.BTN settings for a configured message-window button.
  std::optional<MwndConfig::Button> config;
  // Script CALL override, owned by the resolved scene module.
  serilang::Function* call = nullptr;
  // Cursor/group hit state: NORMAL, HIT or PUSH.
  ButtonState hit_state = ButtonState::Normal;
  // Drawing state after applying SELECT, DISABLE and window policy.
  ButtonState real_state = ButtonState::Normal;
  // Visibility supplied by the window button's system-command policy.
  bool visible = true;

  bool operator==(const ButtonProperties& rhs) const = default;

  // for debug
  std::string ToString() const;

  // boost::serialization support
  template <class Archive>
  void serialize(Archive& ar, unsigned int version) {
    // The override values are stuck here because I'm not sure about
    // initialization otherwise.
    ar & is_button & action & se & group & button_number & state &
        using_overides & pattern_override & x_offset_override &
        y_offset_override & rep_tr_override & rep_bright_override &
        rep_dark_override;
  }
};

struct ObjectParameter {
  bool is_visible = false;
  int position_x = 0;
  int position_y = 0;
  std::vector<int> adjustment_offsets_x;
  std::vector<int> adjustment_offsets_y;
  int adjustment_vertical = 0;
  int origin_x = 0;
  int origin_y = 0;
  int repetition_origin_x = 0;
  int repetition_origin_y = 0;
  int scale_x_percent = 100;
  int scale_y_percent = 100;
  int high_quality_scale_x_percent = 1000;
  int high_quality_scale_y_percent = 1000;
  int rotation_div10 = 0;
  int pattern_number = 0;
  int alpha_source = 255;
  std::vector<int> adjustment_alphas;
  Rect clipping_region = Rect(Point(0, 0), Size(-1, -1));
  Rect own_space_clipping_region = Rect(Point(0, 0), Size(-1, -1));
  int monochrome_transform = 0;
  int invert_transform = 0;
  int bright = 0;
  int dark = 0;
  RGBColour tint_colour = RGBColour(0, 0, 0);
  RGBAColour blend_colour = RGBAColour(0, 0, 0, 0);
  int mask_no = -1;
  int tonecurve_no = -1;
  int culling = 0;
  int alpha_test = 1;
  int alpha_blend = 1;
  int composite_mode = 0;
  int light_no = -1;
  int fog_use = 0;
  int scroll_rate_x = 0;
  int scroll_rate_y = 0;
  int z_order = 0;
  int z_layer = 0;
  int z_depth = 0;
  TextProperties text;
  DriftProperties drift;
  DigitProperties digit;
  ButtonProperties button;
  std::vector<int> F;
  int wipe_copy = 0;
  int wipe_erase = 0;
  int click_disable = 0;

  ObjectParameter();

  int visible() const { return static_cast<int>(is_visible); }
  void SetVisible(const int in) { is_visible = static_cast<bool>(in); }

  int x() const { return position_x; }
  void SetX(const int x) { position_x = x; }

  int y() const { return position_y; }
  void SetY(const int y) { position_y = y; }

  int x_adjustment(int idx) const { return adjustment_offsets_x[idx]; }
  int GetXAdjustmentSum() const {
    return std::accumulate(adjustment_offsets_x.cbegin(),
                           adjustment_offsets_x.cend(), 0);
  }
  void SetXAdjustment(int idx, int x) { adjustment_offsets_x[idx] = x; }

  int y_adjustment(int idx) const { return adjustment_offsets_y[idx]; }
  int GetYAdjustmentSum() const {
    return std::accumulate(adjustment_offsets_y.cbegin(),
                           adjustment_offsets_y.cend(), 0);
  }
  void SetYAdjustment(int idx, int y) { adjustment_offsets_y[idx] = y; }

  int vert() const { return adjustment_vertical; }
  void SetVert(int in) { adjustment_vertical = in; }

  void SetOriginX(int in) { origin_x = in; }

  void SetOriginY(int in) { origin_y = in; }

  int rep_origin_x() const { return repetition_origin_x; }
  void SetRepOriginX(int in) { repetition_origin_x = in; }

  int rep_origin_y() const { return repetition_origin_y; }
  void SetRepOriginY(int in) { repetition_origin_y = in; }

  int ScaleX() const { return scale_x_percent; }
  void SetScaleX(const int in) { scale_x_percent = in; }
  int ScaleY() const { return scale_y_percent; }
  void SetScaleY(const int in) { scale_y_percent = in; }

  // Note: hq scales are object scale factors out of 1000.
  int HqScaleX() const { return high_quality_scale_x_percent; }
  void SetHqScaleX(const int in) { high_quality_scale_x_percent = in; }
  int HqScaleY() const { return high_quality_scale_y_percent; }
  void SetHqScaleY(const int in) { high_quality_scale_y_percent = in; }

  float GetWidthScaleFactor() const {
    return (static_cast<float>(ScaleX()) / 100.0f) *
           (static_cast<float>(HqScaleX()) / 1000.0f);
  }
  float GetHeightScaleFactor() const {
    return (static_cast<float>(ScaleY()) / 100.0f) *
           (static_cast<float>(HqScaleY()) / 1000.0f);
  }

  int rotation() const { return rotation_div10; }
  void SetRotation(const int in) { rotation_div10 = in; }

  int GetPattNo() const {
    if (button.using_overides)
      return button.pattern_override;

    return pattern_number + button.cut_no;
  }
  void SetPattNo(const int in) { pattern_number = in; }

  int mono() const { return monochrome_transform; }
  void SetMono(const int in) { monochrome_transform = in; }

  int invert() const { return invert_transform; }
  void SetInvert(const int in) { invert_transform = in; }

  int light() const { return Bright() - Dark(); }
  void SetLight(const int in) {
    const int light = std::clamp(in, -255, 255);
    if (light >= 0) {
      bright = light;
      dark = 0;
    } else {
      bright = 0;
      dark = -light;
    }
  }

  inline int Bright() const { return std::clamp(bright, 0, 255); }
  inline float GetNormalizedBright() const {
    const int button_bright =
        button.using_overides ? button.rep_bright_override : 0;
    return std::clamp(Bright() + button_bright, 0, 255) / 255.f;
  }
  void SetBright(const int in) { bright = std::clamp(in, 0, 255); }
  int Dark() const { return std::clamp(dark, 0, 255); }
  inline float GetNormalizedDark() const {
    const int button_dark =
        button.using_overides ? button.rep_dark_override : 0;
    return std::clamp(Dark() + button_dark, 0, 255) / 255.f;
  }
  void SetDark(const int in) { dark = std::clamp(in, 0, 255); }
  static int ComposeEffectLevel(int child, int parent) {
    child = std::clamp(child, 0, 255);
    parent = std::clamp(parent, 0, 255);
    return 255 - ((255 - child) * (255 - parent)) / 255;
  }
  int EffectiveBright(const ObjectParameter* parent = nullptr) const {
    return parent ? ComposeEffectLevel(Bright(), parent->Bright()) : Bright();
  }
  int EffectiveDark(const ObjectParameter* parent = nullptr) const {
    return parent ? ComposeEffectLevel(Dark(), parent->Dark()) : Dark();
  }

  RGBColour tint() const { return tint_colour; }
  int tint_red() const { return tint().r(); }
  int tint_green() const { return tint().g(); }
  int tint_blue() const { return tint().b(); }
  void SetTint(const RGBColour& in) { tint_colour = in; }
  void SetTintRed(const int r) { tint_colour.set_red(r); }
  void SetTintGreen(const int g) { tint_colour.set_green(g); }
  void SetTintBlue(const int b) { tint_colour.set_blue(b); }

  RGBAColour colour() const { return blend_colour; }
  int colour_red() const { return colour().r(); }
  int colour_green() const { return colour().g(); }
  int colour_blue() const { return colour().b(); }
  int colour_level() const { return colour().a(); }
  void SetColour(const RGBAColour& in) { blend_colour = in; }
  void SetColourRed(const int in) { blend_colour.set_red(in); }
  void SetColourGreen(const int in) { blend_colour.set_green(in); }
  void SetColourBlue(const int in) { blend_colour.set_blue(in); }
  void SetColourLevel(const int in) { blend_colour.set_alpha(in); }

  void SetMaskNo(int in) { mask_no = in; }
  void SetTonecurveNo(int in) { tonecurve_no = in; }
  void SetCulling(int in) { culling = in ? 1 : 0; }
  void SetAlphaTest(int in) { alpha_test = in ? 1 : 0; }
  void SetAlphaBlend(int in) { alpha_blend = in ? 1 : 0; }
  void SetCompositeMode(int in) { composite_mode = in; }
  void SetLightNo(int in) { light_no = in; }
  void SetFogUse(int in) { fog_use = in ? 1 : 0; }

  void SetScrollRateX(int in) { scroll_rate_x = in; }

  void SetScrollRateY(int in) { scroll_rate_y = in; }

  void SetZOrder(const int in) { z_order = in; }
  void SetZLayer(const int in) { z_layer = in; }
  void SetZDepth(const int in) { z_depth = in; }

  int GetComputedAlpha() const {
    auto alpha = raw_alpha();
    for (const auto it : adjustment_alphas)
      alpha = (alpha * it) / 255;
    if (button.using_overides)
      alpha = (alpha * button.rep_tr_override) / 255;
    return alpha;
  }
  inline float GetNormalizedAlpha() const { return GetComputedAlpha() / 255.f; }
  int raw_alpha() const { return alpha_source; }
  void SetAlpha(const int in) { alpha_source = in; }

  inline auto alpha_adjustment() const { return adjustment_alphas; }
  int alpha_adjustment(int idx) const { return adjustment_alphas[idx]; }
  void SetAlphaAdjustment(int idx, int alpha) {
    adjustment_alphas[idx] = alpha;
  }

  Rect clip_rect() const { return clipping_region; }
  bool has_clip_rect() const {
    const auto clip = clip_rect();
    return clip.width() >= 0 || clip.height() >= 0;
  }
  void ClearClipRect() { SetClipRect(EMPTY_RECT); }
  void SetClipRect(const Rect& in) { clipping_region = in; }

  Rect own_clip_rect() const { return own_space_clipping_region; }
  bool has_own_clip_rect() const {
    const auto clip = own_clip_rect();
    return clip.width() >= 0 || clip.height() >= 0;
  }
  void ClearOwnClipRect() { SetOwnClipRect(EMPTY_RECT); }
  void SetOwnClipRect(const Rect& in) { own_space_clipping_region = in; }

  void SetWipeCopy(int in) { wipe_copy = in ? 1 : 0; }
  void SetWipeErase(int in) { wipe_erase = in ? 1 : 0; }
  void SetClickDisable(int in) { click_disable = in ? 1 : 0; }

  TextProperties TextProperty() const { return text; }
  void SetTextProperty(const TextProperties& in) { text = in; }
  void SetTextText(const std::string& utf8str) { text.value = utf8str; }
  std::string GetTextText() const { return text.value; }

  void SetTextOps(int size,
                  int xspace,
                  int yspace,
                  int char_count,
                  int colour,
                  int shadow) {
    text.text_size = size;
    text.xspace = xspace;
    text.yspace = yspace;
    text.char_count = char_count;
    text.colour = colour;
    text.shadow_colour = shadow;
  }
  int GetTextSize() const { return text.text_size; }
  int GetTextXSpace() const { return text.xspace; }
  int GetTextYSpace() const { return text.yspace; }
  int GetTextCharCount() const { return text.char_count; }
  int GetTextColour() const { return text.colour; }
  int GetTextShadowColour() const { return text.shadow_colour; }

  DriftProperties DriftProperty() const { return drift; }
  void SetDriftProperty(const DriftProperties& in) { drift = in; }
  void SetDriftOpts(int count,
                    int use_animation,
                    int start_pattern,
                    int end_pattern,
                    int total_animation_time_ms,
                    int yspeed,
                    int period,
                    int amplitude,
                    int use_drift,
                    int unknown_drift_property,
                    int driftspeed,
                    Rect driftarea) {
    drift.count = count;
    drift.use_animation = use_animation;
    drift.start_pattern = start_pattern;
    drift.end_pattern = end_pattern;
    drift.total_animation_time_ms = total_animation_time_ms;
    drift.yspeed = yspeed;
    drift.period = period;
    drift.amplitude = amplitude;
    drift.use_drift = use_drift;
    drift.unknown_drift_property = unknown_drift_property;
    drift.driftspeed = driftspeed;
    drift.drift_area = driftarea;
  }
  int GetDriftParticleCount() const { return drift.count; }
  int GetDriftUseAnimation() const { return drift.use_animation; }
  int GetDriftStartPattern() const { return drift.start_pattern; }
  int GetDriftEndPattern() const { return drift.end_pattern; }
  int GetDriftAnimationTime() const { return drift.total_animation_time_ms; }
  int GetDriftYSpeed() const { return drift.yspeed; }
  int GetDriftPeriod() const { return drift.period; }
  int GetDriftAmplitude() const { return drift.amplitude; }
  int GetDriftUseDrift() const { return drift.use_drift; }
  int GetDriftUnknown() const { return drift.unknown_drift_property; }
  int GetDriftDriftSpeed() const { return drift.driftspeed; }
  Rect GetDriftArea() const { return drift.drift_area; }

  DigitProperties DigitProperty() const { return digit; }
  void SetDigitProperty(const DigitProperties& in) { digit = in; }
  void SetDigitValue(int value) { digit.value = value; }
  void SetDigitOpts(int digits, int zero, int sign, int pack, int space) {
    digit.digits = digits;
    digit.zero = zero;
    digit.sign = sign;
    digit.pack = pack;
    digit.space = space;
  }
  int GetDigitValue() const { return digit.value; }
  int GetDigitDigits() const { return digit.digits; }
  int GetDigitZero() const { return digit.zero; }
  int GetDigitSign() const { return digit.sign; }
  int GetDigitPack() const { return digit.pack; }
  int GetDigitSpace() const { return digit.space; }

  ButtonProperties ButtonProperty() const { return button; }
  void SetButtonProperty(const ButtonProperties& in) { button = in; }
  void SetButtonOpts(int action, int se, int group, int button_number) {
    button.is_button = 1;
    button.action = action;
    button.se = se;
    button.group = group;
    button.button_number = button_number;
  }
  void SetButtonState(int state) { button.state = state; }
  int IsButton() const { return button.is_button; }
  int GetButtonAction() const { return button.action; }
  int GetButtonSe() const { return button.se; }
  int GetButtonGroup() const { return button.group; }
  int GetButtonNumber() const { return button.button_number; }
  int GetButtonState() const { return button.state; }
  void SetButtonOverrides(int override_pattern,
                          int override_x_offset,
                          int override_y_offset,
                          int override_rep_tr,
                          int override_rep_bright,
                          int override_rep_dark) {
    button.using_overides = true;
    button.pattern_override = override_pattern;
    button.x_offset_override = override_x_offset;
    button.y_offset_override = override_y_offset;
    button.rep_tr_override = override_rep_tr;
    button.rep_bright_override = override_rep_bright;
    button.rep_dark_override = override_rep_dark;
  }
  inline void ClearButtonOverrides() { button.using_overides = false; }
  inline bool GetButtonUsingOverides() const { return button.using_overides; }
  inline Point GetButtonOffsetOverride() const {
    return Point(button.x_offset_override, button.y_offset_override);
  }

 private:
  friend class boost::serialization::access;

  template <class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    ar & is_visible & position_x & position_y & adjustment_offsets_x &
        adjustment_offsets_y & adjustment_vertical & origin_x & origin_y &
        repetition_origin_x & repetition_origin_y & scale_x_percent &
        scale_y_percent & high_quality_scale_x_percent &
        high_quality_scale_y_percent & rotation_div10 & pattern_number &
        alpha_source & adjustment_alphas & clipping_region &
        own_space_clipping_region & monochrome_transform & invert_transform &
        bright & dark;
    ar & tint_colour & blend_colour;
    ar & mask_no & tonecurve_no & culling & alpha_test & alpha_blend;
    ar & composite_mode;
    ar & light_no & fog_use;
    ar & scroll_rate_x & scroll_rate_y & z_order & z_layer & z_depth & text &
        drift & digit & button & wipe_copy;
    ar & wipe_erase & click_disable & F;
  }
};

template <typename T>
inline constexpr bool always_false_v = false;

template <auto member>
using ObjectParameterMemberType = std::remove_cv_t<std::remove_reference_t<
    decltype(std::declval<ObjectParameter&>().*member)>>;

template <auto member>
auto CreateGetter() {
  using property_type = ObjectParameterMemberType<member>;

  if constexpr (std::is_same_v<property_type, std::vector<int>>) {
    return static_cast<std::function<int(const ObjectParameter&, int)>>(
        [](const ObjectParameter& param, int repno) -> int {
          return (param.*member)[repno];
        });
  } else if constexpr (std::is_same_v<property_type, int> ||
                       std::is_same_v<property_type, bool>) {
    return static_cast<std::function<int(const ObjectParameter&)>>(
        [](const ObjectParameter& param) -> int {
          return static_cast<int>(param.*member);
        });
  } else {
    static_assert(always_false_v<property_type>, "Unsupported type.");
  }
}

template <auto member>
auto CreateSetter() {
  using property_type = ObjectParameterMemberType<member>;

  if constexpr (std::is_same_v<property_type, std::vector<int>>) {
    return static_cast<std::function<void(ObjectParameter&, int, int)>>(
        [](ObjectParameter& param, int repno, int value) {
          (param.*member)[repno] = value;
        });
  } else if constexpr (std::is_same_v<property_type, int> ||
                       std::is_same_v<property_type, bool>) {
    return static_cast<std::function<void(ObjectParameter&, int)>>(
        [](ObjectParameter& param, int value) {
          param.*member = static_cast<property_type>(value);
        });
  } else {
    static_assert(always_false_v<property_type>, "Unsupported type.");
  }
}
