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

// Message-window (MWND) configuration template.
// Waku is the Reallive term for a window frame, including its filter and
// buttons.
class MwndConfig {
 public:
  // Number of per-window RealLive FACE slots.
  static constexpr int kNumFaceSlots = 8;

  // RealLive face graphic settings, positioned relative to the message window.
  struct FaceSlot {
    // Horizontal face position in pixels from the window's left edge.
    int x = 0;
    // Vertical face position in pixels from the window's top edge.
    int y = 0;
    // Drawing order: 1 draws before the name window, 0 draws after it.
    int is_behind = 0;
    // Hide other message windows when this face graphic is opened or closed.
    int hide_other_windows = 0;
    int unknown = 0;

    bool operator==(const FaceSlot&) const = default;
  };

  struct Namebox {
    struct SiglusLayout {
      bool auto_size = false;
      // NAME_WINDOW_ALIGN: 0 left, 1 center, 2 right of NAME_WINDOW_POS.
      int alignment = 0;
      Size window_size{300, 100};
      Point message_position{8, 8};
      Point message_offset;
      Rect message_margin = Rect::GRP(8, 8, 8, 8);
      // The legacy engine expands automatic name frames by MESSAGE_MARGIN.
      Rect window_margin = Rect::GRP(20, 20, 20, 20);

      Size GetContentSize(Size text_size) const;
      Rect GetWindowRect(Point anchor, Size waku_size, Size text_size) const;
      Point GetTextOrigin(Point anchor, Size text_size) const;

      bool operator==(const SiglusLayout&) const = default;
    };

    // Native Siglus anchor and text positioning, independent of RealLive's
    // bottom-left offset and padding convention.
    std::optional<SiglusLayout> siglus_layout;
    // Whether a separate name-window waku was configured.
    bool has_waku = false;
    // Name-window frame number (NAME_WAKU_SETNO / NAME_WAKU_NO).
    int waku_set = 0;
    // Horizontal space between name characters, in pixels (name_moji_space).
    int x_spacing = 0;
    // Horizontal margin for the name text, in pixels.
    int horizontal_padding = 0;
    // Vertical margin for the name text, in pixels.
    int vertical_padding = 0;
    // Horizontal name-window offset from the message window's left edge.
    int x_offset = 0;
    // Vertical name-window bottom offset from the message window's top edge.
    int y_offset = 0;
    // RealLive NAME_WAKU_DIR setting.
    int waku_dir_set = 0;
    // Nonzero enables the name waku's horizontal/vertical centering.
    int centering = 0;
    // Minimum name-window width in character cells (name_moji_cnt).
    int minimum_size = 4;
    // Name character size in pixels, used to size the name window.
    int character_size = 0;

    bool operator==(const Namebox&) const = default;
  };

  // Message window settings, adapted from RealLive WINDOW or Siglus MWND.
  struct Window {
    // Screen dimensions in pixels, used to resolve the window position.
    Size screen_size;
    // Message area, moji spacing, and ruby size derived from the text settings.
    TextLayout layout{0, 0, 0};
    // Default message character size in pixels (moji_size).
    int default_font_size = 25;
    // RealLive ATTR_MOD: 0 uses global WINDOW_ATTR;
    // nonzero uses this window's ATTR.
    int window_attr_mod = 0;
    // Message-window frame number (WAKU_SETNO / waku_no).
    int waku_set = 0;
    // Window background/filter RGBA colour; each channel ranges from 0 to 255.
    RGBAColour colour = RGBAColour::White();
    // RealLive WINDOW_ATTR filter flag, passed to colour-mask rendering.
    bool is_filter = false;
    // Initial message text colour, taken from COLOR_TABLE.000.
    RGBColour default_colour = RGBColour::White();
    // RealLive indentation flag (INDENT_USE): 0 off, nonzero on.
    int use_indentation = 1;
    // Pause behavior: 0 starts a new page, nonzero starts a new paragraph.
    // RealLive R_COMMAND_MOD or Siglus novel_mode (0 AVG, 1 novel).
    int action_on_pause = 0;
    // Screen corner: 0 top left, 1 top right, 2 bottom left, 3 bottom right.
    int origin = 0;
    // Horizontal distance in pixels inward from the selected screen corner.
    int x_distance_from_origin = 50;
    // Vertical distance in pixels inward from the selected screen corner.
    int y_distance_from_origin = 400;
    // Top message margin in pixels (msg_pos or msg_margin).
    int upper_box_padding = 20;
    // Bottom message margin in pixels, derived from the window size or margin.
    int lower_box_padding = 20;
    // Left message margin in pixels (msg_pos or msg_margin).
    int left_box_padding = 20;
    // Right message margin in pixels, derived from the window size or margin.
    int right_box_padding = 20;
    // RealLive key-wait cursor: 0 text area's bottom right, 1 after the last
    // character, 2 at keycursor_pos relative to the text area's top left.
    int keycursor_type = 0;
    // Key-wait cursor offset in pixels; used only when keycursor_type is 2.
    Point keycursor_pos;
    // Speaker-name mode: 0 inline, 1 separate name window, 2 hidden
    // (NAME_DISP_MODE)
    int name_mod = 0;
    // Separate speaker-name window configuration, used when name_mod is 1.
    Namebox namebox;
    // RealLive FACE slots; absent entries have no configured face graphic.
    std::array<std::optional<FaceSlot>, kNumFaceSlots> face_slots;
  };

