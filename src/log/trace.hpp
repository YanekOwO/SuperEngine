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

#include <functional>
#include <string>
#include <string_view>

enum class TraceCategory { Script, FrameAction };

struct TraceLocation {
  int scene;
  int line;
};

struct TraceOptions {
  bool script = false;
  bool frame_action = false;

  bool Any() const { return script || frame_action; }
};

// Parse the explicit value of --trace; a bare option supplies "script".
TraceOptions ParseTraceOptions(std::string_view selection);

bool IsTraceEnabled(TraceCategory category);

// Format the content only after a trace sink accepts the record. Location is
// printed uniformly, independently of the category used to select output.
void WriteTrace(TraceCategory category,
                TraceLocation location,
                const std::function<std::string()>& format_content);
