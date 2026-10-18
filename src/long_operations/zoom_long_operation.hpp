// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2007 Elliot Glaysher
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
//
// -----------------------------------------------------------------------

#pragma once

#include <memory>

#include "core/rect.hpp"
#include "long_operations/long_operation.hpp"

class RLMachine;
class SDLSurface;

namespace lop {

class ZoomLongOperation : public LongOperation {
 public:
  ZoomLongOperation(RLMachine& machine,
                    const std::shared_ptr<SDLSurface>& orig_surface_,
                    const std::shared_ptr<SDLSurface>& src_surface_,
                    const Rect& frect_,
                    const Rect& trect_,
                    const Rect& drect_,
                    const int time);
  virtual ~ZoomLongOperation();

  Task<int> Run(WaitContext& waits) override;

 private:
  RLMachine& machine_;

  std::shared_ptr<SDLSurface> orig_surface_;
  std::shared_ptr<SDLSurface> src_surface_;

  const Rect frect_;
  const Rect trect_;
  const Rect drect_;
  const unsigned int duration_;

  unsigned int start_time_;
};

}  // namespace lop
