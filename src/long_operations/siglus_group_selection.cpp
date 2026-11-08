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

#include "long_operations/siglus_group_selection.hpp"

namespace lop {

SiglusGroupSelection::SiglusGroupSelection(std::shared_ptr<Group> group,
                                           std::function<void()> consume_input)
    : group_(std::move(group)), consume_input_(std::move(consume_input)) {}

SiglusGroupSelection::~SiglusGroupSelection() {
  if (!finished_ && group_ && group_->status == Group::Status::Waiting) {
    group_->status = Group::Status::Disabled;
    group_->cancel_enabled = false;
    group_->ClearTransientInteraction();
  }
}

Task<int> SiglusGroupSelection::Run(WaitContext& waits) {
  while (group_->status == Group::Status::Waiting)
    co_await waits.Schedule(false);
  finished_ = true;
  if (consume_input_)
    consume_input_();
  co_return group_->decided_button_no.value_or(-1);
}

}  // namespace lop
