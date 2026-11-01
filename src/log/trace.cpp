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

#include "log/trace.hpp"

#include <boost/log/sources/channel_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>

#include <format>
#include <stdexcept>
#include <utility>

TraceOptions ParseTraceOptions(std::string_view selection) {
  if (selection == "script")
    return {.script = true};
  if (selection == "frame-action")
    return {.frame_action = true};
  if (selection == "all")
    return {.script = true, .frame_action = true};
  throw std::invalid_argument(
      "Invalid --trace selection; expected script, frame-action, or all");
}

void WriteTrace(TraceCategory category,
                TraceLocation location,
                const std::function<std::string()>& format_content) {
  if (!IsTraceEnabled(category))
    return;

  namespace logging = boost::log;
  static logging::sources::channel_logger<> script(logging::keywords::channel =
                                                       "instruction.script");
  static logging::sources::channel_logger<> frame_action(
      logging::keywords::channel = "instruction.frame-action");
  auto& logger = category == TraceCategory::Script ? script : frame_action;
  if (auto record = logger.open_record()) {
    logging::record_ostream stream(record);
    stream << std::format("[{}:{}] ", location.scene, location.line)
           << format_content();
    stream.flush();
    logger.push_record(std::move(record));
  }
}