  // Animated key-wait or page-wait icon.
  struct Icon {
    // Icon image filename (file_name).
    std::string file;
    // Number of animation patterns (anime_pat_cnt).
    int pattern_count = 1;
    // Time per animation pattern in milliseconds (anime_speed).
    int speed_ms = 100;

    bool operator==(const Icon&) const = default;
  };

  // Frame sizing (extend_type).
  enum class WakuStyle {
    // Fixed-size frame (Siglus extend_type 0, RealLive TYPE 5).
    Fixed,
    // Variable-size frame stretched around the message area.
    Stretch
  };
  enum class FilterRendering {
    // RealLive backing image rendered as a colour mask.
    ColorMask,
    // Siglus filter image rendered with its texture colour and alpha.
    AlphaBlend
  };
  // Button system type (sys_type), plus RealLive waku button actions.
  enum class ButtonAction {
    // No system type action (none); a CALL may still be configured.
    None,
    // Hide the message window (close_mwnd).
    HideWindow,
    // Open the save interface (save).
    Save,
    // Open the load interface (load).
    Load,
    // Quick save (qsave).
    QuickSave,
    // Quick load (qload).
    QuickLoad,
    // Return to the previous selection (return_sel).
    ReturnToSelection,
    // Open message history (msg_log / MSG_BACK).
    MessageLog,
    // Replay voice (koe_play).
    ReplayVoice,
    // Open configuration (config).
    Config,
    // Control read skip (read_skip).
    ReadSkip,
    // Control automatic message advance (auto_mode).
    AutoMode,
    // Control a local extra switch (local_switch).
    LocalSwitch,
    // Control a global extra switch (global_switch).
    GlobalSwitch,
    // Control a local extra mode (local_mode).
    LocalMode,
    // Control a global extra mode (global_mode).
    GlobalMode,
    // Toggle interface visibility through RealLive CLEAR_BOX.
    ClearWindow,
    // Move back through message history (MSGBKLEFT_BOX).
    BackPage,
    // Move forward through message history (MSGBKRIGHT_BOX).
    ForwardPage,
    // Run the WBCALL farcall assigned to a RealLive EXBTN slot.
    RealliveFarcall,
  };

  // Script destination for a button CALL or FRAME_ACTION.
  struct CallTarget {
    // Call scene name (scn_name); RealLive stores the scene number as a string.
    std::string scene;
    // Optional Siglus command name (cmd_name), instead of a numeric label.
    std::optional<std::string> command;
    // Optional Siglus Z-label number (z_no), or RealLive farcall entrypoint.
    std::optional<int> entrypoint;

    bool operator==(const CallTarget&) const = default;
  };

  // Message-window waku button
  struct Button {
    // Configuration key and runtime lookup name, e.g. BTN.000 or CLEAR_BOX.
    std::string name;
    // Button image filename (btn_file).
    std::string file;
    // The source WAKU.BTN slot. Siglus syscom addresses buttons by this id;
    // it must not be replaced by the compacted vector index.
    int slot_id = -1;
    // Siglus base cut/pattern number, before button action corrections.
    int cut_no = 0;
    // Waku corner (pos_base): 0 top left, 1 top right,
    // 2 bottom left, 3 bottom right.
    int position_base = 0;
    // Button position in pixels, measured inward from position_base.
    Point position;
    // RealLive button width/height; absent means use the image cut's size.
    std::optional<Size> explicit_size;
    // Button action template number (action_no), selecting per-state drawing.
    int action_no = 0;
    // Button SE template number (BUTTON.SE); -1 disables button sounds.
    int se_no = -1;
    // System type action (sys_type), executed unless CALL is configured.
    ButtonAction action = ButtonAction::None;
    // System type option (sys_type_opt), e.g. the extra switch or mode index.
    int action_option = 0;
    // Button mode number (btn_mode), used for mode-dependent system types.
    int mode = 0;
    // Siglus uses paired buttons for mode-dependent actions. Only the button
    // whose required mode matches the current value is registered or drawn.
    std::optional<int> required_mode;
    // Optional CALL destination, taking precedence over the system type action.
    std::optional<CallTarget> call;
    // Configured FRAME_ACTION scene/command destination for the button.
    std::optional<CallTarget> frame_action;
    // Base pattern in RealLive's shared BTN image, fixed for each button type.
    int reallive_pattern = 0;

