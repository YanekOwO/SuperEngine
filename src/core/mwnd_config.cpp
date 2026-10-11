// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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

#include "core/mwnd_config.hpp"

#include "core/gameexe.hpp"
#include "utilities/graphics.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <set>
#include <string_view>

namespace {

constexpr int kDefaultSiglusWindowCount = 2;
constexpr int kMaximumConfigEntries = 256;

std::optional<int> ParseIndex(std::string_view text) {
  int result = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), result);
  if (error != std::errc() || end != text.data() + text.size())
    return std::nullopt;
  return result;
}

RGBAColour ParseRGBA(const std::vector<int>& values,
                     RGBAColour fallback = RGBAColour::White()) {
  if (values.size() < 4)
    return fallback;
  return RGBAColour(
      std::clamp(values[0], 0, 255), std::clamp(values[1], 0, 255),
      std::clamp(values[2], 0, 255), std::clamp(values[3], 0, 255));
}

RGBColour ParseRGB(const std::vector<int>& values,
                   RGBColour fallback = RGBColour::White()) {
  if (values.size() < 3)
    return fallback;
  return RGBColour(std::clamp(values[0], 0, 255), std::clamp(values[1], 0, 255),
                   std::clamp(values[2], 0, 255));
}

TextLayout MakeLayout(int font_size,
                      int count_x,
                      int count_y,
                      int space_x,
                      int space_y,
                      int ruby_size) {
  const int height =
      std::max(count_y, 0) * std::max(font_size + space_y + ruby_size, 0);
  const int width = std::max(count_x, 0) * std::max(font_size + space_x, 0);
  TextLayout layout(height, width, width + std::max(font_size, 0));
  layout.font_size = font_size;
  layout.ruby_font_size = ruby_size;
  layout.x_spacing = space_x;
  layout.y_spacing = space_y;
  return layout;
}

Size ReadScreenSize(Gameexe& gexe) {
  if (!gexe.Exists("SCREENSIZE_MOD"))
    return Size(800, 600);
  return GetScreenSize(gexe);
}

std::set<int> FindIndices(Gameexe& gexe, std::string prefix, std::size_t part) {
  std::set<int> result;
  for (auto record : gexe.Filter(std::move(prefix))) {
    const auto parts = record.GetKeyParts();
    if (parts.size() <= part)
      continue;
    if (auto index = ParseIndex(parts[part]); index && *index >= 0)
      result.insert(*index);
  }
  return result;
}

std::optional<std::vector<int>> SiglusIntVec(Gameexe& gexe,
                                             int index,
                                             std::string_view suffix) {
  for (const std::string& key : {std::format("MWND.{:03}.{}", index, suffix),
                                 std::format("MWND.{}.{}", index, suffix)}) {
    if (auto value = gexe(key).IntVec())
      return std::move(*value);
  }
  return std::nullopt;
}

std::optional<int> SiglusInt(Gameexe& gexe,
                             int index,
                             std::string_view suffix) {
  for (const std::string& key : {std::format("MWND.{:03}.{}", index, suffix),
                                 std::format("MWND.{}.{}", index, suffix)}) {
    if (auto value = gexe(key).Int())
      return *value;
  }
  return std::nullopt;
}

void ApplyPair(const std::optional<std::vector<int>>& value, int& x, int& y) {
  if (value && value->size() >= 2)
    x = (*value)[0], y = (*value)[1];
}

void ApplyRect(const std::optional<std::vector<int>>& value,
               int& left,
               int& top,
               int& right,
               int& bottom) {
  if (value && value->size() >= 4) {
    left = (*value)[0];
    top = (*value)[1];
    right = (*value)[2];
    bottom = (*value)[3];
  }
}

