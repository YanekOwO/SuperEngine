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

#include "core/selection_point.hpp"

#include <algorithm>
#include <utility>

SelectionPoint::SelectionPoint(int capacity)
    : capacity_(std::clamp(capacity, 1, 100)) {}

void SelectionPoint::SetCurrentSnapshot(Snapshot snapshot) {
  if (snapshot.empty())
    ClearCurrentSnapshot();
  else
    current_ = std::move(snapshot);
}

void SelectionPoint::ClearCurrentSnapshot() { current_.reset(); }

void SelectionPoint::Append(const std::optional<Snapshot>& snapshot) {
  if (!snapshot)
    return;
  if (history_.size() == capacity_)
    history_.erase(history_.begin());
  history_.push_back(*snapshot);
}

void SelectionPoint::Mark() { Append(current_); }

void SelectionPoint::Clear() { history_.clear(); }

bool SelectionPoint::HasPoint() const { return !history_.empty(); }

void SelectionPoint::Stack() { stock_ = current_; }

void SelectionPoint::Drop() {
  Append(stock_);
  stock_.reset();
}

void SelectionPoint::Reset() {
  ClearCurrentSnapshot();
  stock_.reset();
  Clear();
}
