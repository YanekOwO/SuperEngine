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

#include "libsiglus/archive.hpp"
#include "libsiglus/lexeme.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace libsiglus::binding {

// A single-use builder for a scene with an empty script and one command at
// byte offset 5. Append the command body before calling AddTo.
struct FakeScene {
  std::string scene_name;
  std::string command_name;
  std::vector<Type> args;
  bool included = false;
  std::string body;

  // Register the serialized scene and its local or included command.
  // Zlabel 0 points to the command entry. This consumes the body and finishes
  // the builder's use.
  int AddTo(Archive& archive) {
    const int scene_id = archive.raw_scene_data_.size();
    const std::string payload = std::move(body);

    body = std::string(5, '\0');
    body[0] = static_cast<char>(ByteCode::Return);
    const int32_t offset = body.size();
    for (std::size_t i = 0; i < args.size(); ++i) {
      AppendByte(ByteCode::Declare);
      AppendInt(static_cast<int32_t>(args[i]));
      AppendInt(i);
    }
    AppendByte(ByteCode::Arg);
    body += payload;
    AppendByte(ByteCode::Return);
    body.append(4, '\0');
    AppendByte(ByteCode::End);
    Scene_hdr header{
        .header_size = sizeof(Scene_hdr),
        .scene_offset = sizeof(Scene_hdr),
        .scene_size = static_cast<int32_t>(body.size()),
        .zlabel_list_offset =
            static_cast<int32_t>(sizeof(Scene_hdr) + body.size()),
        .zlabel_cnt = 1,
    };
    const std::u16string name(command_name.begin(), command_name.end());
    if (!included) {
      header.cmdlist_offset = header.zlabel_list_offset + sizeof(int32_t);
      header.cmd_cnt = 1;
      header.cmd_nameidx_offset = header.cmdlist_offset + sizeof(int32_t);
      header.cmd_nameidx_cnt = 1;
      header.cmd_name_offset = header.cmd_nameidx_offset + 2 * sizeof(int32_t);
      header.cmd_name_cnt = name.size();
    }

    body = std::string(reinterpret_cast<const char*>(&header), sizeof(header)) +
           body;
    AppendInt(offset);
    if (!included) {
      AppendInt(offset);
      AppendInt(0);
      AppendInt(name.size());
      body.append(reinterpret_cast<const char*>(name.data()),
                  name.size() * sizeof(char16_t));
    } else {
      archive.cmd_map_[command_name] = archive.cmd_.size();
      archive.cmd_.push_back(Command{
          .scene_id = scene_id, .offset = offset, .name = command_name});
    }
    archive.raw_scene_data_.push_back(std::move(body));
    archive.scene_names_.push_back(scene_name);
    archive.scn_map_[scene_name] = scene_id;
    return scene_id;
  }

  void AppendByte(ByteCode code) { body += static_cast<char>(code); }

  void AppendInt(int32_t value) {
    body.append(reinterpret_cast<const char*>(&value), sizeof(value));
  }

  void AppendElement(std::initializer_list<int> elements) {
    AppendByte(ByteCode::Marker);
    for (int element : elements) {
      AppendByte(ByteCode::Push);
      AppendInt(static_cast<int32_t>(Type::Int));
      AppendInt(element);
    }
  }
};

}  // namespace libsiglus::binding
