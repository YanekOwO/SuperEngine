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
#include <iterator>
#include <utility>

SelectionPoint::SelectionPoint(std::size_t capacity)
    : history_(std::max<std::size_t>(1, capacity)) {}

void SelectionPoint::SetCurrentSnapshot(CheckpointRef snapshot) {
  if (!snapshot)
    current_.reset();
  else
    current_ = std::move(snapshot);
}

void SelectionPoint::Append(const std::optional<CheckpointRef>& snapshot) {
  if (!snapshot)
    return;
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
  current_.reset();
  stock_.reset();
  Clear();
}

SelectionPoint::State SelectionPoint::GetState() const {
  std::vector<CheckpointRef> history(history_.cbegin(), history_.cend());
  return {current_, stock_, std::move(history)};
}

void SelectionPoint::RestoreState(State state) {
  if (state.current && !*state.current)
    state.current.reset();
  if (state.stock && !*state.stock)
    state.stock.reset();
  std::erase_if(state.history,
                [](const auto& checkpoint) { return !checkpoint; });

  current_ = std::move(state.current);
  stock_ = std::move(state.stock);
  history_.assign(history_.capacity(),
                  std::make_move_iterator(state.history.begin()),
                  std::make_move_iterator(state.history.end()));
}
