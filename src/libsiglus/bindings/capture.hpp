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

#include "core/capture.hpp"
#include "vm/value.hpp"

#include <string>
#include <vector>

namespace libsiglus::binding {

CaptureRequest DecodeCaptureRequest(CaptureKind kind,
                                    std::vector<serilang::Value> raw);

struct CaptureFileOptions {
  std::string name, extension, title;
  bool dialog = false;
  serilang::Value integers = serilang::nil, strings = serilang::nil;
  int int_start = 0, int_count = 0, str_start = 0, str_count = 0;
};

CaptureFileOptions DecodeCaptureFileOptions(std::vector<serilang::Value> raw,
                                            bool save);

}  // namespace libsiglus::binding
