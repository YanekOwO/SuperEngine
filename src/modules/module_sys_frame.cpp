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

#include "core/frame_counter.hpp"
#include "machine/general_operations.hpp"
#include "machine/rlmachine.hpp"
#include "machine/rlmodule.hpp"
#include "machine/rloperation.hpp"
#include "machine/rloperation/argc_t.hpp"
#include "machine/rloperation/complex_t.hpp"
#include "modules/module_sys.hpp"
#include "utilities/clock.hpp"

#include <utility>

namespace {

struct InitFrame : public RLOpcode<IntConstant_T,
                                   IntConstant_T,
                                   IntConstant_T,
                                   IntConstant_T> {
  const int layer_;
  const InterpolationType type_;
  const InterpolationMode mode_;

  explicit InitFrame(int layer,
                     InterpolationType type = InterpolationType::OneShot,
                     InterpolationMode mode = InterpolationMode::Linear)
      : layer_(layer), type_(type), mode_(mode) {}

  void operator()(RLMachine& machine,
                  int counter,
                  int frameMin,
                  int frameMax,
                  int time) {
    FrameCounter fc(
        std::make_shared<Clock>(),
        Interpolation(Range(frameMin, frameMax), type_, mode_), time);
    machine.GetEnvironment().SetFrameCounter(layer_, counter, std::move(fc));
  }
};

struct ReadFrame : public RLStoreOpcode<IntConstant_T> {
  const int layer_;
  explicit ReadFrame(int layer) : layer_(layer) {}

  int operator()(RLMachine& machine, int counter) {
    auto fc = machine.GetEnvironment().GetFrameCounter(layer_, counter);
    if (!fc)
      return 0;

    return static_cast<int>(fc->ReadFrame());
  }
};

struct FrameActive : public RLStoreOpcode<IntConstant_T> {
  const int layer_;
  explicit FrameActive(int layer) : layer_(layer) {}

  int operator()(RLMachine& machine, int counter) {
    auto fc = machine.GetEnvironment().GetFrameCounter(layer_, counter);
    if (!fc)
      return 0;

    return fc->IsActive() ? 1 : 0;
  }
};

struct AnyFrameActive : public RLStoreOpcode<IntConstant_T> {
  const int layer_;
  explicit AnyFrameActive(int layer) : layer_(layer) {}

  int operator()(RLMachine& machine, int counter /*?*/) {
    for (int i = 0; i < 255; ++i) {
      auto fc = machine.GetEnvironment().GetFrameCounter(layer_, i);
      if (fc && fc->IsActive())
        return 1;
    }
    return 0;
  }
};

struct ClearFrame_0 : public RLOpcode<IntConstant_T> {
  const int layer_;
  explicit ClearFrame_0(int layer) : layer_(layer) {}

  void operator()(RLMachine& machine, int counter) {
    machine.GetEnvironment().ClearFrameCounter(layer_, counter);
  }
};

struct ClearFrame_1 : public RLOpcode<IntConstant_T, IntConstant_T> {
  const int layer_;
  explicit ClearFrame_1(int layer) : layer_(layer) {}

  void operator()(RLMachine& machine, int counter, int new_value) {
    auto fc = machine.GetEnvironment().GetFrameCounter(layer_, counter);
    fc->SetFrame(new_value);
  }
};

struct ClearAllFrames_0 : public RLOpcode<IntConstant_T> {
  const int layer_;
  explicit ClearAllFrames_0(int layer) : layer_(layer) {}

  void operator()(RLMachine& machine, int new_value) {
    for (int i = 0; i < 255; ++i) {
      if (auto fc = machine.GetEnvironment().GetFrameCounter(layer_, i)) {
        fc->SetFrame(new_value);
      }
    }
  }
};

struct ClearAllFrames_1 : public RLOpcode<> {
  const int layer_;
  explicit ClearAllFrames_1(int layer) : layer_(layer) {}

  void operator()(RLMachine& machine) {
    for (int i = 0; i < 255; ++i)
      machine.GetEnvironment().ClearFrameCounter(layer_, i);
  }
};

typedef Complex_T<IntConstant_T, IntReference_T> FrameDataInReadFrames;

struct ReadFrames : public RLStoreOpcode<Argc_T<FrameDataInReadFrames>> {
  const int layer_;
  explicit ReadFrames(int layer) : layer_(layer) {}

  int operator()(RLMachine& machine,
                 std::vector<FrameDataInReadFrames::type> frames) {
    bool storeValue = false;

    for (auto& frame : frames) {
      int counter = get<0>(frame);

      if (auto fc = machine.GetEnvironment().GetFrameCounter(layer_, counter)) {
        auto val = static_cast<int>(fc->ReadFrame());
        *(get<1>(frame)) = val;

        if (fc->IsActive())
          storeValue = true;
      } else {
        *(get<1>(frame)) = 0;
      }
    }

    return storeValue;
  }
};

}  // namespace

