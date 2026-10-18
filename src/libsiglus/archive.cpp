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

#include "libsiglus/archive.hpp"

#include "core/compression.hpp"
#include "encodings/utf16.hpp"
#include "libsiglus/cracker.hpp"
#include "libsiglus/xorkey.hpp"
#include "log/domain_logger.hpp"
#include "utilities/byte_reader.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace libsiglus {

static DomainLogger logger("Archive");

Archive Archive::Create(std::string_view raw_data) {
  for (auto it = ExekeyRegistry::cbegin(); it != ExekeyRegistry::cend(); ++it) {
    try {
      const auto& [name, key] = *it;
      static DomainLogger logger("Archive");
      logger(Severity::Info) << "Decrypt using exekey: " << name;
      return Archive(raw_data, key);
    } catch (...) {
    }
  }

  // fallback
  if (raw_data.size() < sizeof(Pack_hdr))
    throw std::runtime_error("Archive::Create: truncated archive header.");
  Pack_hdr header;
  std::memcpy(&header, raw_data.data(), sizeof(header));
  xorkey_t key{};
  if (header.scn_data_exe_angou_mod != 0 && header.scn_data_cnt != 0) {
    if (header.scn_data_cnt < 0 ||
        header.scn_data_index_cnt < header.scn_data_cnt ||
        header.scn_data_index_list_ofs < 0 || header.scn_data_list_ofs < 0 ||
        static_cast<std::size_t>(header.scn_data_index_list_ofs) >
            raw_data.size() ||
        static_cast<std::size_t>(header.scn_data_list_ofs) > raw_data.size())
      throw std::runtime_error("Archive::Create: invalid scene index.");
    const auto count = static_cast<std::size_t>(header.scn_data_cnt);
    const auto index = raw_data.substr(header.scn_data_index_list_ofs);
    const auto data = raw_data.substr(header.scn_data_list_ofs);
    if (count > index.size() / 8)
      throw std::runtime_error("Archive::Create: truncated scene index.");
    ByteReader reader(index);
    std::vector<std::string_view> scenes;
    scenes.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      const auto offset = reader.PopAs<uint32_t>(4);
      const auto size = reader.PopAs<uint32_t>(4);
      if (offset > data.size() || size > data.size() - offset)
        throw std::runtime_error("Archive::Create: truncated scene data.");
      scenes.push_back(data.substr(offset, size));
    }

    key = CrackScene(scenes);
    logger(Severity::Info) << "Cracked xorkey: " << ToHex(key);
  }

  return Archive(raw_data, key);
}

Archive::Archive(std::string_view data, const xorkey_t& key)
    : data_(data), key_(key) {
  hdr_ = reinterpret_cast<Pack_hdr const*>(data_.data());
  ParseScndata();
  CreateScnMap();

  ParseIncprop();
  CreateIncpropMap();

  ParseIncCmd();
  CreateIncCmdMap();

  data_ = std::string_view();
  hdr_ = nullptr;
}

Archive::Archive(Archive&& other) noexcept
    : data_(other.data_),
      key_(other.key_),
      hdr_(reinterpret_cast<Pack_hdr const*>(data_.data())),
      raw_scene_data_(std::move(other.raw_scene_data_)),
      scene_names_(std::move(other.scene_names_)),
      scn_map_(std::move(other.scn_map_)),
      prop_(std::move(other.prop_)),
      prop_map_(std::move(other.prop_map_)),
      cmd_(std::move(other.cmd_)),
      cmd_map_(std::move(other.cmd_map_)) {
  hdr_ = reinterpret_cast<Pack_hdr const*>(data_.data());
}

Scene Archive::ParseScene(int id) const {
  return Scene(raw_scene_data_[id], id, scene_names_[id]);
}

