// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2009 Elliot Glaysher
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

#include "long_operations/wait_for_selection.hpp"

#include <stdexcept>
#include <utility>

namespace lop {

WaitForSelection::WaitForSelection(
    std::shared_ptr<SelState> state,
    std::shared_ptr<Selection> sel,
    std::function<void(std::shared_ptr<Selection>)> finish_cb,
    std::function<void(std::shared_ptr<Selection>)> cancel_cb,
    std::function<void(std::shared_ptr<Event>)> event_cb)
    : state_(state),
      sel_(sel),
      finish_cb_(std::move(finish_cb)),
      cancel_cb_(std::move(cancel_cb)),
      event_cb_(std::move(event_cb)) {}

void WaitForSelection::OnEvent(std::shared_ptr<Event> event) {
  if (event_cb_)
    event_cb_(std::move(event));
}

WaitForSelection::~WaitForSelection() {
  if (cancel_cb_)
    cancel_cb_(sel_);
}

Task<int> WaitForSelection::Run(WaitContext& waits) {
  while (!sel_->CanResume()) {
    if (state_->active_ != sel_) {
      [[unlikely]] throw std::runtime_error(
          "Selection was replaced before completion");
    }
    co_await waits.Schedule(false);
  }
  finish_cb_(sel_);
  cancel_cb_ = {};  // finished, no cancellation cleanup
  co_return *sel_->result;
}

}  // namespace lop
