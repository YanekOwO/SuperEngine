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

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Selection points copy the latest savepoint rather than capturing live state.
// Storage is shared by all script machines and survives EX calls.
class SelectionPoint {
 public:
  // Opaque serialized state, including embedded null bytes. Snapshot capture
  // and restoration are not implemented; only the host can supply this data.
  using Snapshot = std::string;

  explicit SelectionPoint(int capacity = 1);

  // An empty snapshot means no savepoint is available.
  void SetCurrentSnapshot(Snapshot snapshot);
  void ClearCurrentSnapshot();

  void Mark();
  void Clear();
  bool HasPoint() const;
  // Stack overwrites one temporary slot; it does not push a nested stack.
  void Stack();
  // Append the stocked snapshot and consume it, even when no data is present.
  void Drop();
  // Clear all snapshots while retaining the configured history capacity.
  void Reset();

  std::span<const Snapshot> history() const { return history_; }

 private:
  void Append(const std::optional<Snapshot>& snapshot);

  std::size_t capacity_;
  std::optional<Snapshot> current_;
  std::optional<Snapshot> stock_;
  std::vector<Snapshot> history_;
};