void Archive::ParseScndata() {
  raw_scene_data_.clear();
  raw_scene_data_.reserve(hdr_->scn_data_cnt);
  ByteReader reader(data_.substr(hdr_->scn_data_index_list_ofs,
                                 8 * hdr_->scn_data_index_cnt));

  for (int i = 0; i < hdr_->scn_data_cnt; ++i) {
    auto offset = reader.PopAs<uint32_t>(4);
    auto size = reader.PopAs<uint32_t>(4);

    std::string scene_data(
        data_.substr(offset + hdr_->scn_data_list_ofs, size));
    Decrypt(scene_data);
    scene_data = Decompress_lzss(scene_data);

    raw_scene_data_.emplace_back(std::move(scene_data));
  }
}

void Archive::Decrypt(std::string& scene_data) {
  if (hdr_->scn_data_exe_angou_mod != 0) {
    for (size_t i = 0; i < scene_data.length(); ++i) {
      scene_data[i] ^= key_[i & 0xf];
    }
  }

  for (size_t i = 0; i < scene_data.length(); ++i) {
    scene_data[i] ^= easykey[i & 0xff];
  }
}

void Archive::CreateScnMap() {
  std::u16string_view names =
      sv_to_u16sv(data_.substr(hdr_->scn_name_list_ofs));
  ByteReader reader(data_.substr(hdr_->scn_name_index_list_ofs,
                                 8 * hdr_->scn_name_index_cnt));

  // map from scene name to scene id
  scene_names_.reserve(hdr_->scn_name_cnt);
  for (int i = 0; i < hdr_->scn_name_cnt; ++i) {
    auto offset = reader.PopAs<uint32_t>(4);
    auto size = reader.PopAs<uint32_t>(4);
    const auto scnname = utf16le::Decode(names.substr(offset, size));

    scn_map_.emplace(scnname, i);
    scene_names_.emplace_back(std::move(scnname));
  }
}

void Archive::ParseIncprop() {
  ByteReader reader(data_.substr(hdr_->inc_prop_list_ofs,
                                 sizeof(Property) * hdr_->inc_prop_cnt));
  for (int i = 0; i < hdr_->inc_prop_cnt; ++i) {
    Property incprop;
    incprop.form = static_cast<Type>(reader.PopAs<int32_t>(4));
    incprop.size = reader.PopAs<int32_t>(4);
    prop_.emplace_back(std::move(incprop));
  }
}

void Archive::CreateIncpropMap() {
  ByteReader reader(data_.substr(hdr_->inc_prop_name_index_list_ofs,
                                 8 * hdr_->inc_prop_name_cnt));
  std::u16string_view props =
      sv_to_u16sv(data_.substr(hdr_->inc_prop_name_list_ofs));

  for (int i = 0; i < hdr_->inc_prop_name_cnt; ++i) {
    auto offset = reader.PopAs<uint32_t>(4);
    auto size = reader.PopAs<uint32_t>(4);
    const auto name = utf16le::Decode(props.substr(offset, size));
    prop_map_.emplace(name, i);
    prop_[i].name = name;
  }
}

void Archive::ParseIncCmd() {
  ByteReader reader(data_.substr(hdr_->inc_cmd_list_ofs,
                                 sizeof(Command) * hdr_->inc_cmd_cnt));
  for (int i = 0; i < hdr_->inc_cmd_cnt; ++i) {
    Command inccmd;
    inccmd.scene_id = reader.PopAs<int32_t>(4);
    inccmd.offset = reader.PopAs<int32_t>(4);
    cmd_.emplace_back(std::move(inccmd));
  }
}

void Archive::CreateIncCmdMap() {
  ByteReader reader(data_.substr(hdr_->inc_cmd_name_index_list_ofs,
                                 8 * hdr_->inc_cmd_name_index_cnt));
  std::u16string_view cmds =
      sv_to_u16sv(data_.substr(hdr_->inc_cmd_name_list_ofs));

  for (int i = 0; i < hdr_->inc_cmd_name_cnt; ++i) {
    auto offset = reader.PopAs<uint32_t>(4);
    auto size = reader.PopAs<uint32_t>(4);
    const auto name = utf16le::Decode(cmds.substr(offset, size));
    cmd_map_.emplace(name, i);
    cmd_[i].name = name;
  }
}

}  // namespace libsiglus
