// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
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

#include "modules/module_sys_frame.hpp"

#include <boost/numeric/conversion/cast.hpp>

#include "core/event_listener.hpp"
#include "core/frame_counter.hpp"
#include "log/domain_logger.hpp"
#include "long_operations/wait_long_operation.hpp"
#include "machine/long_operation.hpp"
#include "machine/rlmachine.hpp"
#include "machine/rlmodule.hpp"
#include "machine/rloperation.hpp"
#include "machine/rloperation/default_value_t.hpp"
#include "machine/rloperation/rlop_store.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"
#include "systems/event_system.hpp"

#include <chrono>

namespace chr = std::chrono;
using boost::numeric_cast;

namespace {

struct ResetTimer : public RLOpcode<DefaultIntValue_T<0>> {
  const int layer_;
  explicit ResetTimer(const int in) : layer_(in) {}

  void operator()(RLMachine& machine, int counter) {
    auto& timer = machine.GetTimer(layer_, counter);
    timer.Apply(Stopwatch::Action::Reset);
    timer.Apply(Stopwatch::Action::Run);
  }
};

bool TimerIsDone(RLMachine& machine,
                 int layer,
                 int counter,
                 unsigned int target_time) {
  auto duration = chr::duration_cast<chr::milliseconds>(
      machine.GetTimer(layer, counter).GetReading());
  return duration.count() > target_time;
}

struct Sys_time : public RLOpcode<IntConstant_T, DefaultIntValue_T<0>> {
  const int layer_;
  const bool in_time_c_;
  Sys_time(const int in, const bool timeC) : layer_(in), in_time_c_(timeC) {}

  void operator()(RLMachine& machine, int time, int counter) {
    const auto duration = chr::duration_cast<chr::milliseconds>(
        machine.GetTimer(layer_, counter).GetReading());
    if (duration.count() < numeric_cast<unsigned int>(time)) {
      auto wait_op = std::make_shared<WaitLongOperation>(machine);
      if (in_time_c_)
        wait_op->BreakOnClicks();
      wait_op->BreakOnEvent(
          std::bind(TimerIsDone, std::ref(machine), layer_, counter, time));
      machine.PushLongOperation(wait_op);
    }
  }
};

struct Timer : public RLStoreOpcode<DefaultIntValue_T<0>> {
  const int layer_;
  explicit Timer(const int in) : layer_(in) {}

  int operator()(RLMachine& machine, int counter) {
    const auto duration = chr::duration_cast<chr::milliseconds>(
        machine.GetTimer(layer_, counter).GetReading());

    return numeric_cast<int>(duration.count());
  }
};

struct CmpTimer : public RLStoreOpcode<IntConstant_T, DefaultIntValue_T<0>> {
  const int layer_;
  explicit CmpTimer(const int in) : layer_(in) {}

  int operator()(RLMachine& machine, int val, int counter) {
    const auto duration = chr::duration_cast<chr::milliseconds>(
        machine.GetTimer(layer_, counter).GetReading());
    return duration.count() > val;
  }
};

struct SetTimer : public RLOpcode<IntConstant_T, DefaultIntValue_T<0>> {
  const int layer_;
  explicit SetTimer(const int in) : layer_(in) {}

  void operator()(RLMachine& machine, int val, int counter) {
    if (val) {
      static DomainLogger logger("SetTimer");
      logger(Severity::Warn) << "Implementation might be wrong. val = " << val;
    }

    auto& timer = machine.GetTimer(layer_, counter);
    timer.Apply(Stopwatch::Action::Reset);
    timer.Apply(Stopwatch::Action::Run);
  }
};

}  // namespace

// -----------------------------------------------------------------------

void AddSysTimerOpcodes(RLModule& m) {
  m.AddOpcode(110, 0, "ResetTimer", new ResetTimer(0));
  m.AddOpcode(110, 1, "ResetTimer", new ResetTimer(0));
  m.AddOpcode(111, 0, "time", new Sys_time(0, false));
  m.AddOpcode(111, 1, "time", new Sys_time(0, false));
  m.AddOpcode(112, 0, "timeC", new Sys_time(0, true));
  m.AddOpcode(112, 1, "timeC", new Sys_time(0, true));
  m.AddOpcode(114, 0, "Timer", new Timer(0));
  m.AddOpcode(114, 1, "Timer", new Timer(0));
  m.AddOpcode(115, 0, "CmpTimer", new CmpTimer(0));
  m.AddOpcode(115, 1, "CmpTimer", new CmpTimer(0));
  m.AddOpcode(116, 0, "SetTimer", new SetTimer(0));
  m.AddOpcode(116, 1, "SetTimer", new SetTimer(0));

  m.AddOpcode(120, 0, "ResetExTimer", new ResetTimer(1));
  m.AddOpcode(120, 1, "ResetExTimer", new ResetTimer(1));
  m.AddOpcode(121, 0, "timeEx", new Sys_time(1, false));
  m.AddOpcode(121, 1, "timeEx", new Sys_time(1, false));
  m.AddOpcode(122, 0, "timeExC", new Sys_time(1, true));
  m.AddOpcode(122, 1, "timeExC", new Sys_time(1, true));
  m.AddOpcode(124, 0, "ExTimer", new Timer(1));
  m.AddOpcode(124, 1, "ExTimer", new Timer(1));
  m.AddOpcode(125, 0, "CmpExTimer", new CmpTimer(1));
  m.AddOpcode(125, 1, "CmpExTimer", new CmpTimer(1));
  m.AddOpcode(126, 0, "SetExTimer", new SetTimer(1));
  m.AddOpcode(126, 1, "SetExTimer", new SetTimer(1));
}