    bool operator==(const Button&) const = default;
  };

  // Button sound-effect template (BUTTON.SE); negative SE numbers mean silence.
  struct ButtonSound {
    // SE number for HIT, when the cursor hits the button.
    int hit = 0;
    // SE number for PUSH, when the button is pushed.
    int push = -1;
    // SE number for DECIDE, when the button is confirmed.
    int decide = 1;

    bool operator==(const ButtonSound&) const = default;
  };

  // Message-window frame template.
  struct Waku {
    // Fixed or variable frame size (extend_type).
    WakuStyle style = WakuStyle::Fixed;
    // Rendering method for the background filter image.
    FilterRendering filter_rendering = FilterRendering::ColorMask;
    // Frame image filename (waku_file / RealLive NAME).
    std::string main_file;
    // Filter image filename (filter_file / RealLive BACK); may be empty.
    std::string filter_file;
    // Filter margins in pixels from the window: left, top, right, bottom.
    // The Rect holds margin widths in x/y/x2/y2 (legacy filter_margin).
    Rect filter_margin;
    // Filter RGBA colour, used for a generated backing when no file exists.
    RGBAColour filter_colour = RGBAColour(0, 0, 0, 128);
    // Apply configured filter RGB (filter_config_color); Siglus adds it to RGB.
    bool use_config_colour = false;
    // Apply configured transparency (filter_config_tr).
    // Siglus multiplies texture alpha by the configured opacity / 255.
    bool use_config_opacity = false;
    // Draw the background filter, generating a backing if no image is supplied.
    bool draw_filter = false;
    // Key-wait icon number (icon_no); -1 selects no icon.
    int key_icon_no = -1;
    // Page-wait icon number (page_icon_no); -1 selects no icon.
    int page_icon_no = -1;
    // Icon position type (icon_pos_type): 0 uses a waku corner;
    // 1 follows the text insertion point.
    int icon_position_type = 0;
    // Icon corner (icon_pos_base): 0 top left, 1 top right,
    // 2 bottom left, 3 bottom right; used for position type 0.
    int icon_position_base = 0;
    // Icon offsets in pixels from icon_position_base, used for position type 0.
    Point icon_position;
    // Configured frame buttons; Siglus buttons retain their source slot_id.
    std::vector<Button> buttons;
    // Reserved Siglus button_list slots (WAKU.BTN.CNT), including empty FILEs.
    int button_count = 0;
    // Face graphic positions in pixels relative to the waku (face_pos).
    std::vector<Point> face_positions;
    // Number of reserved Siglus waku object slots (WAKU.OBJECT.CNT).
    int object_count = 0;

    bool operator==(const Waku&) const = default;
  };

  static MwndConfig ParseReallive(Gameexe& gexe);
  static MwndConfig ParseSiglus(Gameexe& gexe);

  inline int default_window() const { return default_window_; }
  bool layered_windows() const { return layered_windows_; }

  int window_capacity() const {
    return windows_.empty() ? 0 : windows_.rbegin()->first + 1;
  }
  inline int default_selection_window() const {
    return default_selection_window_;
  }
  inline const std::vector<int>& window_attr() const { return window_attr_; }
  inline const ButtonActionTable& button_actions() const {
    return button_actions_;
  }
  const ButtonSound* GetButtonSound(int id) const;

  const Window& GetWindow(int id) const;
  const Waku& GetWaku(int set, int variant = 0) const;
  const Icon* GetIcon(int id) const;
  inline bool HasWindow(int id) const { return windows_.contains(id); }
  inline bool HasWaku(int set, int variant = 0) const {
    return wakus_.contains({set, variant});
  }

  bool layered_windows_ = false;
  int order_ = 1;
  // Default message-window number (default_mwnd_no); RealLive defaults to 0.
  int default_window_ = 0;
  // Default selection-window number (default_sel_mwnd_no / DEFAULT_SEL_WINDOW).
  int default_selection_window_ = 1;
  // Global background attributes: red, green, blue, opacity, and filter flag.
  // Siglus adapts CONFIG.FILTER_COLOR to this layout with a zero filter flag.
  std::vector<int> window_attr_{255, 255, 255, 255, 0};
  // Button action templates, indexed by each button's action_no.
  ButtonActionTable button_actions_;
  // Button SE templates, indexed by each button's se_no.
  std::vector<ButtonSound> button_sounds_;
  // Parsed message windows indexed by WINDOW / MWND number.
  std::map<int, Window> windows_;
  // Frame templates indexed by (set number, variant); Siglus uses variant 0.
  std::map<std::pair<int, int>, Waku> wakus_;
  // Animated wait icons indexed by ICON number.
  std::map<int, Icon> icons_;
};
