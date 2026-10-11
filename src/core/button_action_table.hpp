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

#include "core/rect.hpp"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

class Gameexe;

enum class ButtonState { Normal, Hit, Push, Select, Disable };
std::optional<ButtonState> ParseState(std::string_view state);

class ButtonActionTable {
 public:
  // Button action template pattern: drawing settings for a single state.
  struct State {
    // Pattern number, added to an object's |pat_no|.
    int pattern = 0;
    // Drawing position correction in pixels, added to the base position.
    Point rep_pos = Point(0, 0);
    // Transparency correction (0..255): an object's tr is multiplied by
    // rep_tr / 255; 0 is fully transparent and 255 leaves tr unchanged.
    int rep_tr = 255;
    // Brightness correction (0..255), added to an object's bright value.
    int rep_bright = 0;
    // Darkness correction (0..255), added to an object's dark value.
    int rep_dark = 0;

    bool operator==(const State&) const = default;
  };

  // Button action template
  struct Entry {
    // NORMAL: the button is idle.
    State normal;
    // HIT: the cursor hits the button, or its group designates it as hit.
    State hit;
    // PUSH: the button is pushed, or its group has decided this button.
    State push;
    // SELECT: the button is explicitly in the selected state.
    State select;
    // DISABLE: the button is disabled and cannot accept button events.
    State disable;

    State GetState(ButtonState state) const;
    void SetState(ButtonState state, State value);

    bool operator==(const Entry&) const = default;
  };

  static ButtonActionTable ParseSiglus(Gameexe& gexe);
  static ButtonActionTable ParseReallive(Gameexe& gexe);

  inline std::size_t GetCount() const { return cnt_; }
  inline Entry GetEntry(std::size_t idx) const { return entry_.at(idx); }
  inline auto GetAllEntry() const { return entry_; }

 private:
  std::size_t cnt_ = 0;
  std::vector<Entry> entry_;
};
