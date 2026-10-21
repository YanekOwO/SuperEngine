// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006 Elliot Glaysher
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

#include "modules/module_scr.hpp"

#include "core/haikei.hpp"
#include "core/stage.hpp"
#include "machine/general_operations.hpp"
#include "machine/rlmachine.hpp"
#include "machine/rloperation.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/system.hpp"

namespace {

struct GetDCPixel : public RLOpcode<IntConstant_T,
                                    IntConstant_T,
                                    IntConstant_T,
                                    IntReference_T,
                                    IntReference_T,
                                    IntReference_T> {
  void operator()(RLMachine& machine,
                  int x,
                  int y,
                  int dc,
                  IntReferenceIterator r,
                  IntReferenceIterator g,
                  IntReferenceIterator b) {
    RGBAColour pixel = machine.haikei().GetDC(dc)->GetPixelAt(Point(x, y));
    *r = pixel.r();
    *g = pixel.g();
    *b = pixel.b();
  }
};

struct StackClear : public RLOpcode<> {
  void operator()(RLMachine& machine) {
    Stage& stage = machine.stage();
    stage.graphics_stack.clear();
  }
};

struct StackPop : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int count) {
    Stage& stage = machine.stage();
    if (count > stage.graphics_stack.size())
      count = stage.graphics_stack.size();
    stage.graphics_stack.resize(stage.graphics_stack.size() - count);
  }
};

struct StackSize : public RLStoreOpcode<> {
  int operator()(RLMachine& machine) {
    Stage& stage = machine.stage();
    return stage.graphics_stack.size();
  }
};

struct StackNop : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int numberOfNops) {
    Stage& stage = machine.stage();

    for (int i = 0; i < numberOfNops; ++i)
      stage.AddGraphicsStackCommand("");
  }
};

struct StackTrunc : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int count) {
    Stage& stage = machine.stage();
    if (count < 0)
      count = 0;
    if (static_cast<size_t>(count) < stage.graphics_stack.size())
      stage.graphics_stack.resize(count);
  }
};

}  // namespace

// -----------------------------------------------------------------------

ScrModule::ScrModule() : RLModule("Scr", 1, 30) {
  AddOpcode(0, 0, "stackClear", std::make_shared<StackClear>());
  AddOpcode(1, 0, "stackNop", std::make_shared<StackNop>());
  AddOpcode(2, 0, "StackPop", std::make_shared<StackPop>());
  AddOpcode(3, 0, "StackSize", std::make_shared<StackSize>());
  AddOpcode(4, 0, "stackTrunc", std::make_shared<StackTrunc>());

  AddOpcode(20, 0, "DrawAuto",
            CallFunctionWith(&RLMachine::SetScreenUpdateMode,
                             RLMachine::SCREENUPDATEMODE_AUTOMATIC));
  AddOpcode(21, 0, "DrawSemiAuto",
            CallFunctionWith(&RLMachine::SetScreenUpdateMode,
                             RLMachine::SCREENUPDATEMODE_SEMIAUTOMATIC));
  AddOpcode(22, 0, "DrawManual",
            CallFunctionWith(&RLMachine::SetScreenUpdateMode,
                             RLMachine::SCREENUPDATEMODE_MANUAL));

  AddOpcode(31, 0, "GetDCPixel", std::make_shared<GetDCPixel>());
}
