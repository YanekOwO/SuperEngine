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

#pragma once

#include "core/button_action_table.hpp"
#include "core/colour.hpp"
#include "core/rect.hpp"
#include "core/text_layout.hpp"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class Gameexe;

class MwndConfig {
 public:
  static constexpr int kNumFaceSlots = 8;

  struct FaceSlot {
    int x = 0;
    int y = 0;
    int is_behind = 0;
    int hide_other_windows = 0;
    int unknown = 0;

    bool operator==(const FaceSlot&) const = default;
  };

  struct Namebox {
    bool has_waku = false;
    int waku_set = 0;
    int x_spacing = 0;
    int horizontal_padding = 0;
    int vertical_padding = 0;
    int x_offset = 0;
    int y_offset = 0;
    int waku_dir_set = 0;
    int centering = 0;
    int minimum_size = 4;
    int character_size = 0;

    bool operator==(const Namebox&) const = default;
  };

  struct Window {
    Size screen_size;
    TextLayout layout{0, 0, 0};
    int default_font_size = 25;
    int window_attr_mod = 0;
    int waku_set = 0;
    RGBAColour colour = RGBAColour::White();
    bool is_filter = false;
    RGBColour default_colour = RGBColour::White();
    int use_indentation = 1;
    int action_on_pause = 0;
    int origin = 0;
    int x_distance_from_origin = 50;
    int y_distance_from_origin = 400;
    int upper_box_padding = 20;
    int lower_box_padding = 20;
    int left_box_padding = 20;
    int right_box_padding = 20;
    int keycursor_type = 0;
    Point keycursor_pos;
    int name_mod = 0;
    Namebox namebox;
    std::array<std::optional<FaceSlot>, kNumFaceSlots> face_slots;
  };

  struct Icon {
    std::string file;
    int pattern_count = 1;
    int speed_ms = 100;

    bool operator==(const Icon&) const = default;
  };

  enum class WakuStyle { Fixed, Stretch };
  enum class FilterRendering { ColorMask, AlphaBlend };
  enum class ButtonAction {
    None,
    HideWindow,
    Save,
    Load,
    QuickSave,
    QuickLoad,
    ReturnToSelection,
    MessageLog,
    ReplayVoice,
    Config,
    ReadSkip,
    AutoMode,
    LocalSwitch,
    GlobalSwitch,
    LocalMode,
    GlobalMode,
    ClearWindow,
    BackPage,
    ForwardPage,
    RealliveFarcall,
  };

  struct CallTarget {
    std::string scene;
    std::optional<std::string> command;
    std::optional<int> entrypoint;

    bool operator==(const CallTarget&) const = default;
  };

  struct Button {
    std::string name;
    std::string file;
    // The source WAKU.BTN slot. Siglus syscom addresses buttons by this id;
    // it must not be replaced by the compacted vector index.
    int slot_id = -1;
    int cut_no = 0;
    int position_base = 0;
    Point position;
    std::optional<Size> explicit_size;
    int action_no = 0;
    int se_no = -1;
    ButtonAction action = ButtonAction::None;
    int action_option = 0;
    int mode = 0;
    // Siglus uses paired buttons for mode-dependent actions. Only the button
    // whose required mode matches the current value is registered or drawn.
    std::optional<int> required_mode;
    std::optional<CallTarget> call;
    std::optional<CallTarget> frame_action;
    int reallive_pattern = 0;

    bool operator==(const Button&) const = default;
  };

  struct ButtonSound {
    int hit = 0;
    int push = -1;
    int decide = 1;

    bool operator==(const ButtonSound&) const = default;
  };

  struct Waku {
    WakuStyle style = WakuStyle::Fixed;
    FilterRendering filter_rendering = FilterRendering::ColorMask;
    std::string main_file;
    std::string filter_file;
    Rect filter_margin;
    RGBAColour filter_colour = RGBAColour(0, 0, 0, 128);
    bool use_config_colour = false;
    bool use_config_opacity = false;
    bool draw_filter = false;
    int key_icon_no = -1;
    int page_icon_no = -1;
    int icon_position_type = 0;
    int icon_position_base = 0;
    Point icon_position;
    std::vector<Button> buttons;
    std::vector<Point> face_positions;
    int object_count = 0;

    bool operator==(const Waku&) const = default;
  };

  static MwndConfig ParseReallive(Gameexe& gexe);
  static MwndConfig ParseSiglus(Gameexe& gexe);

  int default_window() const { return default_window_; }
  int default_selection_window() const { return default_selection_window_; }
  const std::vector<int>& window_attr() const { return window_attr_; }
  const ButtonActionTable& button_actions() const { return button_actions_; }
  const ButtonSound* GetButtonSound(int id) const;

  const Window& GetWindow(int id) const;
  const Waku& GetWaku(int set, int variant = 0) const;
  const Icon* GetIcon(int id) const;
  bool HasWindow(int id) const { return windows_.contains(id); }
  bool HasWaku(int set, int variant = 0) const {
    return wakus_.contains({set, variant});
  }

 private:
  int default_window_ = 0;
  int default_selection_window_ = 1;
  std::vector<int> window_attr_{255, 255, 255, 255, 0};
  ButtonActionTable button_actions_;
  std::vector<ButtonSound> button_sounds_;
  std::map<int, Window> windows_;
  std::map<std::pair<int, int>, Waku> wakus_;
  std::map<int, Icon> icons_;
};