// -----------------------------------------------------------------------

void AddSysFrameOpcodes(RLModule& m) {
  // Normal frame counter operations
  m.AddOpcode(500, 0, "InitFrame", new InitFrame(0));
  m.AddOpcode(501, 0, "InitFrameLoop",
              new InitFrame(0, InterpolationType::Loop));
  m.AddOpcode(502, 0, "InitFrameTurn",
              new InitFrame(0, InterpolationType::Turn));
  m.AddOpcode(503, 0, "InitFrameAccel",
              new InitFrame(0, InterpolationType::OneShot,
                            InterpolationMode::Accelerate));
  m.AddOpcode(504, 0, "InitFrameDecel",
              new InitFrame(0, InterpolationType::OneShot,
                            InterpolationMode::Decelerate));
  m.AddOpcode(510, 0, "ReadFrame", new ReadFrame(0));
  m.AddOpcode(511, 0, "FrameActive", new FrameActive(0));
  m.AddOpcode(512, 0, "AnyFrameActive", new AnyFrameActive(0));
  m.AddOpcode(513, 0, "ClearFrame", new ClearFrame_0(0));
  m.AddOpcode(513, 1, "ClearFrame", new ClearFrame_1(0));
  m.AddOpcode(514, 0, "ClearAllFrames", new ClearAllFrames_0(0));
  m.AddOpcode(514, 1, "ClearAllFrames", new ClearAllFrames_1(0));

  // Extended frame counter operations
  m.AddOpcode(520, 0, "InitExFrame", new InitFrame(1));
  m.AddOpcode(521, 0, "InitExFrameLoop",
              new InitFrame(1, InterpolationType::Loop));
  m.AddOpcode(522, 0, "InitExFrameTurn",
              new InitFrame(1, InterpolationType::Turn));
  m.AddOpcode(523, 0, "InitExFrameAccel",
              new InitFrame(1, InterpolationType::OneShot,
                            InterpolationMode::Accelerate));
  m.AddOpcode(524, 0, "InitExFrameDecel",
              new InitFrame(1, InterpolationType::OneShot,
                            InterpolationMode::Decelerate));
  m.AddOpcode(530, 0, "ReadExFrame", new ReadFrame(1));
  m.AddOpcode(531, 0, "ExFrameActive", new FrameActive(1));
  m.AddOpcode(532, 0, "AnyExFrameActive", new AnyFrameActive(1));
  m.AddOpcode(533, 0, "ClearExFrame", new ClearFrame_0(1));
  m.AddOpcode(533, 1, "ClearExFrame", new ClearFrame_1(1));
  m.AddOpcode(534, 0, "ClearAllExFrames", new ClearAllFrames_0(1));
  m.AddOpcode(534, 1, "ClearAllExFrames", new ClearAllFrames_1(1));

  // Multiple Dispatch operations on normal frame counters
  m.AddOpcode(600, 0, "InitFrames", new MultiDispatch(new InitFrame(0)));
  m.AddOpcode(601, 0, "InitFramesLoop",
              new MultiDispatch(new InitFrame(0, InterpolationType::Loop)));
  m.AddOpcode(602, 0, "InitFramesTurn",
              new MultiDispatch(new InitFrame(0, InterpolationType::Turn)));
  m.AddOpcode(603, 0, "InitFramesAccel",
              new MultiDispatch(new InitFrame(0, InterpolationType::OneShot,
                                              InterpolationMode::Accelerate)));
  m.AddOpcode(604, 0, "InitFramesDecel",
              new MultiDispatch(new InitFrame(0, InterpolationType::OneShot,
                                              InterpolationMode::Decelerate)));
  m.AddOpcode(610, 0, "ReadFrames", new ReadFrames(0));

  // Multiple Dispatch operations on normal frame counters
  m.AddOpcode(620, 0, "InitExFrames", new MultiDispatch(new InitFrame(1)));
  m.AddOpcode(621, 0, "InitExFramesLoop",
              new MultiDispatch(new InitFrame(1, InterpolationType::Loop)));
  m.AddOpcode(622, 0, "InitExFramesTurn",
              new MultiDispatch(new InitFrame(1, InterpolationType::Turn)));
  m.AddOpcode(623, 0, "InitExFramesAccel",
              new MultiDispatch(new InitFrame(1, InterpolationType::OneShot,
                                              InterpolationMode::Accelerate)));
  m.AddOpcode(624, 0, "InitExFramesDecel",
              new MultiDispatch(new InitFrame(1, InterpolationType::OneShot,
                                              InterpolationMode::Decelerate)));
  m.AddOpcode(630, 0, "ReadExFrames", new ReadFrames(1));
}
