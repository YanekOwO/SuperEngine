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

#include "core/game_config.hpp"
#include "core/selection_point.hpp"

#include <boost/serialization/array.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class Gameexe;

namespace libsiglus {

struct SaveCounts {
  int normal = 10;
  int quick = 3;
  int end = 0;
  int inner = 0;
  int selection = 1;

  static SaveCounts Parse(Gameexe& gameexe);
  SaveCounts Clamped() const;
};

struct SaveLocalExtra {
  std::array<ExtraSwitchConfig, 4> switches{};
  std::array<ExtraModeConfig, 4> modes{};

  bool operator==(const SaveLocalExtra&) const = default;
  void serialize(auto& ar, const unsigned int) { ar & switches & modes; }
};

struct LocalStateBlob {
  std::string runtime_state;
  std::vector<CheckpointRef> inner;
  CheckpointRef stock;
  // Selection identities are weak references: evicted points are not retained
  // merely because a surviving checkpoint remembers their position in history.
  std::vector<CheckpointId> selection_ids;

  bool operator==(const LocalStateBlob&) const = default;
  void serialize(auto& ar, const unsigned int) {
    ar & runtime_state & inner & stock & selection_ids;
  }
};

struct SlotPayload {
  CheckpointRef active;
  std::vector<CheckpointRef> selection;
  std::map<CheckpointId, LocalStateBlob> blobs;
  SaveLocalExtra extra;

  bool operator==(const SlotPayload&) const = default;
  void serialize(auto& ar, const unsigned int) {
    ar & active & selection & blobs & extra;
  }
};

struct SaveHeader {
  int year = 0, month = 0, day = 0, weekday = 0;
  int hour = 0, minute = 0, second = 0, millisecond = 0;
  std::string title, message, full_message, comment, append_dir, append_name;
  std::array<int, 256> values{};

  bool operator==(const SaveHeader&) const = default;
  void serialize(auto& ar, const unsigned int) {
    ar & year & month & day & weekday & hour & minute & second & millisecond &
        title & message & full_message & comment & append_dir & append_name &
        values;
  }
};

// Stores one copy of each captured local state. References copied into inner
// saves and selection history never recursively contain other state blobs.
class SaveManager {
 public:
  using Clock = std::function<SaveHeader()>;
  SaveManager(std::filesystem::path directory,
              SaveCounts counts,
              SelectionPoint& selection,
              Clock clock = {});

  inline const SaveCounts& counts() const { return counts_; }
  inline const std::string& last_error() const { return error_; }

  std::filesystem::path SlotPath(int slot) const;
  std::filesystem::path ThumbnailPath(int slot) const;

  // must be png or bmp
  void SetThumbnailExtension(std::string extension);

  // slot
  std::optional<int> NormalSlot(int slot) const;
  // |normal| + slot
  std::optional<int> QuickSlot(int slot) const;
  // |normal| + |quick| + slot
  std::optional<int> EndSlot(int offset = 0) const;

  CheckpointId Capture(std::string runtime_state,
                       std::string title = {},
                       std::string append_dir = {},
                       std::string append_name = {});
  void AppendMessage(std::string_view text);
  void AppendMessage(std::string_view text, std::string_view full_text);
  void ClearCheckpoint();
  void ResetMessages(std::string_view full_message);
  bool HasCheckpoint() const { return static_cast<bool>(active_); }
  const CheckpointRef& active() const { return active_; }
  const LocalStateBlob& Blob(CheckpointId id) const;
  std::size_t blob_count() const { return blobs_.size(); }

  bool SaveInner(int slot);
  bool ClearInner(int slot);
  bool CopyInner(int from, int to);
  bool HasInner(int slot) const;
  CheckpointRef GetInner(int slot) const;
  std::optional<CheckpointRef> LatestSelection() const;

  SlotPayload MakePayload(const SaveLocalExtra& extra = {}) const;
  SlotPayload MakePayloadFor(const CheckpointRef& checkpoint,
                             const SaveLocalExtra& extra = {}) const;
  // Validation is read-only. Restore commits storage only after the caller has
  // successfully reconstructed the runtime state from the selected blob.
  static void Validate(const SlotPayload& payload);
  void RestorePayload(const SlotPayload& payload);
  void Prune();
  void Reset();

  bool SaveSlot(int slot,
                const SaveLocalExtra& extra = {},
                std::optional<std::string> thumbnail_bytes = std::nullopt);
  std::optional<SlotPayload> LoadSlot(int slot) const;
  std::optional<SaveHeader> ReadHeader(int slot) const;
  bool Exists(int slot) const;
  // Quick lookup arguments and result are quick-relative. Equal timestamps
  // select the later candidate, consistently in both lookup forms.
  int Newest(bool quick = false) const;
  int Newest(int start, int count, bool quick = false) const;
  bool SetComment(int slot, std::string_view text);
  bool SetValues(int slot, std::span<const int> values);
  bool CopySlot(int from, int to);
  bool SwapSlots(int first, int second);
  bool DeleteSlot(int slot);

 private:
  struct StoredFile {
    SaveHeader header;
    std::uint64_t raw_size = 0;
    std::string compressed;
  };
  const std::filesystem::path directory_;
  SaveCounts counts_;
  SelectionPoint& selection_;
  Clock clock_;
  std::string thumbnail_extension_;
  CheckpointId next_id_ = 1;
  CheckpointRef active_;
  std::map<CheckpointId, LocalStateBlob> blobs_;
  std::vector<CheckpointRef> inner_;
  mutable std::string error_;

  bool ValidSlot(int slot) const;
  bool ValidInner(int slot) const;
  StoredFile ReadFile(int slot) const;
  static std::string EncodeFile(const StoredFile& file);
  bool RewriteHeader(int slot, const std::function<void(SaveHeader&)>& update);
};

}  // namespace libsiglus