MwndConfig::ButtonAction ParseSiglusButtonAction(std::string_view type,
                                                 int& option,
                                                 int& mode) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (start <= type.size()) {
    const std::size_t comma = type.find(',', start);
    fields.emplace_back(type.substr(start, comma - start));
    if (comma == std::string_view::npos)
      break;
    start = comma + 1;
  }
  if (fields.size() >= 2)
    option = ParseIndex(fields[1]).value_or(0);
  if (fields.size() >= 3)
    mode = ParseIndex(fields[2]).value_or(0);
  else if (fields.size() >= 2 &&
           (fields[0] == "read_skip" || fields[0] == "auto_mode"))
    mode = option;

  using Action = MwndConfig::ButtonAction;
  if (fields.empty() || fields[0] == "none")
    return Action::None;
  if (fields[0] == "close_mwnd")
    return Action::HideWindow;
  if (fields[0] == "save")
    return Action::Save;
  if (fields[0] == "load")
    return Action::Load;
  if (fields[0] == "qsave")
    return Action::QuickSave;
  if (fields[0] == "qload")
    return Action::QuickLoad;
  if (fields[0] == "return_sel")
    return Action::ReturnToSelection;
  if (fields[0] == "msg_log")
    return Action::MessageLog;
  if (fields[0] == "koe_play")
    return Action::ReplayVoice;
  if (fields[0] == "config")
    return Action::Config;
  if (fields[0] == "read_skip")
    return Action::ReadSkip;
  if (fields[0] == "auto_mode")
    return Action::AutoMode;
  if (fields[0] == "local_switch")
    return Action::LocalSwitch;
  if (fields[0] == "global_switch")
    return Action::GlobalSwitch;
  if (fields[0] == "local_mode")
    return Action::LocalMode;
  if (fields[0] == "global_mode")
    return Action::GlobalMode;
  return Action::None;
}

std::optional<MwndConfig::CallTarget> ParseCall(GameexeInterpretObject value) {
  auto scene = value.StrAt(0);
  if (!scene || scene->empty())
    return std::nullopt;
  MwndConfig::CallTarget result{.scene = *scene};
  if (auto entry = value.IntAt(1))
    result.entrypoint = *entry;
  else if (auto command = value.StrAt(1))
    result.command = *command;
  else
    return std::nullopt;
  return result;
}

}  // namespace

const MwndConfig::Window& MwndConfig::GetWindow(int id) const {
  return windows_.at(id);
}

const MwndConfig::Waku& MwndConfig::GetWaku(int set, int variant) const {
  return wakus_.at({set, variant});
}

const MwndConfig::Icon* MwndConfig::GetIcon(int id) const {
  const auto it = icons_.find(id);
  return it == icons_.end() ? nullptr : &it->second;
}

const MwndConfig::ButtonSound* MwndConfig::GetButtonSound(int id) const {
  if (id < 0 || static_cast<std::size_t>(id) >= button_sounds_.size())
    return nullptr;
  return &button_sounds_[id];
}

