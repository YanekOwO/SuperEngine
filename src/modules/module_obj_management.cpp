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

#include "modules/module_obj_management.hpp"

#include "core/object.hpp"
#include "core/stage.hpp"
#include "machine/general_operations.hpp"
#include "machine/properties.hpp"
#include "machine/rlmachine.hpp"
#include "machine/rlmodule.hpp"
#include "machine/rloperation.hpp"
#include "modules/module_obj.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"

// -----------------------------------------------------------------------

namespace {

template <typename Function>
void ForEachTargetObject(RLMachine& machine, RLOperation* op, Function fn) {
  Stage& stage = machine.stage();

  int fgbg;
  if (!op->GetProperty(P_FGBG, fgbg))
    fgbg = kLayerFg;

  int parentobj;
  if (op->GetProperty(P_PARENTOBJ, parentobj)) {
    GraphicsObject& parent = stage.GetObject(fgbg, parentobj);
    EnsureIsParentObject(parent,
                         machine.GetSystem().graphics().GetObjectLayerSize());

    for (auto& child : parent.GetChildren()) {
      if (child)
        fn(*child);
    }
    return;
  }

  for (auto& object : stage.ObjectsForLayer(fgbg))
    if (object)
      fn(*object);
}

struct objCopyFgToBg_0 : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int buf) {
    Stage& stage = machine.stage();
    GraphicsObject& go = stage.GetObject(kLayerFg, buf);
    stage.SetObject(kLayerBg, buf, go.Clone());
  }
};

struct objCopyFgToBg_1 : public RLOpcode<IntConstant_T, IntConstant_T> {
  void operator()(RLMachine& machine, int start, int end) {
    Stage& stage = machine.stage();

    for (int i = start; i <= end; ++i) {
      GraphicsObject& go = stage.GetObject(kLayerFg, i);
      stage.SetObject(kLayerBg, i, go.Clone());
    }
  }
};

struct objCopy : public RLOpcode<IntConstant_T, IntConstant_T> {
  int from_fgbg_, to_fgbg_;
  objCopy(int from, int to) : from_fgbg_(from), to_fgbg_(to) {}

  void operator()(RLMachine& machine, int sbuf, int dbuf) {
    Stage& stage = machine.stage();
    GraphicsObject& go = stage.GetObject(from_fgbg_, sbuf);
    stage.SetObject(to_fgbg_, dbuf, go.Clone());
  }
};

struct SetWipeCopyTo_0 : public RLOpcode<IntConstant_T> {
  int val_;
  explicit SetWipeCopyTo_0(int value) : val_(value) {}

  void operator()(RLMachine& machine, int buf) {
    GetGraphicsObject(machine, this, buf).Param().SetWipeCopy(val_);
  }
};

struct SetWipeCopyTo_1 : public RLOpcode<IntConstant_T, IntConstant_T> {
  int val_;
  explicit SetWipeCopyTo_1(int value) : val_(value) {}

  void operator()(RLMachine& machine, int min, int numObjsToSet) {
    int maxObj = min + numObjsToSet;
    for (int i = min; i < maxObj; ++i) {
      GetGraphicsObject(machine, this, i).Param().SetWipeCopy(val_);
    }
  }
};

struct objFreeAll : public RLOpcode<> {
  virtual void operator()(RLMachine& machine) override {
    ForEachTargetObject(
        machine, this, [](GraphicsObject& object) { object.FreeObjectData(); });
  }
};

struct objInitAll : public RLOpcode<> {
  virtual void operator()(RLMachine& machine) override {
    ForEachTargetObject(machine, this, [](GraphicsObject& object) {
      object.InitializeParams();
    });
  }
};

struct objFreeInitAll : public RLOpcode<> {
  virtual void operator()(RLMachine& machine) override {
    ForEachTargetObject(machine, this, [](GraphicsObject& object) {
      object.FreeObjectData();
      object.InitializeParams();
    });
  }
};

void addObjManagementFunctions(RLModule& m, const std::string& base) {
  m.AddOpcode(
      0, 0, base + "Free",
      std::make_shared<Obj_CallFunction>(&GraphicsObject::FreeObjectData));
  m.AddOpcode(0, 1, base + "Free",
              RangeMappingFun(std::make_shared<Obj_CallFunction>(
                  &GraphicsObject::FreeObjectData)));

  m.AddOpcode(4, 0, base + "WipeCopyOn", std::make_shared<SetWipeCopyTo_0>(1));
  m.AddOpcode(4, 1, base + "WipeCopyOn", std::make_shared<SetWipeCopyTo_1>(1));
  m.AddOpcode(5, 0, base + "WipeCopyOff", std::make_shared<SetWipeCopyTo_0>(0));
  m.AddOpcode(5, 1, base + "WipeCopyOff", std::make_shared<SetWipeCopyTo_1>(0));

  m.AddOpcode(
      10, 0, base + "Init",
      std::make_shared<Obj_CallFunction>(&GraphicsObject::InitializeParams));
  m.AddOpcode(10, 1, base + "Init",
              RangeMappingFun(std::make_shared<Obj_CallFunction>(
                  &GraphicsObject::InitializeParams)));
  m.AddOpcode(11, 0, base + "FreeInit",
              std::make_shared<Obj_CallFunction>(
                  &GraphicsObject::Reset));
  m.AddOpcode(11, 1, base + "FreeInit",
              RangeMappingFun(std::make_shared<Obj_CallFunction>(
                  &GraphicsObject::Reset)));

  m.AddOpcode(100, 0, base + "FreeAll", std::make_shared<objFreeAll>());
  m.AddOpcode(110, 0, base + "InitAll", std::make_shared<objInitAll>());
  m.AddOpcode(111, 0, base + "FreeInitAll", std::make_shared<objFreeInitAll>());
}

