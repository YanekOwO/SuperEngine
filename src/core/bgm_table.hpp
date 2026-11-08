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
// -----------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <string>
#include <vector>

class Gameexe;

class BgmTable {
 public:
  BgmTable(std::vector<std::string> names, std::vector<bool> listened);
  static BgmTable CreateFromSiglus(Gameexe& gameexe);

  void SetListen(std::string name, bool value, bool warn_if_unknown = true);
  void SetListenAll(bool value);
  int GetListen(std::string name) const;
  std::size_t Count() const;
  std::vector<bool> ListenState() const { return listened_; }
  void RestoreListenState(std::vector<bool> listened);

 private:
  int Find(const std::string& name) const;

  std::vector<std::string> names_;
  std::vector<bool> listened_;
};
