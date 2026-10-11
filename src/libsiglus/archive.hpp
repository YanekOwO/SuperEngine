// -----------------------------------------------------------------------
//
// This file is part of RLVM
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

#pragma once

#include "libsiglus/property.hpp"
#include "libsiglus/scene.hpp"
#include "libsiglus/xorkey.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace libsiglus {

struct Pack_hdr {
  int32_t header_size;
  int32_t inc_prop_list_ofs;
  int32_t inc_prop_cnt;
  int32_t inc_prop_name_index_list_ofs;
  int32_t inc_prop_name_index_cnt;
  int32_t inc_prop_name_list_ofs;
  int32_t inc_prop_name_cnt;
  int32_t inc_cmd_list_ofs;
  int32_t inc_cmd_cnt;
  int32_t inc_cmd_name_index_list_ofs;
  int32_t inc_cmd_name_index_cnt;
  int32_t inc_cmd_name_list_ofs;
  int32_t inc_cmd_name_cnt;
  int32_t scn_name_index_list_ofs;
  int32_t scn_name_index_cnt;
  int32_t scn_name_list_ofs;
  int32_t scn_name_cnt;
  int32_t scn_data_index_list_ofs;
  int32_t scn_data_index_cnt;
  int32_t scn_data_list_ofs;
  int32_t scn_data_cnt;
  int32_t scn_data_exe_angou_mod;
  int32_t original_source_header_size;
};

class Archive {
 public:
  static Archive Create(std::string_view raw_data);

  Archive(std::string_view data, const xorkey_t& key);
  Archive(const Archive&) = delete;
  Archive& operator=(const Archive&) = delete;
  Archive(Archive&& other) noexcept;
  Archive& operator=(Archive&& other) = delete;

  Scene ParseScene(int id) const;

  inline size_t GetScenarioCount() const noexcept {
    return raw_scene_data_.size();
  }

 private:
  void ParseScndata();
  void Decrypt(std::string& scene_data);
  void CreateScnMap();

  void ParseIncprop();
  void CreateIncpropMap();

  void ParseIncCmd();
  void CreateIncCmdMap();

 public:
  std::string_view data_;
  const xorkey_t& key_;
  Pack_hdr const* hdr_;

  std::vector<std::string> raw_scene_data_;
  std::vector<std::string> scene_names_;
  std::map<std::string, int> scn_map_;

  std::vector<Property> prop_;
  std::map<std::string, int> prop_map_;

  std::vector<Command> cmd_;
  std::map<std::string, int> cmd_map_;
};

}  // namespace libsiglus
