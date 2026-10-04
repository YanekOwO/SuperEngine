// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2013 Elliot Glaysher
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
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
// -----------------------------------------------------------------------

#include "modules/object_mutator_operations.hpp"

#include "core/object.hpp"
#include "core/object_internal/object_mutator.hpp"
#include "core/object_internal/object_parameter.hpp"
#include "long_operations/wait_long_operation.hpp"
#include "machine/rlmachine.hpp"
#include "modules/module_obj.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"
#include "utilities/clock.hpp"

FrameCounter MakeFrameCounter(int duration,
                              int delay,
                              int start_val,
                              int end_val,
                              int type,
                              std::shared_ptr<Clock> clock) {
  InterpolationMode mode = InterpolationMode::Linear;
  switch (type) {
    case 1:
      mode = InterpolationMode::Decelerate;
      break;
    case 2:
      mode = InterpolationMode::Accelerate;
      break;
    case 0:
    default:
      break;
  }

  FrameCounter fc(std::move(clock),
                  Interpolation(Range(start_val, end_val),
                                InterpolationType::OneShot, mode),
                  duration);
  fc.BeginTimer(std::chrono::milliseconds(delay));
  return fc;
}

// ------------------------------------------------------------------------------
namespace {
using namespace std::placeholders;

bool MutatorIsDone(RLMachine& machine,
                   RLOperation* op,
                   int obj,
                   int repno,
                   const std::string& name) {
  return GetGraphicsObject(machine, op, obj)
             .IsMutatorRunningMatching(repno, name) == false;
}

bool ObjectMutatorIsWorking(RLMachine& machine,
                            RLOperation* op,
                            int obj,
                            int repno,
                            const std::string& name) {
  return GetGraphicsObject(machine, op, obj)
             .IsMutatorRunningMatching(repno, name) == false;
}

}  // namespace

// -----------------------------------------------------------------------

Op_ObjectMutatorInt::Op_ObjectMutatorInt(Getter getter,
                                         Setter setter,
                                         const std::string& name)
    : getter_(getter), setter_(setter), name_(name) {}

Op_ObjectMutatorInt::~Op_ObjectMutatorInt() {}

void Op_ObjectMutatorInt::operator()(RLMachine& machine,
                                     int object,
                                     int endval,
                                     int duration_time,
                                     int delay,
                                     int type) {
  std::shared_ptr<Clock> clock = machine.GetSystem().event().GetClock();
  GraphicsObject& obj = GetGraphicsObject(machine, this, object);

  // If there's a currently running mutator that matches the incoming
  // operation, ignore the new operation. Kud Wafter's ED relies on this
  // behavior.
  if (obj.IsMutatorRunningMatching(-1, name_))
    return;

  int startval = std::invoke(getter_, obj.Param());

  ObjectMutator mutator(
      ObjectParameterMutator(
          setter_, MakeFrameCounter(duration_time, delay, startval, endval,
                                    type, std::move(clock))),
      -1, name_);
  obj.AddObjectMutator(std::move(mutator));
}

// -----------------------------------------------------------------------

Op_ObjectMutatorRepnoInt::Op_ObjectMutatorRepnoInt(Getter getter,
                                                   Setter setter,
                                                   const std::string& name)
    : getter_(getter), setter_(setter), name_(name) {}

Op_ObjectMutatorRepnoInt::~Op_ObjectMutatorRepnoInt() {}

void Op_ObjectMutatorRepnoInt::operator()(RLMachine& machine,
                                          int object,
                                          int repno,
                                          int endval,
                                          int duration_time,
                                          int delay,
                                          int type) {
  std::shared_ptr<Clock> clock = machine.GetSystem().event().GetClock();
  GraphicsObject& obj = GetGraphicsObject(machine, this, object);

  if (obj.IsMutatorRunningMatching(repno, name_))
    return;

  int startval = std::invoke(getter_, obj.Param(), repno);
  ObjectMutator mutator(
      ObjectParameterMutator(std::bind(setter_, _1, repno, _2),
                             MakeFrameCounter(duration_time, delay, startval,
                                              endval, type, std::move(clock))),
      repno, name_);

  obj.AddObjectMutator(std::move(mutator));
}

// -----------------------------------------------------------------------

Op_ObjectMutatorIntInt::Op_ObjectMutatorIntInt(Getter getter_one,
                                               Setter setter_one,
                                               Getter getter_two,
                                               Setter setter_two,
                                               const std::string& name)
    : getter_one_(getter_one),
      setter_one_(setter_one),
      getter_two_(getter_two),
      setter_two_(setter_two),
      name_(name) {}

Op_ObjectMutatorIntInt::~Op_ObjectMutatorIntInt() {}

