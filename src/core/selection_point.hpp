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

#include <boost/circular_buffer.hpp>
#include <boost/serialization/string.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

using CheckpointId = std::uint64_t;

// Metadata is copied with each reference, independently of the immutable state
// blob. Later message output cannot change a previously stocked checkpoint.
struct CheckpointRef {
  CheckpointId id = 0;
  std::string title, message, full_message, append_dir, append_name;

  explicit operator bool() const { return id != 0; }
  bool operator==(const CheckpointRef&) const = default;
  void serialize(auto& ar, const unsigned int) {
    ar & id & title & message & full_message & append_dir & append_name;
  }
};

// Selection points copy the latest savepoint rather than capturing live state.
// Storage is shared by all script machines and survives EX calls.
class SelectionPoint {
 public:
  struct State {
    std::optional<CheckpointRef> current;
    std::optional<CheckpointRef> stock;
    std::vector<CheckpointRef> history;
  };

  explicit SelectionPoint(std::size_t capacity = 1);
  State GetState() const;
  // Restores copied state while retaining this instance's configured capacity.
  void RestoreState(State state);

  // A zero checkpoint ID means no savepoint is available.
  void SetCurrentSnapshot(CheckpointRef snapshot);

  void Mark();
  void Clear();
  bool HasPoint() const;
  // Stack overwrites one temporary slot; it does not push a nested stack.
  void Stack();
  // Append the stocked snapshot and consume it, even when no data is present.
  void Drop();
  // Clear all snapshots while retaining the configured history capacity.
  void Reset();

  inline std::span<const CheckpointRef> history() {
    return std::span<const CheckpointRef>(history_.linearize(),
                                          history_.size());
  }

 private:
  void Append(const std::optional<CheckpointRef>& snapshot);

  std::optional<CheckpointRef> current_;
  std::optional<CheckpointRef> stock_;
  boost::circular_buffer<CheckpointRef> history_;
};
