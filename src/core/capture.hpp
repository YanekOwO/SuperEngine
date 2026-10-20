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

#include "core/rect.hpp"

#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class Gameexe;
class SDLSurface;

struct CaptureConfig {
  bool save_thumbnail = false;
  Size thumbnail_size{200, 150};
  bool png_thumbnail = false;
  Size screen_size;
  static CaptureConfig Parse(Gameexe& gameexe, Size screen_size);
};

enum class CaptureKind { Save, Object, Tweet, Local, Buffer };
struct CaptureRequest {
  CaptureKind kind = CaptureKind::Save;
  std::pair<int, int> end{std::numeric_limits<int>::max(),
                          std::numeric_limits<int>::max()};
  bool display = true;
  Size size;
  Point offset;
  std::string png_name;
  int priority = 3;  // Siglus NONE/SAVE/END/CAPTURE priorities are 0/1/2/3.
};

struct CaptureFlags {
  std::vector<int> integers;
  std::vector<std::string> strings;
  bool empty() const { return integers.empty() && strings.empty(); }
};

// Owns snapshots and capture file operations. The caller schedules captures at
// frame boundaries; the renderer never advances state or presents the window.
class CaptureManager {
 public:
  using Render =
      std::function<std::shared_ptr<SDLSurface>(const CaptureRequest&)>;
  using Load =
      std::function<std::shared_ptr<SDLSurface>(const std::filesystem::path&)>;
  using LiveImages = std::function<std::vector<std::string>()>;
  using Clock = std::function<int()>;
  CaptureManager(CaptureConfig config,
                 std::filesystem::path save_path,
                 Render render,
                 Load load,
                 LiveImages live_images = {},
                 Clock clock = {});

  // Normalizes dimensions and buffer presentation, rejecting invalid requests.
  // A disabled or lower-priority save capture needs no frame and returns empty.
  std::optional<CaptureRequest> PrepareRequest(CaptureRequest request) const;
  // Executes a prepared request. Local captures return their file timestamp;
  // other kinds return empty. Save priority is checked again before rendering.
  std::optional<int> Capture(const CaptureRequest& request);
  // Clears snapshots and priority, retaining configuration and thumbnail table.
  void Reset();

  void Release(CaptureKind kind, int priority = 3);
  std::shared_ptr<SDLSurface> Snapshot(CaptureKind kind) const;
  int save_priority() const { return save_priority_; }
  void FromFile(const std::filesystem::path& filename);
  void CreateBuffer(Size size);
  void DestroyBuffer();
  std::shared_ptr<SDLSurface> LoadThumbnail(int number, bool local);
  void LoadThumbnailTable(const std::filesystem::path& path);

  bool Export(const std::filesystem::path& path,
              std::string extension,
              const CaptureFlags& flags = {}) const;
  static CaptureFlags ImportFlags(const std::filesystem::path& path);
  std::filesystem::path FilePath(std::string name, std::string extension) const;

 private:
  CaptureConfig config_;
  std::filesystem::path save_path_;
  Render render_;
  Load load_;
  LiveImages live_images_;
  Clock clock_;
  std::shared_ptr<SDLSurface> save_, object_, tweet_, buffer_;
  int save_priority_ = 0;
  std::map<std::string, std::string> thumbnail_table_;
};
