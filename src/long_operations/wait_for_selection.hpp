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

#pragma once

#include "core/selection.hpp"
#include "long_operations/long_operation.hpp"

#include <functional>
#include <memory>

namespace lop {

class WaitForSelection final : public LongOperation {
 public:
  WaitForSelection(
      std::shared_ptr<SelState> state,
      std::shared_ptr<Selection> sel,
      std::function<void(std::shared_ptr<Selection>)> finish_cb,
      std::function<void(std::shared_ptr<Selection>)> cancel_cb = {},
      std::function<void(std::shared_ptr<Event>)> event_cb = {});
  ~WaitForSelection() override;

  Task<int> Run(WaitContext& waits) override;
  void OnEvent(std::shared_ptr<Event> event) override;

 private:
  std::shared_ptr<SelState> state_;
  std::shared_ptr<Selection> sel_;
  std::function<void(std::shared_ptr<Selection>)> finish_cb_;
  std::function<void(std::shared_ptr<Selection>)> cancel_cb_;
  std::function<void(std::shared_ptr<Event>)> event_cb_;
};

}  // namespace lop