MwndConfig MwndConfig::ParseReallive(Gameexe& gexe) {
  MwndConfig result;
  result.button_actions_ = ButtonActionTable::ParseReallive(gexe);
  result.default_selection_window_ =
      gexe("DEFAULT_SEL_WINDOW").Int().value_or(1);
  result.window_attr_ = gexe("WINDOW_ATTR")
                            .IntVec()
                            .value_or(std::vector<int>{255, 255, 255, 255, 0});
  if (result.window_attr_.size() < 5)
    result.window_attr_ = {255, 255, 255, 255, 0};

  auto window_ids = FindIndices(gexe, "WINDOW.", 1);
  window_ids.insert(0);
  for (int id : window_ids) {
    auto source = gexe("WINDOW", id);
    Window window;
    window.screen_size = ReadScreenSize(gexe);
    window.window_attr_mod = source("ATTR_MOD").Int().value_or(0);
    const auto attr = window.window_attr_mod == 0
                          ? result.window_attr_
                          : source("ATTR").IntVec().value_or(
                                std::vector<int>{255, 255, 255, 255, 0});
    window.colour = ParseRGBA(attr);
    window.is_filter = attr.size() >= 5 && attr[4] != 0;
    window.default_font_size = source("MOJI_SIZE").Int().value_or(25);
    const auto cnt =
        source("MOJI_CNT").IntVec().value_or(std::vector<int>{26, 3});
    const auto rep =
        source("MOJI_REP").IntVec().value_or(std::vector<int>{-1, 10});
    const int ruby = source("LUBY_SIZE").Int().value_or(0);
    window.layout = MakeLayout(window.default_font_size, cnt[0], cnt[1], rep[0],
                               rep[1], ruby);
    const auto padding =
        source("MOJI_POS").IntVec().value_or(std::vector<int>{20, 20, 20, 20});
    window.upper_box_padding = padding[0];
    window.lower_box_padding = padding[1];
    window.left_box_padding = padding[2];
    window.right_box_padding = padding[3];
    const auto position =
        source("POS").IntVec().value_or(std::vector<int>{0, 50, 400});
    window.origin = position[0];
    window.x_distance_from_origin = position[1];
    window.y_distance_from_origin = position[2];
    window.default_colour =
        ParseRGB(gexe("COLOR_TABLE", 0)
                     .IntVec()
                     .value_or(std::vector<int>{255, 255, 255}));
    window.use_indentation = source("INDENT_USE").Int().value_or(1);
    const auto cursor =
        source("KEYCUR_MOD").IntVec().value_or(std::vector<int>{0, 0, 0});
    window.keycursor_type = cursor[0];
    window.keycursor_pos = Point(cursor[1], cursor[2]);
    window.action_on_pause = source("R_COMMAND_MOD").Int().value_or(0);
    window.waku_set = source("WAKU_SETNO").Int().value_or(0);
    window.name_mod = source("NAME_MOD").Int().value_or(0);
    if (window.name_mod == 1 && source("NAME_WAKU_SETNO").Exists()) {
      window.namebox.has_waku = true;
      window.namebox.waku_set = source("NAME_WAKU_SETNO").Int().value_or(0);
      window.namebox.x_spacing = source("NAME_MOJI_REP").Int().value_or(0);
      const auto pad =
          source("NAME_MOJI_POS").IntVec().value_or(std::vector<int>{0, 0});
      window.namebox.horizontal_padding = pad[0];
      window.namebox.vertical_padding = pad[1];
      const auto pos =
          source("NAME_POS").IntVec().value_or(std::vector<int>{0, 0});
      window.namebox.x_offset = pos[0];
      window.namebox.y_offset = pos[1];
      window.namebox.waku_dir_set = source("NAME_WAKU_DIR").Int().value_or(0);
      window.namebox.centering = source("NAME_CENTERING").Int().value_or(0);
      window.namebox.minimum_size = source("NAME_MOJI_MIN").Int().value_or(4);
      window.namebox.character_size =
          source("NAME_MOJI_SIZE").Int().value_or(0);
    }
    for (auto face : gexe.Filter(source.key() + ".FACE")) {
      const auto parts = face.GetKeyParts();
      if (parts.size() < 4)
        continue;
      const auto slot = ParseIndex(parts[3]);
      const auto values = face.IntVec();
      if (!slot || *slot < 0 || *slot >= kNumFaceSlots || !values ||
          values->size() < 5)
        continue;
      window.face_slots[*slot] = FaceSlot{
          (*values)[0], (*values)[1], (*values)[2], (*values)[3], (*values)[4]};
    }
    result.windows_[id] = std::move(window);
  }

  auto set_ids = FindIndices(gexe, "WAKU.", 1);
  for (const auto& entry : result.windows_)
    set_ids.insert(entry.second.waku_set);
  set_ids.insert(0);
  for (int set : set_ids) {
    auto set_source = gexe("WAKU", set);
    std::set<int> variants;
    for (auto record : gexe.Filter(set_source.key() + ".")) {
      const auto parts = record.GetKeyParts();
      if (parts.size() >= 4) {
        if (auto id = ParseIndex(parts[2]); id && *id >= 0)
          variants.insert(*id);
      }
    }
    variants.insert(0);
    for (int variant : variants) {
      auto source = gexe("WAKU", set, variant);
      Waku waku;
      waku.style = set_source("TYPE").Int().value_or(5) == 5
                       ? WakuStyle::Fixed
                       : WakuStyle::Stretch;
      waku.main_file = source("NAME").Str().value_or("");
      waku.filter_file = source("BACK").Str().value_or("");
      waku.draw_filter = !waku.filter_file.empty();
      const auto area =
          source("AREA").IntVec().value_or(std::vector<int>{0, 0, 0, 0});
      waku.filter_margin = Rect::GRP(area[2], area[0], area[3], area[1]);

      const std::string button_file = source("BTN").Str().value_or("");
      auto add_button = [&](std::string name, ButtonAction action,
                            int pattern) {
        auto values = source(name).IntVec();
        if (!values || values->size() != 5)
          return;
        Button button;
        button.name = std::move(name);
        button.file = button_file;
        button.position_base = (*values)[0];
        button.position = Point((*values)[1], (*values)[2]);
        button.explicit_size = Size((*values)[3], (*values)[4]);
        button.action = action;
        button.reallive_pattern = pattern;
        waku.buttons.emplace_back(std::move(button));
      };
      add_button("CLEAR_BOX", ButtonAction::ClearWindow, 8);
      add_button("MSGBKLEFT_BOX", ButtonAction::BackPage, 24);
      add_button("MSGBKRIGHT_BOX", ButtonAction::ForwardPage, 32);
      add_button("READJUMP_BOX", ButtonAction::ReadSkip, 104);
      add_button("AUTOMODE_BOX", ButtonAction::AutoMode, 112);
      for (int i = 0; i < 7; ++i) {
        const std::string name = std::format("EXBTN_{:03}_BOX", i);
        auto values = source(name).IntVec();
        if (!values || values->size() != 5)
          continue;
        Button button;
        button.name = name;
        button.file = button_file;
        button.position_base = (*values)[0];
        button.position = Point((*values)[1], (*values)[2]);
        button.explicit_size = Size((*values)[3], (*values)[4]);
        button.action = ButtonAction::RealliveFarcall;
        button.reallive_pattern = 40 + i * 8;
        button.call = CallTarget{
            .scene = std::to_string(gexe("WBCALL", i).Int().value_or(0)),
            .entrypoint = gexe("WBCALL", i).IntAt(1).value_or(0)};
        waku.buttons.emplace_back(std::move(button));
      }
      result.wakus_[{set, variant}] = std::move(waku);
    }
  }
  return result;
}