struct objChildCopy : public RLOpcode<IntConstant_T, IntConstant_T> {
  int fgbg_;
  explicit objChildCopy(int fgbg) : fgbg_(fgbg) {}

  void operator()(RLMachine& machine, int sbuf, int dbuf) {
    GraphicsSystem& graphics = machine.GetSystem().graphics();
    Stage& stage = machine.stage();

    // By the time we enter this method, our parameters have already been
    // tampered with by the ChildObjAdaptor. So use P_PARENTOBJ as our toplevel
    // object.
    int parentobj;
    if (GetProperty(P_PARENTOBJ, parentobj)) {
      GraphicsObject& go = stage.GetObject(fgbg_, parentobj);
      EnsureIsParentObject(go, graphics.GetObjectLayerSize());

      GraphicsObject& src_obj = go.TouchChild(sbuf);
      go.SetChild(dbuf, src_obj.Clone());
    }
  }
};

struct objFreeInit : public RLOpcode<IntConstant_T> {
  virtual void operator()(RLMachine& machine, int buf) {
    Stage& stage = machine.stage();
    stage.FreeObjectData(buf);
    stage.InitializeObjectParams(buf);
  }
};

struct objFgBgFreeInitAll : public RLOpcode<> {
  virtual void operator()(RLMachine& machine) override {
    Stage& stage = machine.stage();
    stage.FreeAllObjectData();
    stage.InitializeAllObjectParams();
  }
};

}  // namespace

// -----------------------------------------------------------------------

ObjManagement::ObjManagement() : RLModule("ObjManagement", 1, 60) {
  AddOpcode(0, 0, "objFree", CallFunction(&Stage::FreeObjectData));
  AddOpcode(0, 1, "objFree",
            RangeMappingFun(std::shared_ptr<RLOperation>(
                CallFunction(&Stage::FreeObjectData))));

  // TODO: This needs to be reverse engineered. It doesn't seem to be quite
  // equivalent to objWipeCopyOff.
  AddOpcode(1, 0, "objEraseWipeCopy", std::make_shared<SetWipeCopyTo_0>(0));

  AddOpcode(2, 0, "objCopyFgToBg", new objCopyFgToBg_0);
  AddOpcode(2, 1, "objCopyFgToBg", new objCopyFgToBg_1);

  AddOpcode(10, 0, "objInit", CallFunction(&Stage::InitializeObjectParams));
  AddOpcode(10, 1, "objInit",
            RangeMappingFun(std::shared_ptr<RLOperation>(
                CallFunction(&Stage::InitializeObjectParams))));

  AddOpcode(11, 0, "objFreeInit", new objFreeInit);
  AddOpcode(11, 1, "objFreeInit",
            RangeMappingFun(std::make_shared<objFreeInit>()));

  AddOpcode(100, 0, "objFreeAll", CallFunction(&Stage::FreeLayerObjectData));
  AddOpcode(110, 0, "objInitAll",
            CallFunction(&Stage::InitializeAllObjectParams));
  AddOpcode(111, 0, "objFreeInitAll", new objFgBgFreeInitAll);
}

// -----------------------------------------------------------------------

ObjFgManagement::ObjFgManagement() : RLModule("ObjFgManagement", 1, 61) {
  AddOpcode(2, 0, "objCopy", new objCopy(kLayerFg, kLayerFg));
  AddOpcode(3, 0, "objCopyToBg", new objCopy(kLayerFg, kLayerBg));

  addObjManagementFunctions(*this, "objFg");
  SetProperty(P_FGBG, kLayerFg);
}

// -----------------------------------------------------------------------

ObjBgManagement::ObjBgManagement() : RLModule("ObjBgManagement", 1, 62) {
  AddOpcode(2, 0, "objBgCopyToFg", new objCopy(kLayerBg, kLayerFg));
  AddOpcode(3, 0, "objBgCopy", new objCopy(kLayerBg, kLayerBg));

  addObjManagementFunctions(*this, "objBg");
  SetProperty(P_FGBG, kLayerBg);
}

// -----------------------------------------------------------------------

ChildObjFgManagement::ChildObjFgManagement()
    : MappedRLModule(ChildObjMappingFun, "ChildObjFgManagement", 2, 61) {
  AddOpcode(2, 0, "objSetCopy", new objCopy(kLayerFg, kLayerFg));
  AddOpcode(3, 0, "objSetCopyToBg", new objCopy(kLayerFg, kLayerBg));

  AddOpcode(14, 0, "objChildCopy", new objChildCopy(kLayerFg));

  addObjManagementFunctions(*this, "objChildFg");
  SetProperty(P_FGBG, kLayerFg);
}

// -----------------------------------------------------------------------

ChildObjBgManagement::ChildObjBgManagement()
    : MappedRLModule(ChildObjMappingFun, "ChildObjFgManagement", 2, 62) {
  AddOpcode(2, 0, "objSetBgCopyToFg", new objCopy(kLayerBg, kLayerFg));
  AddOpcode(3, 0, "objSetBgCopy", new objCopy(kLayerBg, kLayerBg));

  AddOpcode(14, 0, "objChildCopy", new objChildCopy(kLayerBg));

  addObjManagementFunctions(*this, "objChildBg");
  SetProperty(P_FGBG, kLayerBg);
}

// -----------------------------------------------------------------------
