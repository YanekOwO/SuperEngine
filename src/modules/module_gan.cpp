// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
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

#include "modules/module_gan.hpp"

#include "core/object.hpp"
#include "core/object_internal/drawer/gan.hpp"
#include "core/stage.hpp"
#include "long_operations/wait_long_operation.hpp"
#include "machine/long_operation.hpp"
#include "machine/properties.hpp"
#include "machine/rlmachine.hpp"
#include "machine/rlmodule.hpp"
#include "machine/rloperation.hpp"
#include "machine/rloperation/rlop_store.hpp"
#include "modules/module_obj.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"

// -----------------------------------------------------------------------

namespace {

struct objWaitAll : public RLOpcode<> {
  static bool WaitUntilDone(RLMachine& machine) {
    // Clannad puts us in DrawManual() right before calling us so we force
    // refreshes.
    machine.GetSystem().graphics().ForceRefresh();
    auto& fgobj = machine.stage().objects[kLayerFg];
    bool animations_playing = false;
    for (const auto& object : fgobj) {
      if (object && object->HasDrawer()) {
        const GraphicsObjectData& data = object->GetDrawer();
        if (data.IsAnimation() && data.GetAnimator()->IsPlaying()) {
          animations_playing = true;
          break;
        }
      }
    }

    return !animations_playing;
  }

  void operator()(RLMachine& machine) {
    auto wait_op = std::make_shared<WaitLongOperation>(machine);
    wait_op->BreakOnEvent(std::bind(WaitUntilDone, std::ref(machine)));
    machine.PushLongOperation(wait_op);
  }
};

// Save the screen update mode and change it to automatic when entering a
// blocking animation, restoring when we leave.
//
// When I actually started paying attention to DrawManual()/DrawAuto(), I
// stopped forcing a refresh of the screen on each and instead obeyed the
// DCScreenUpdateMode, I broke the OP of Kanon.
//
// I don't know if this is the correct solution. If this breaks things, the
// solution is that things like AnmGraphicsObjectData force screen refreshes,
// ignoring whatever the current screen update mode is.
struct WaitForGanToFinish : public LongOperation {
  WaitForGanToFinish(GraphicsSystem& system, int fgbg, int parent, int inBuf)
      : system_(system),
        mode_(system_.screen_update_mode()),
        fgbg_(fgbg),
        parent_(parent),
        buf_(inBuf) {
    system_.SetScreenUpdateMode(GraphicsSystem::SCREENUPDATEMODE_AUTOMATIC);
  }

  bool operator()(RLMachine& machine) override {
    GraphicsObject& obj = GetObject(machine);
    bool done = true;

    if (obj.HasDrawer()) {
      const GraphicsObjectData& data = obj.GetDrawer();
      if (data.IsAnimation())
        done = !data.GetAnimator()->IsPlaying();
    }

    if (done) {
      // Restore whatever mode we were in before.
      system_.SetScreenUpdateMode(mode_);
    }

    return done;
  }

  GraphicsObject& GetObject(RLMachine& machine) {
    GraphicsSystem& graphics = machine.GetSystem().graphics();
    Stage& stage = machine.stage();

    if (parent_ != -1) {
      GraphicsObject& parent = stage.GetObject(fgbg_, parent_);
      EnsureIsParentObject(parent, graphics.GetObjectLayerSize());
      return parent.TouchChild(buf_);
    } else {
      return stage.GetObject(fgbg_, buf_);
    }
  }

  GraphicsSystem& system_;
  GraphicsSystem::DCScreenUpdateMode mode_;

  int fgbg_;
  int parent_;
  int buf_;
};

struct ganPlay : public RLOpcode<IntConstant_T, IntConstant_T> {
  bool block_;
  AfterAnimation after_effect_;

  ganPlay(bool block, AfterAnimation after)
      : block_(block), after_effect_(after) {}

  void operator()(RLMachine& machine, int buf, int animationSet) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, buf);

    if (obj.HasDrawer()) {
      GraphicsObjectData& data = obj.GetDrawer();
      if (data.IsAnimation()) {
        data.PlaySet(animationSet);
        data.GetAnimator()->SetAfterAction(after_effect_);

        if (block_) {
          int fgbg;
          if (!GetProperty(P_FGBG, fgbg))
            fgbg = kLayerFg;

          int parent_object;
          if (!GetProperty(P_PARENTOBJ, parent_object))
            parent_object = -1;

          machine.PushLongOperation(std::make_shared<WaitForGanToFinish>(
              machine.GetSystem().graphics(), fgbg, parent_object, buf));
        }
      }
    }
  }
};

struct ganWait : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int buf) {
    int fgbg;
    if (!GetProperty(P_FGBG, fgbg))
      fgbg = kLayerFg;

