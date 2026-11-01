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
#include "log/core.hpp"
#include "log/domain_logger.hpp"
#include "log_capture.hpp"

#include <gtest/gtest.h>

class TraceLoggingTest : public ::testing::Test {
 protected:
  LogCapture output;
};

TEST_F(TraceLoggingTest, ScriptSelectionEnablesOnlyScript) {
  // Parse the script selection; frame-action tracing stays disabled.
  const auto options = ParseTraceOptions("script");
  EXPECT_TRUE(options.script);
  EXPECT_FALSE(options.frame_action);
}

TEST_F(TraceLoggingTest, FrameActionSelectionEnablesOnlyFrameActions) {
  // Parse the callback selection; ordinary script tracing stays disabled.
  const auto options = ParseTraceOptions("frame-action");
  EXPECT_FALSE(options.script);
  EXPECT_TRUE(options.frame_action);
}

TEST_F(TraceLoggingTest, AllSelectionEnablesBothCategories) {
  // The all selection enables both kinds of instruction trace.
  const auto options = ParseTraceOptions("all");
  EXPECT_TRUE(options.script);
  EXPECT_TRUE(options.frame_action);
}

TEST_F(TraceLoggingTest, UnknownSelectionIsRejected) {
  // An unsupported selection must fail instead of silently enabling tracing.
  EXPECT_THROW(ParseTraceOptions("warning"), std::invalid_argument);
}

TEST_F(TraceLoggingTest, DefaultConfigurationDisablesTracing) {
  // Severity-only setup accepts neither category and does not format content.
  SetupLogging(Severity::None);
  bool formatted = false;
  WriteTrace(TraceCategory::Script, {1, 2}, [&] {
    formatted = true;
    return "hidden";
  });
  EXPECT_FALSE(formatted);
  EXPECT_FALSE(IsTraceEnabled(TraceCategory::FrameAction));
  EXPECT_TRUE(output.Str().empty());
}

TEST_F(TraceLoggingTest, ScriptOutputExcludesFrameActions) {
  // Script-only setup emits one location/content record without extra labels.
  SetupLogging(Severity::Error, {.script = true});
  WriteTrace(TraceCategory::Script, {7, 12}, [] { return "script"; });
  WriteTrace(TraceCategory::FrameAction, {7, 13}, [] { return "callback"; });
  EXPECT_EQ(output.Str(), "[7:12] script\n");
}

TEST_F(TraceLoggingTest, UnselectedCategoryDoesNotFormatContent) {
  // Selecting scripts must not invoke the writer's callback for frame actions.
  SetupLogging(Severity::Error, {.script = true});
  bool formatted = false;
  WriteTrace(TraceCategory::FrameAction, {7, 12}, [&] {
    formatted = true;
    return "hidden";
  });
  EXPECT_FALSE(formatted);
}

TEST_F(TraceLoggingTest, FrameActionOutputExcludesScripts) {
  // Callback-only setup suppresses scripts and uses the same record format.
  SetupLogging(Severity::Error, {.frame_action = true});
  WriteTrace(TraceCategory::Script, {7, 12}, [] { return "script"; });
  WriteTrace(TraceCategory::FrameAction, {7, 13}, [] { return "callback"; });
  EXPECT_EQ(output.Str(), "[7:13] callback\n");
}

TEST_F(TraceLoggingTest, AllOutputIncludesBothCategories) {
  // Both channels reach the trace sink, each exactly once.
  SetupLogging(Severity::Error, ParseTraceOptions("all"));
  WriteTrace(TraceCategory::Script, {1, 2}, [] { return "script"; });
  WriteTrace(TraceCategory::FrameAction, {3, 4}, [] { return "callback"; });
  EXPECT_EQ(output.Str(), "[1:2] script\n[3:4] callback\n");
}

TEST_F(TraceLoggingTest, DiagnosticSeverityDoesNotFilterTraceOutput) {
  // Error-only diagnostics still allow selected traces while dropping warnings.
  SetupLogging(Severity::Error, {.script = true});
  DomainLogger("test")(Severity::Warn) << "hidden warning";
  WriteTrace(TraceCategory::Script, {1, 2}, [] { return "visible"; });
  EXPECT_EQ(output.Str(), "[1:2] visible\n");
}

TEST_F(TraceLoggingTest, DiagnosticDisableDoesNotDisableTracing) {
  // Disabling the diagnostic logger must leave independently enabled traces on.
  SetupLogging(Severity::None, {.script = true});
  logging_enabled = false;
  DomainLogger("test")(Severity::Error) << "hidden error";
  WriteTrace(TraceCategory::Script, {1, 2}, [] { return "visible"; });
  EXPECT_EQ(output.Str(), "[1:2] visible\n");
}

TEST_F(TraceLoggingTest, DiagnosticsDoNotLeakIntoTraceSink) {
  // A diagnostic record keeps its original severity/scope format and appears
  // once.
  SetupLogging(Severity::Warn, ParseTraceOptions("all"));
  DomainLogger("test")(Severity::Warn) << "warning";
  EXPECT_EQ(output.Str(), "[WARNING][test] warning\n");
}

TEST_F(TraceLoggingTest, ReconfigurationReplacesSeverityFilter) {
  // Replacing a permissive setup drops old sinks, so a warning is now hidden.
  SetupLogging(Severity::None);
  SetupLogging(Severity::Error);
  DomainLogger("test")(Severity::Warn) << "hidden";
  EXPECT_TRUE(output.Str().empty());
}

TEST_F(TraceLoggingTest, ReconfigurationDoesNotDuplicateTraceOutput) {
  // Configuring the same trace sink twice still produces a single record.
  SetupLogging(Severity::Error, {.script = true});
  SetupLogging(Severity::Error, {.script = true});
  WriteTrace(TraceCategory::Script, {1, 2}, [] { return "once"; });
  EXPECT_EQ(output.Str(), "[1:2] once\n");
}

TEST_F(TraceLoggingTest, ReconfigurationReplacesCategorySelection) {
  // Switching from scripts to callbacks disables the previously selected
  // stream.
  SetupLogging(Severity::None, {.script = true});
  SetupLogging(Severity::None, {.frame_action = true});
  WriteTrace(TraceCategory::Script, {1, 2}, [] { return "hidden"; });
  WriteTrace(TraceCategory::FrameAction, {3, 4}, [] { return "visible"; });
  EXPECT_EQ(output.Str(), "[3:4] visible\n");
}
