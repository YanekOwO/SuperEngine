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

#pragma once

#include "core/button_action_table.hpp"
#include "core/colour.hpp"
#include "core/rect.hpp"
#include "long_operations/siglus_wait.hpp"

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

class Gameexe;
class SDLSurface;
class TextWindow;
class System;

// One script option. Its position in SelectionRequest::options is also the
// result index, including entries that are disabled or hidden.
struct SelectionOption {
  std::string text;
  // Native types: 0 = off (skipped during placement), 1 = selectable,
  // 2 = visible but disabled, 3 = hidden but reserves a layout slot.
  int type = 1;
  // COLOR_TABLE index; a negative value inherits the menu's normal text color.
  int color = -1;
};

// Normalized command arguments shared by the selection state and its runtime.
struct SelectionRequest {
  enum class Kind { Window, Buttons };
  Kind kind = Kind::Window;
  std::vector<SelectionOption> options;
  bool cancel_enabled = false;
  // sel closes its window when the VM resumes; selmsg leaves it visible.
  bool close_window = true;
  // SELBTN template number, used only by button selections.
  int template_no = 0;
  // Read-history key supplied by the compiler. A negative component disables
  // recording; choosing an option or canceling records the same key.
  int scene = -1;
  int kidoku = -1;
  // Once a result exists, resume at: 0 = Finished, 1 = Closing (or Finished),
  // 2 = immediately. Presentation may continue after the VM resumes.
  int sync_type = 0;
  // Capture the screen after opening. The optional start callback runs only
  // when capture is enabled and start_scene is nonempty.
  bool capture = false;
  std::string start_scene;
  // Scene entry label for the opening callback; a negative value disables it.
  int start_entry = 0;
};

// Gameexe SELBTN.n settings. Positions and spacing are in pixels; animation
// durations are in milliseconds. Missing settings retain these defaults.
struct BtnSelTemplate {
  enum class Format { Siglus, Reallive };
  Format format = Format::Siglus;
  std::string base_file, filter_file;
  // Menu anchor, offset between slots, and text anchor within each button.
  Point base_pos, rep_pos, text_pos;
  // Alignment values: 0 = start, 1 = center, 2 = end on the corresponding axis.
  // A positive max_rows wraps placement to columns separated by column_width.
  int x_align = 0, y_align = 0, max_rows = 0, column_width = 100;
  int font_size = 25, x_spacing = 0, y_spacing = 0, text_count = 0;
  int text_x_align = 0, text_y_align = 0;
  // COLOR_TABLE indices and an optional BTN_ACTION index (-1 uses defaults).
  int color = 0, hit_color = 5, action = -1;

  // RealLive centers against the screen and supplies actions directly.
  bool center_x = false, center_y = false;
  std::optional<ButtonActionTable::Entry> frame_actions;

  // Open/close types: 0 = skip, 1 = fade, 2/3 = vertical slides,
  // 4/5 = horizontal slides, 6 = alternating horizontal slides.
  // Slide types stagger rows by 50 ms in addition to the duration below.
  int open_type = 1, open_time = 500;
  int close_type = 1, close_time = 500;
  // Decide types: 0 = skip, 1 = fade other options, 2 = also move them toward
  // the chosen option. Canceling skips this phase.
  int decide_type = 1, decide_time = 500;

  // Rejects template numbers outside SELBTN.CNT (default count: 16).
  static BtnSelTemplate ParseSiglus(Gameexe& gexe, int number);
  static BtnSelTemplate ParseReallive(Gameexe& gexe, int number);
};

// Stage-owned selection presentation.
class Selection {
 public:
  // Start: Prepared -> Opening -> Waiting.
  // Choose: Waiting -> Deciding -> Closing -> Finished.
  // Window menus and disabled animations skip phases.
  enum class Phase { Prepared, Opening, Waiting, Deciding, Closing, Finished };
  struct Item {
    // Layout coordinates relative to origin, or to the window's text surface.
    Point position;
    Rect hit_rect;
    std::shared_ptr<SDLSurface> normal, highlighted;
    // Logical text/button size, excluding text-surface shadow padding.
    Size size;
  };
  struct Presentation {
    // Transient drawing adjustments; layout and hit rectangles stay fixed.
    Point offset;
    int alpha = 255;
  };