    int parent_object;
    if (!GetProperty(P_PARENTOBJ, parent_object))
      parent_object = -1;

    machine.PushLongOperation(std::make_shared<WaitForGanToFinish>(
        machine.GetSystem().graphics(), fgbg, parent_object, buf));
  }
};

// Returns true when an animation is completed.
//
// RATIONALE:
//
// This is a guess. I'm not entirely sure what this function does, but I'm
// trying to pick the meaning out of context. Here's a snippet of CLANNAD's
// SEEN9081, where it's used:
//
// @code
// #line 412
// ganPlay2 (3, 15)
//
// [...]
//
//   @62
// #line 416
// op<1:073:00003, 0> (3)
// intA[0] = store
// #line 417
// goto_unless (!intA[0]) @63
// #line 418
// intA[200] = 19
// #line 420
//
//   @63
// goto @64
// @endcode
//
// So it looks like it's trying to set some sort of sentinel value since
// intA[200] is checked later. It's checking SOMETHING about the gan object,
// and I'm assuming it's whether it's done animating.
//
// After implementing this, we no longer get stuck in an infinite loop during
// Ushio's birth so I'm assuming this is correct.
struct isGanDonePlaying : public RLStoreOpcode<IntConstant_T> {
  int operator()(RLMachine& machine, int gan_num) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, gan_num);

    if (obj.HasDrawer()) {
      GraphicsObjectData& data = obj.GetDrawer();
      if (data.IsAnimation()) {
        if (data.GetAnimator()->IsFinished()) {
          return 0;
        } else {
          return 1;
        }
      }
    }

    return 0;
  }
};

struct objStop_0 : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int obj_num) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, obj_num);
    if (obj.HasDrawer())
      obj.GetDrawer().GetAnimator()->SetIsPlaying(false);
  }
};

}  // namespace

// -----------------------------------------------------------------------

void addGanOperationsTo(RLModule& m) {
  m.AddOpcode(0, 0, "objStop2", new objStop_0);
  m.AddOpcode(3, 0, "ganIsDonePlaying", new isGanDonePlaying);
  m.AddOpcode(4, 0, "ganWait", new ganWait);

  m.AddOpcode(1000, 0, "objStop", new objStop_0);
  m.AddUnsupportedOpcode(1000, 1, "objStop");

  m.AddOpcode(104, 0, "objWaitAll", new objWaitAll);

  m.AddOpcode(1001, 0, "ganLoop", new ganPlay(false, AFTER_LOOP));
  m.AddOpcode(1003, 0, "ganPlay", new ganPlay(false, AFTER_NONE));
  m.AddOpcode(1005, 0, "ganPlayOnce", new ganPlay(false, AFTER_CLEAR));
  m.AddOpcode(1006, 0, "ganPlayEx", new ganPlay(true, AFTER_NONE));
  m.AddOpcode(1007, 0, "ganPlayOnceEx", new ganPlay(true, AFTER_CLEAR));

  m.AddOpcode(2001, 0, "objLoop", new ganPlay(false, AFTER_LOOP));
  m.AddOpcode(2003, 0, "objPlay", new ganPlay(false, AFTER_NONE));

  m.AddOpcode(3001, 0, "ganLoop2", new ganPlay(false, AFTER_LOOP));
  m.AddOpcode(3003, 0, "ganPlay2", new ganPlay(false, AFTER_NONE));
  m.AddOpcode(3005, 0, "ganPlayOnce2", new ganPlay(false, AFTER_CLEAR));
  m.AddOpcode(3006, 0, "ganPlayEx2", new ganPlay(true, AFTER_NONE));
  m.AddOpcode(3007, 0, "ganPlayOnceEx2", new ganPlay(true, AFTER_CLEAR));

  m.AddOpcode(3103, 0, "ganPlay3", new ganPlay(false, AFTER_NONE));
}

// -----------------------------------------------------------------------

GanFgModule::GanFgModule() : RLModule("GanFg", 1, 73) {
  addGanOperationsTo(*this);
  SetProperty(P_FGBG, kLayerFg);
}

// -----------------------------------------------------------------------

GanBgModule::GanBgModule() : RLModule("GanBg", 1, 74) {
  addGanOperationsTo(*this);
  SetProperty(P_FGBG, kLayerBg);
}

// -----------------------------------------------------------------------

ChildGanFgModule::ChildGanFgModule()
    : MappedRLModule(ChildObjMappingFun, "ChildGanFg", 2, 73) {
  addGanOperationsTo(*this);
  SetProperty(P_FGBG, kLayerFg);
}

// -----------------------------------------------------------------------

ChildGanBgModule::ChildGanBgModule()
    : MappedRLModule(ChildObjMappingFun, "ChildGanBg", 2, 74) {
  addGanOperationsTo(*this);
  SetProperty(P_FGBG, kLayerBg);
}