MwndConfig MwndConfig::ParseSiglus(Gameexe& gexe) {
  MwndConfig result;
  result.button_actions_ = ButtonActionTable::ParseSiglus(gexe);
  const RGBAColour configured_filter_colour =
      ParseRGBA(gexe("CONFIG.FILTER_COLOR")
                    .IntVec()
                    .value_or(std::vector<int>{0, 0, 0, 128}),
                RGBAColour(0, 0, 0, 128));
  result.window_attr_ = {
      configured_filter_colour.r(), configured_filter_colour.g(),
      configured_filter_colour.b(), configured_filter_colour.a(), 0};
  const int sound_count = std::clamp(gexe("BUTTON.SE.CNT").Int().value_or(16),
                                     0, kMaximumConfigEntries);
  result.button_sounds_.resize(sound_count);
  for (int id = 0; id < sound_count; ++id) {
    auto source = gexe("BUTTON", "SE", id);
    result.button_sounds_[id] = ButtonSound{
        .hit = source("HIT").Int().value_or(0),
        .push = source("PUSH").Int().value_or(-1),
        .decide = source("DECIDE").Int().value_or(1),
    };
  }
  result.default_window_ = gexe("MWND.DEFAULT_MWND_NO").Int().value_or(0);
  result.default_selection_window_ =
      gexe("MWND.DEFAULT_SEL_MWND_NO").Int().value_or(1);
  const int configured_count =
      gexe("MWND.CNT").Int().value_or(kDefaultSiglusWindowCount);
  const int window_count =
      std::clamp(std::max({configured_count, result.default_window_ + 1,
                           result.default_selection_window_ + 1, 0}),
                 0, kMaximumConfigEntries);
  const RGBColour default_colour =
      ParseRGB(gexe("COLOR_TABLE", 0)
                   .IntVec()
                   .value_or(std::vector<int>{255, 255, 255}));

  for (int id = 0; id < window_count; ++id) {
    int novel_mode = 0, extend_type = 0;
    int window_x = 50, window_y = 400, window_width = 700, window_height = 150;
    int message_x = 20, message_y = 20;
    int margin_left = 20, margin_top = 20, margin_right = 20,
        margin_bottom = 20;
    int count_x = 26, count_y = 3, font_size = 25;
    int space_x = -1, space_y = 10, ruby_size = 10, waku_no = 0;
    int name_mode = 0, name_extend = 0, name_x = 0, name_y = -100;
    int name_message_x = 8, name_message_y = 8;
    int name_margin_left = 8, name_margin_top = 8, name_margin_right = 8,
        name_margin_bottom = 8;
    int name_size = 16, name_space_x = -1, name_space_y = 8, name_count = 8,
        name_waku = -1;

    novel_mode = SiglusInt(gexe, id, "NOVEL_MODE").value_or(novel_mode);
    extend_type = SiglusInt(gexe, id, "EXTEND_TYPE").value_or(extend_type);
    ApplyPair(SiglusIntVec(gexe, id, "WINDOW_POS"), window_x, window_y);
    ApplyPair(SiglusIntVec(gexe, id, "WINDOW_SIZE"), window_width,
              window_height);
    ApplyPair(SiglusIntVec(gexe, id, "MESSAGE_POS"), message_x, message_y);
    ApplyRect(SiglusIntVec(gexe, id, "MESSAGE_MARGIN"), margin_left, margin_top,
              margin_right, margin_bottom);
    ApplyPair(SiglusIntVec(gexe, id, "MOJI_CNT"), count_x, count_y);
    font_size = SiglusInt(gexe, id, "MOJI_SIZE").value_or(font_size);
    ApplyPair(SiglusIntVec(gexe, id, "MOJI_SPACE"), space_x, space_y);
    ruby_size = SiglusInt(gexe, id, "RUBY_SIZE").value_or(ruby_size);
    waku_no = SiglusInt(gexe, id, "WAKU_NO").value_or(waku_no);
    name_mode = SiglusInt(gexe, id, "NAME_DISP_MODE").value_or(name_mode);
    name_extend = SiglusInt(gexe, id, "NAME_EXTEND_TYPE").value_or(name_extend);
    ApplyPair(SiglusIntVec(gexe, id, "NAME_WINDOW_POS"), name_x, name_y);
    ApplyPair(SiglusIntVec(gexe, id, "NAME_MESSAGE_POS"), name_message_x,
              name_message_y);
    ApplyRect(SiglusIntVec(gexe, id, "NAME_MESSAGE_MARGIN"), name_margin_left,
              name_margin_top, name_margin_right, name_margin_bottom);
    name_size = SiglusInt(gexe, id, "NAME_MOJI_SIZE").value_or(name_size);
    ApplyPair(SiglusIntVec(gexe, id, "NAME_MOJI_SPACE"), name_space_x,
              name_space_y);
    name_count = SiglusInt(gexe, id, "NAME_MOJI_CNT").value_or(name_count);
    name_waku = SiglusInt(gexe, id, "NAME_WAKU_NO").value_or(name_waku);

    Window window;
    window.screen_size = ReadScreenSize(gexe);
    window.colour = configured_filter_colour;
    window.default_font_size = font_size;
    window.layout =
        MakeLayout(font_size, count_x, count_y, space_x, space_y, ruby_size);
    if (extend_type == 1) {
      window.upper_box_padding = margin_top;
      window.lower_box_padding = margin_bottom;
      window.left_box_padding = margin_left;
      window.right_box_padding = margin_right;
    } else {
      window.upper_box_padding = std::max(message_y, 0);
      window.left_box_padding = std::max(message_x, 0);
      window.right_box_padding = std::max(
          window_width - message_x - window.layout.GetNormalSize().width(), 0);
      window.lower_box_padding = std::max(
          window_height - message_y - window.layout.GetNormalSize().height(),
          0);
    }
    window.x_distance_from_origin = window_x;
    window.y_distance_from_origin = window_y;
    window.default_colour = default_colour;
    window.action_on_pause = novel_mode;
    window.waku_set = waku_no;
    window.name_mod = name_mode == 2 ? 2 : (name_mode == 0 && name_waku >= 0);
    if (window.name_mod == 1 && name_waku >= 0) {
      window.namebox.has_waku = true;
      window.namebox.waku_set = name_waku;
      window.namebox.x_spacing = name_space_x;
      window.namebox.horizontal_padding =
          name_extend == 1 ? name_margin_left : name_message_x;
      window.namebox.vertical_padding =
          name_extend == 1 ? name_margin_top : name_message_y;
      window.namebox.x_offset = name_x;
      window.namebox.y_offset = name_y;
      window.namebox.minimum_size = name_count;
      window.namebox.character_size = name_size;
    }
    result.windows_[id] = std::move(window);
  }

  const int icon_count =
      std::clamp(gexe("ICON.CNT").Int().value_or(0), 0, kMaximumConfigEntries);
  for (int id = 0; id < icon_count; ++id) {
    auto source = gexe("ICON", id);
    result.icons_[id] = Icon{.file = source("FILE").Str().value_or(""),
                             .pattern_count = source("CNT").Int().value_or(1),
                             .speed_ms = source("SPEED").Int().value_or(100)};
  }

  const int waku_count =
      std::clamp(gexe("WAKU.CNT").Int().value_or(0), 0, kMaximumConfigEntries);
  const int button_count = std::clamp(gexe("WAKU.BTN.CNT").Int().value_or(0), 0,
                                      kMaximumConfigEntries);
  const int face_count = std::clamp(gexe("WAKU.FACE.CNT").Int().value_or(0), 0,
                                    kMaximumConfigEntries);
  const int object_count = std::clamp(gexe("WAKU.OBJECT.CNT").Int().value_or(0),
                                      0, kMaximumConfigEntries);
  for (int id = 0; id < waku_count; ++id) {
    auto source = gexe("WAKU", id);
    Waku waku;
    waku.filter_rendering = FilterRendering::AlphaBlend;
    waku.style = source("EXTEND_TYPE").Int().value_or(0) == 1
                     ? WakuStyle::Stretch
                     : WakuStyle::Fixed;
    waku.main_file = source("WAKU_FILE").Str().value_or("");
    waku.filter_file = source("FILTER_FILE").Str().value_or("");
    waku.draw_filter = true;
    const auto margin =
        source("FILTER_MARGIN").IntVec().value_or(std::vector<int>{0, 0, 0, 0});
    waku.filter_margin = Rect::GRP(margin[0], margin[1], margin[2], margin[3]);
    const auto colour = source("FILTER_COLOR")
                            .IntVec()
                            .value_or(std::vector<int>{0, 0, 255, 128});
    waku.filter_colour = ParseRGBA(colour, RGBAColour(0, 0, 255, 128));
    waku.use_config_colour =
        source("FILTER_CONFIG_COLOR").Int().value_or(1) != 0;
    waku.use_config_opacity = source("FILTER_CONFIG_TR").Int().value_or(1) != 0;
    waku.key_icon_no = source("ICON_NO").Int().value_or(-1);
    waku.page_icon_no = source("PAGE_ICON_NO").Int().value_or(-1);
    waku.icon_position_type = source("ICON_POS_TYPE").Int().value_or(0);
    const auto icon_pos =
        source("ICON_POS").IntVec().value_or(std::vector<int>{0, 0, 0});
    waku.icon_position_base = icon_pos[0];
    waku.icon_position = Point(icon_pos[1], icon_pos[2]);
    waku.object_count = object_count;

    for (int button_id = 0; button_id < button_count; ++button_id) {
      auto button_source = source("BTN", button_id);
      const std::string file = button_source("FILE").Str().value_or("");
      if (file.empty())
        continue;
      Button button;
      button.name = std::format("BTN.{:03}", button_id);
      button.file = file;
      button.slot_id = button_id;
      button.cut_no = button_source("CUT_NO").Int().value_or(0);
      const auto pos =
          button_source("POS").IntVec().value_or(std::vector<int>{0, 0, 0});
      button.position_base = pos[0];
      button.position = Point(pos[1], pos[2]);
      button.action_no = button_source("ACTION").Int().value_or(0);
      button.se_no = button_source("SE").Int().value_or(0);
      auto type_source = button_source("TYPE");
      std::string type = type_source.StrAt(0).value_or("none");
      if (auto value = type_source.IntAt(1))
        type += "," + std::to_string(*value);
      if (auto value = type_source.IntAt(2))
        type += "," + std::to_string(*value);
      button.action =
          ParseSiglusButtonAction(type, button.action_option, button.mode);
      switch (button.action) {
        case ButtonAction::ReadSkip:
        case ButtonAction::AutoMode:
        case ButtonAction::LocalSwitch:
        case ButtonAction::GlobalSwitch:
        case ButtonAction::LocalMode:
        case ButtonAction::GlobalMode:
          button.required_mode = button.mode;
          break;
        default:
          break;
      }
      button.call = ParseCall(button_source("CALL"));
      button.frame_action = ParseCall(button_source("FRAME_ACTION"));
      waku.buttons.emplace_back(std::move(button));
    }
    for (int face = 0; face < face_count; ++face) {
      const auto pos =
          source("FACE", face, "POS").IntVec().value_or(std::vector<int>{0, 0});
      waku.face_positions.emplace_back(pos[0], pos[1]);
    }
    result.wakus_[{id, 0}] = std::move(waku);
  }

  if (result.wakus_.empty()) {
    Waku waku;
    waku.filter_rendering = FilterRendering::AlphaBlend;
    waku.filter_colour = RGBAColour(0, 0, 255, 128);
    result.wakus_[{0, 0}] = std::move(waku);
  }
  return result;
}