  SelectionRequest request;
  BtnSelTemplate config;
  // One entry per request option, preserving script indices through layout.
  std::vector<Item> items;
  std::shared_ptr<SDLSurface> base, filter, capture;
  std::shared_ptr<TextWindow> window;
  // Render order and layer, also used to decide participation in stage wipes.
  std::pair<int, int> sorter{1, 0};
  ButtonActionTable::Entry button_action;
  // RealLive BACK masks inherit the current message window's attributes.
  RGBAColour filter_colour = RGBAColour::White();
  bool filter_mode = false;
  Point origin;
  // Original option indices. result is unset until a choice is accepted;
  // -1 represents a player cancellation when cancel_enabled is true.
  std::optional<int> focused, pressed, result;

  // Set by the runtime's opening hook; Waiting alone does not enable input.
  bool open_hook_done = false;

  Selection(SelectionRequest request, BtnSelTemplate config = {});

  // Places buttons of a common size, then aligns the whole menu to base_pos.
  void LayoutButtons(Size item_size, Size screen = {});

  // Uses premeasured item sizes. Fixed windows fill columns within area;
  // variable windows stack options from start_y and return their fitted size.
  Size LayoutWindow(Size area,
                    int font_size,
                    int line_height,
                    int start_y,
                    bool variable);

  // Starts a prepared session. The runtime performs the opening hook in Update.
  void Start();
  // Negative deltas are ignored. Each call crosses at most one timed phase
  // boundary and does not carry overshoot into the next animation.
  void Advance(int milliseconds);
  // Accepts a selectable original option index, or -1 for allowed cancellation.
  // Returns false without changing state if input or the choice is invalid.
  bool Choose(int index);
  bool CanInput() const;
  // Tests result availability and sync_type, independently of visibility.
  bool CanResume() const;
  bool Visible() const;
  Phase phase() const { return phase_; }
  // Animation timer in milliseconds.
  int elapsed() const { return elapsed_; }
  // Computes the current animation without changing state; index is an
  // original option index and screen supplies the slide distance.
  Presentation GetPresentation(int index, Size screen) const;
  // Copies phase, timing, result, and shared visual resources for a stage wipe,
  // clearing focus and press state. Callers may rebind window to the copy in
  // the destination stage layer.
  std::shared_ptr<Selection> Clone() const;

  Point Origin() const;

 private:
  Phase phase_ = Phase::Prepared;
  int elapsed_ = 0;
  int AnimationRows() const;
  void Close();
};

// Resolves surfaces, text styling, and hit geometry. Window requests require
// a target window; button requests use a null window. Runtime hooks stay with
// the caller, including stage ordering, opening, history, and completion.
std::shared_ptr<Selection> BuildSelection(
    System& system,
    SelectionRequest request,
    BtnSelTemplate config,
    std::shared_ptr<TextWindow> window = nullptr);

struct SelState {
  explicit SelState();
  ~SelState();

  // Draws an active session or stage snapshot without advancing it. The caller
  // supplies stage ordering and transition compositing.
  static void Render(const Selection& session, System& system);

  struct InputResult {
    bool focus_changed = false;
    std::optional<int> choice;
  };
  // Updates the active session and consumes selection input. Sound, history,
  // and engine-specific menus are handled by the caller from this feedback.
  InputResult HandleEvent(std::shared_ptr<Event> event);
  // Accepts a choice without input events and updates the last selected text.
  bool Choose(int index);

  std::unique_ptr<lop::PendingCoroutineTasks> pending_;
  // prepared_ is consumed by StartPrepared; active_ is also referenced by its
  // waiter and normally the foreground stage slot.
  std::shared_ptr<Selection> prepared_, active_;
  // Text of the last accepted choice, or "（キャンセル）" for player
  // cancellation.
  std::string last_message_;
  Point mouse_;
  // Track presses accepted while input is enabled, so an opening animation
  // cannot turn a previously held key/button release into a choice.
  std::set<KeyCode> held_keys_;
  bool cancel_pressed_ = false;
};
