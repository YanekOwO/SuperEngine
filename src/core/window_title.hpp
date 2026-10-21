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

#include <functional>
#include <string>

class Gameexe;

class WindowTitle {
 public:
  // Display text is stored as UTF-8.
  std::string base_title_;
  std::string subtitle_;
  bool subtitle_visible_ = false;

  using TitlePublisher = std::function<void(const std::string&)>;
  explicit WindowTitle(TitlePublisher publisher);

  std::string GetTitle() const;
  void Apply() const;

  void LoadReallive(Gameexe& gexe);
  void LoadSiglus(Gameexe& gexe);

 private:
  TitlePublisher publisher_;
};
