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

#include "log/core.hpp"

#include <iostream>
#include <sstream>
#include <string>

// Capture the synchronous application sinks, restoring their startup defaults.
class LogCapture {
 public:
  LogCapture()
      : saved_options_{IsTraceEnabled(TraceCategory::Script),
                       IsTraceEnabled(TraceCategory::FrameAction)},
        saved_enabled_(logging_enabled),
        saved_buffer_(std::clog.rdbuf(output_.rdbuf())) {}
  ~LogCapture() {
    std::clog.rdbuf(saved_buffer_);
    logging_enabled = saved_enabled_;
    SetupLogging(Severity::None, saved_options_);
  }

  std::string Str() const { return output_.str(); }

 private:
  TraceOptions saved_options_;
  bool saved_enabled_;
  std::ostringstream output_;
  std::streambuf* saved_buffer_;
};
