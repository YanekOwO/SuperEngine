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

#include "core/window_title.hpp"

#include "core/gameexe.hpp"
#include "utilities/string_utilities.hpp"

#include <utility>

WindowTitle::WindowTitle(WindowTitle::TitlePublisher pub)
    : publisher_(std::move(pub)) {}

std::string WindowTitle::GetTitle() const {
  if (!subtitle_visible_ || subtitle_.empty())
    return base_title_;

  if (base_title_.empty())
    return subtitle_;

  return base_title_ + ": " + subtitle_;
}

void WindowTitle::Apply() const { publisher_(GetTitle()); }

void WindowTitle::LoadReallive(Gameexe& gexe) {
  base_title_ =
      cp932toUTF8(gexe("CAPTION").ToStr(), gexe("NAME_ENC").Int().value_or(0));
  subtitle_.clear();
  subtitle_visible_ = gexe("SUBTITLE").ReadBool(false);
}

void WindowTitle::LoadSiglus(Gameexe& gexe) {
  base_title_ =
      cp932toUTF8(gexe("CAPTION").ToStr(), gexe("NAME_ENC").Int().value_or(0));
  subtitle_.clear();
  subtitle_visible_ = false;
}