void Op_ObjectMutatorIntInt::operator()(RLMachine& machine,
                                        int object,
                                        int endval_one,
                                        int endval_two,
                                        int duration_time,
                                        int delay,
                                        int type) {
  std::shared_ptr<Clock> clock = machine.GetSystem().event().GetClock();
  GraphicsObject& obj = GetGraphicsObject(machine, this, object);
  ObjectParameter& pm = obj.Param();

  if (obj.IsMutatorRunningMatching(-1, name_))
    return;

  int startval_one = std::invoke(getter_one_, pm);
  int startval_two = std::invoke(getter_two_, pm);

  obj.AddObjectMutator(ObjectMutator(
      ObjectParameterMutator(
          setter_one_, MakeFrameCounter(duration_time, delay, startval_one,
                                        endval_one, type, clock)),
      -1, name_));
  obj.AddObjectMutator(ObjectMutator(
      ObjectParameterMutator(
          setter_two_, MakeFrameCounter(duration_time, delay, startval_two,
                                        endval_two, type, std::move(clock))),
      -1, name_));
}

// -----------------------------------------------------------------------

Op_EndObjectMutation_Normal::Op_EndObjectMutation_Normal(
    const std::string& name)
    : name_(name) {}

Op_EndObjectMutation_Normal::~Op_EndObjectMutation_Normal() {}

void Op_EndObjectMutation_Normal::operator()(RLMachine& machine,
                                             int object,
                                             int speedup) {
  GetGraphicsObject(machine, this, object)
      .EndObjectMutatorMatching(-1, name_, speedup);
}

// -----------------------------------------------------------------------

Op_EndObjectMutation_RepNo::Op_EndObjectMutation_RepNo(const std::string& name)
    : name_(name) {}

Op_EndObjectMutation_RepNo::~Op_EndObjectMutation_RepNo() {}

void Op_EndObjectMutation_RepNo::operator()(RLMachine& machine,
                                            int object,
                                            int repno,
                                            int speedup) {
  GetGraphicsObject(machine, this, object)
      .EndObjectMutatorMatching(repno, name_, speedup);
}

// -----------------------------------------------------------------------

Op_MutatorCheck::Op_MutatorCheck(const std::string& name) : name_(name) {}

Op_MutatorCheck::~Op_MutatorCheck() {}

int Op_MutatorCheck::operator()(RLMachine& machine, int object) {
  GraphicsObject& obj = GetGraphicsObject(machine, this, object);
  return obj.IsMutatorRunningMatching(object, name_) ? 1 : 0;
}

// -----------------------------------------------------------------------

Op_MutatorWaitNormal::Op_MutatorWaitNormal(const std::string& name)
    : name_(name) {}

Op_MutatorWaitNormal::~Op_MutatorWaitNormal() {}

void Op_MutatorWaitNormal::operator()(RLMachine& machine, int obj) {
  auto wait_op = std::make_shared<WaitLongOperation>(machine);
  wait_op->BreakOnEvent(
      std::bind(MutatorIsDone, std::ref(machine), this, obj, -1, name_));
  machine.PushLongOperation(wait_op);
}

// -----------------------------------------------------------------------

Op_MutatorWaitRepNo::Op_MutatorWaitRepNo(const std::string& name)
    : name_(name) {}

Op_MutatorWaitRepNo::~Op_MutatorWaitRepNo() {}

void Op_MutatorWaitRepNo::operator()(RLMachine& machine, int obj, int repno) {
  auto wait_op = std::make_shared<WaitLongOperation>(machine);
  wait_op->BreakOnEvent(
      std::bind(MutatorIsDone, std::ref(machine), this, obj, repno, name_));
  machine.PushLongOperation(wait_op);
}

// -----------------------------------------------------------------------

Op_MutatorWaitCNormal::Op_MutatorWaitCNormal(const std::string& name)
    : name_(name) {}

Op_MutatorWaitCNormal::~Op_MutatorWaitCNormal() {}

void Op_MutatorWaitCNormal::operator()(RLMachine& machine, int obj) {
  auto wait_op = std::make_shared<WaitLongOperation>(machine);
  wait_op->BreakOnClicks();
  wait_op->BreakOnEvent(std::bind(ObjectMutatorIsWorking, std::ref(machine),
                                  this, obj, -1, name_));
  machine.PushLongOperation(wait_op);
}

// -----------------------------------------------------------------------

Op_MutatorWaitCRepNo::Op_MutatorWaitCRepNo(const std::string& name)
    : name_(name) {}

Op_MutatorWaitCRepNo::~Op_MutatorWaitCRepNo() {}

void Op_MutatorWaitCRepNo::operator()(RLMachine& machine, int obj, int repno) {
  auto wait_op = std::make_shared<WaitLongOperation>(machine);
  wait_op->BreakOnClicks();
  wait_op->BreakOnEvent(std::bind(ObjectMutatorIsWorking, std::ref(machine),
                                  this, obj, repno, name_));
  machine.PushLongOperation(wait_op);
}
