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

// Contains definitions for object handling functions for the Modules 81
// "ObjFg", 82 "ObjBg", 90 "ObjRange", and 91 "ObjBgRange".

#include "modules/module_obj_fg_bg.hpp"

#include "core/gameexe.hpp"
#include "core/object.hpp"
#include "core/object_internal/drawer/colour_filter.hpp"
#include "core/object_internal/drawer/text.hpp"
#include "core/object_internal/objdrawer.hpp"
#include "core/object_internal/object_mutator.hpp"
#include "core/stage.hpp"
#include "machine/long_operation.hpp"
#include "machine/properties.hpp"
#include "machine/rlmachine.hpp"
#include "machine/rlmodule.hpp"
#include "machine/rloperation.hpp"
#include "machine/rloperation/default_value_t.hpp"
#include "machine/rloperation/rect_t.hpp"
#include "modules/module_obj.hpp"
#include "modules/object_mutator_operations.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"
#include "utilities/exception.hpp"
#include "utilities/graphics.hpp"
#include "utilities/string_utilities.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {
using namespace std::placeholders;

struct dispArea_0 : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int buf) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, buf);
    obj.Param().ClearClipRect();
  }
};

struct dispArea_1 : RLOpcode<IntConstant_T,
                             IntConstant_T,
                             IntConstant_T,
                             IntConstant_T,
                             IntConstant_T> {
  void operator()(RLMachine& machine, int buf, int x1, int y1, int x2, int y2) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, buf);
    obj.Param().SetClipRect(Rect::GRP(x1, y1, x2, y2));
  }
};

struct dispRect_1 : RLOpcode<IntConstant_T,
                             IntConstant_T,
                             IntConstant_T,
                             IntConstant_T,
                             IntConstant_T> {
  void operator()(RLMachine& machine, int buf, int x, int y, int w, int h) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, buf);
    obj.Param().SetClipRect(Rect::REC(x, y, w, h));
  }
};

struct dispCorner_1 : RLOpcode<IntConstant_T, IntConstant_T, IntConstant_T> {
  void operator()(RLMachine& machine, int buf, int x, int y) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, buf);
    obj.Param().SetClipRect(Rect::GRP(0, 0, x, y));
  }
};

struct dispOwnArea_0 : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int buf) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, buf);
    obj.Param().ClearOwnClipRect();
  }
};

struct dispOwnArea_1 : RLOpcode<IntConstant_T,
                                IntConstant_T,
                                IntConstant_T,
                                IntConstant_T,
                                IntConstant_T> {
  void operator()(RLMachine& machine, int buf, int x1, int y1, int x2, int y2) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, buf);
    obj.Param().SetOwnClipRect(Rect::GRP(x1, y1, x2, y2));
  }
};

struct dispOwnRect_1 : RLOpcode<IntConstant_T,
                                IntConstant_T,
                                IntConstant_T,
                                IntConstant_T,
                                IntConstant_T> {
  void operator()(RLMachine& machine, int buf, int x, int y, int w, int h) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, buf);
    obj.Param().SetOwnClipRect(Rect::REC(x, y, w, h));
  }
};

struct adjust
    : RLOpcode<IntConstant_T, IntConstant_T, IntConstant_T, IntConstant_T> {
  void operator()(RLMachine& machine, int buf, int idx, int x, int y) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, buf);
    obj.Param().SetXAdjustment(idx, x);
    obj.Param().SetYAdjustment(idx, y);
  }
};

struct tint
    : RLOpcode<IntConstant_T, IntConstant_T, IntConstant_T, IntConstant_T> {
  void operator()(RLMachine& machine, int buf, int r, int g, int b) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, buf);
    obj.Param().SetTint(RGBColour(r, g, b));
  }
};

struct colour : RLOpcode<IntConstant_T,
                         IntConstant_T,
                         IntConstant_T,
                         IntConstant_T,
                         IntConstant_T> {
  void operator()(RLMachine& machine, int buf, int r, int g, int b, int level) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, buf);
    obj.Param().SetColour(RGBAColour(r, g, b, level));
  }
};

struct objSetRect_1 : public RLOpcode<IntConstant_T, Rect_T<rect_impl::GRP>> {
  void operator()(RLMachine& machine, int buf, Rect rect) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, buf);
    ColourFilterObjectData* data = obj.GetDrawer<ColourFilterObjectData>();
    if (data) {
      data->set_rect(rect);
    }
  }
};

struct objSetRect_0 : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int buf) {
    Rect rect(0, 0, GetScreenSize(machine.GetSystem().gameexe()));
    objSetRect_1()(machine, buf, rect);
  }
};

struct objSetText : public RLOpcode<IntConstant_T, DefaultStrValue_T> {
  void operator()(RLMachine& machine, int buf, string val) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, buf);
    std::string utf8str = cp932toUTF8(val, machine.GetTextEncoding());
    obj.Param().SetTextText(utf8str);
  }
};

// Note: It appears that the RealLive API changed sometime between when Haeleth
// was working on Kanon and RealLive Max. Previously, the zeroth overload took
// the shadow color. Now, the zeroth doesn't and a new first overload does. Use
// DefaultIntValue to try to handle both cases at once.
struct objTextOpts : public RLOpcode<IntConstant_T,
                                     IntConstant_T,
                                     IntConstant_T,
                                     IntConstant_T,
                                     IntConstant_T,
                                     IntConstant_T,
                                     DefaultIntValue_T<-1>> {
  void operator()(RLMachine& machine,
                  int buf,
                  int size,
                  int xspace,
                  int yspace,
                  int char_count,
                  int colour,
                  int shadow) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, buf);
    obj.Param().SetTextOps(size, xspace, yspace, char_count, colour, shadow);
  }
};

struct objDriftOpts : public RLOpcode<IntConstant_T,
                                      IntConstant_T,
                                      IntConstant_T,
                                      IntConstant_T,
                                      IntConstant_T,
                                      IntConstant_T,
                                      IntConstant_T,
                                      IntConstant_T,
                                      IntConstant_T,
                                      IntConstant_T,
                                      IntConstant_T,
                                      IntConstant_T,
                                      Rect_T<rect_impl::GRP>> {
  void operator()(RLMachine& machine,
                  int buf,
                  int count,
                  int use_animation,
                  int start_pattern,
                  int end_pattern,
                  int total_animaton_time_ms,
                  int yspeed,
                  int period,
                  int amplitude,
                  int use_drift,
                  int unknown,
                  int driftspeed,
                  Rect drift_area) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, buf);
    obj.Param().SetDriftOpts(count, use_animation, start_pattern, end_pattern,
                             total_animaton_time_ms, yspeed, period, amplitude,
                             use_drift, unknown, driftspeed, drift_area);
  }
};

struct objNumOpts : public RLOpcode<IntConstant_T,
                                    IntConstant_T,
                                    IntConstant_T,
                                    IntConstant_T,
                                    IntConstant_T,
                                    IntConstant_T> {
  void operator()(RLMachine& machine,
                  int buf,
                  int digits,
                  int zero,
                  int sign,
                  int pack,
                  int space) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, buf);
    obj.Param().SetDigitOpts(digits, zero, sign, pack, space);
  }
};

struct objAdjustAlpha
    : public RLOpcode<IntConstant_T, IntConstant_T, IntConstant_T> {
  void operator()(RLMachine& machine, int buf, int idx, int alpha) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, buf);
    obj.Param().SetAlphaAdjustment(idx, alpha);
  }
};

struct objButtonOpts : public RLOpcode<IntConstant_T,
                                       IntConstant_T,
                                       IntConstant_T,
                                       IntConstant_T,
                                       IntConstant_T> {
  void operator()(RLMachine& machine,
                  int buf,
                  int action,
                  int se,
                  int group,
                  int button_number) {
    GraphicsObject& obj = GetGraphicsObject(machine, this, buf);
    obj.Param().SetButtonOpts(action, se, group, button_number);
  }
};

// -----------------------------------------------------------------------
class objEveAdjust : public RLOpcode<IntConstant_T,
                                     IntConstant_T,
                                     IntConstant_T,
                                     IntConstant_T,
                                     IntConstant_T,
                                     IntConstant_T,
                                     IntConstant_T> {
 public:
  virtual void operator()(RLMachine& machine,
                          int obj,
                          int repno,
                          int x,
                          int y,
                          int duration_time,
                          int delay,
                          int type) {
    std::shared_ptr<Clock> clock = machine.GetSystem().event().GetClock();

    GraphicsObject& object = GetGraphicsObject(machine, this, obj);
    if (object.IsMutatorRunningMatching(repno, "objEveAdjust"))
      return;

    int start_x = object.Param().x_adjustment(repno);
    int start_y = object.Param().y_adjustment(repno);

    ObjectParameterMutator mutator_x(
        std::bind(CreateSetter<&ObjectParameter::adjustment_offsets_x>(), _1,
                  repno, _2),
        MakeFrameCounter(duration_time, delay, start_x, x, type, clock));
    ObjectParameterMutator mutator_y(
        std::bind(CreateSetter<&ObjectParameter::adjustment_offsets_y>(), _1,
                  repno, _2),
        MakeFrameCounter(duration_time, delay, start_y, y, type,
                         std::move(clock)));
    object.AddObjectMutator(
        ObjectMutator(std::move(mutator_x), repno, "objEveAdjust"));
    object.AddObjectMutator(
        ObjectMutator(std::move(mutator_y), repno, "objEveAdjust"));
  }
};

static void objEveDisplay_impl(GraphicsObject& object,
                               std::shared_ptr<Clock> clock,
                               int display,
                               int duration_time,
                               int delay,
                               int tr_mod,
                               int move_mod,
                               int move_len_x,
                               int move_len_y) {
  ObjectParameter& pm = object.Param();

  int tr_start = display ? 0 : 255, tr_end = display ? 255 : 0;
  int move_start_x = pm.x(), move_end_x = pm.x(), move_start_y = pm.y(),
      move_end_y = pm.y();

  if (display) {
    move_start_x -= move_len_x;
    move_start_y -= move_len_y;
  } else {
    move_end_x += move_len_x;
    move_end_y += move_len_y;
  }

  pm.SetVisible(true);
  if (object.IsMutatorRunningMatching(-1, "objEveDisplay"))
    return;

  const bool hide_on_complete = display == 0;
  if (tr_mod) {
    ObjectMutator alpha_mutator(
        ObjectParameterMutator(
            CreateSetter<&ObjectParameter::alpha_source>(),
            MakeFrameCounter(duration_time, delay, tr_start, tr_end, 0, clock)),
        -1, "objEveDisplay");
    if (!move_mod && hide_on_complete) {
      alpha_mutator.OnComplete(
          [](ObjectParameter& pm) { pm.SetVisible(false); });
    }
    object.AddObjectMutator(std::move(alpha_mutator));
  }

  if (move_mod) {
    object.AddObjectMutator(
        ObjectMutator(ObjectParameterMutator(
                          CreateSetter<&ObjectParameter::position_x>(),
                          MakeFrameCounter(duration_time, delay, move_start_x,
                                           move_end_x, 0, clock)),
                      -1, "objEveDisplay"));

    ObjectMutator y_mutator(
        ObjectParameterMutator(
            CreateSetter<&ObjectParameter::position_y>(),
            MakeFrameCounter(duration_time, delay, move_start_y, move_end_y, 0,
                             std::move(clock))),
        -1, "objEveDisplay");
    if (hide_on_complete) {
      y_mutator.OnComplete([](ObjectParameter& pm) { pm.SetVisible(false); });
    }
    object.AddObjectMutator(std::move(y_mutator));
  }

  if (!tr_mod && !move_mod && hide_on_complete)
    pm.SetVisible(false);
}

struct objEveDisplay_1 : public RLOpcode<IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T> {
  void operator()(RLMachine& machine,
                  int obj,
                  int display,
                  int duration_time,
                  int delay,
                  int objdisp_idx) {
    Gameexe& gameexe = machine.GetSystem().gameexe();
    const std::vector<int> disp = gameexe("OBJDISP", objdisp_idx).ToIntVec();

    GraphicsObject& object = GetGraphicsObject(machine, this, obj);
    std::shared_ptr<Clock> clock = machine.GetSystem().event().GetClock();

    int tr_mod = disp.at(1);
    int move_mod = disp.at(2), move_len_x = disp.at(3), move_len_y = disp.at(4);

    objEveDisplay_impl(object, clock, display, duration_time, delay, tr_mod,
                       move_mod, move_len_x, move_len_y);
  }
};

struct objEveDisplay_2 : public RLOpcode<IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T> {
  void operator()(RLMachine& machine,
                  int obj,
                  int display,
                  int duration_time,
                  int delay,
                  int disp_event_mod,
                  int tr_mod,
                  int move_mod,
                  int move_len_x,
                  int move_len_y) {
    GraphicsObject& object = GetGraphicsObject(machine, this, obj);
    std::shared_ptr<Clock> clock = machine.GetSystem().event().GetClock();

    objEveDisplay_impl(object, clock, display, duration_time, delay, tr_mod,
                       move_mod, move_len_x, move_len_y);
  }
};

struct objEveDisplay_3 : public RLOpcode<IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T,
                                         IntConstant_T> {
  void operator()(RLMachine& machine,
                  int obj,
                  int display,
                  int duration_time,
                  int delay,
                  int disp_event_mod,
                  int tr_mod,
                  int move_mod,
                  int move_len_x,
                  int move_len_y,
                  int rotate_mod,
                  int rotate_count,
                  int scale_x_mod,
                  int scale_x_percent,
                  int scale_y_mod,
                  int scale_y_percent,
                  int sin_mod,
                  int sin_len,
                  int sin_count) {
    GraphicsObject& object = GetGraphicsObject(machine, this, obj);
    std::shared_ptr<Clock> clock = machine.GetSystem().event().GetClock();

    objEveDisplay_impl(object, clock, display, duration_time, delay, tr_mod,
                       move_mod, move_len_x, move_len_y);
  }
};

// -----------------------------------------------------------------------
// -----------------------------------------------------------------------
// -----------------------------------------------------------------------

void addUnifiedFunctions(ObjectModule& h) {
  h.AddDoubleObjectCommands(0, "Move",
                            CreateGetter<&ObjectParameter::position_x>(),
                            CreateSetter<&ObjectParameter::position_x>(),
                            CreateGetter<&ObjectParameter::position_y>(),
                            CreateSetter<&ObjectParameter::position_y>());
  h.AddSingleObjectCommands(1, "Left",
                            CreateGetter<&ObjectParameter::position_x>(),
                            CreateSetter<&ObjectParameter::position_x>());
  h.AddSingleObjectCommands(2, "Top",
                            CreateGetter<&ObjectParameter::position_y>(),
                            CreateSetter<&ObjectParameter::position_y>());
  h.AddSingleObjectCommands(3, "Alpha",
                            CreateGetter<&ObjectParameter::alpha_source>(),
                            CreateSetter<&ObjectParameter::alpha_source>());

  // ----

  h.AddCustomRepno<adjust, objEveAdjust>(6, "Adjust");
  h.AddRepnoObjectCommands(
      7, "AdjustX", CreateGetter<&ObjectParameter::adjustment_offsets_x>(),
      CreateSetter<&ObjectParameter::adjustment_offsets_x>());
  h.AddRepnoObjectCommands(
      8, "AdjustY", CreateGetter<&ObjectParameter::adjustment_offsets_y>(),
      CreateSetter<&ObjectParameter::adjustment_offsets_y>());
  h.AddSingleObjectCommands(
      9, "Mono", CreateGetter<&ObjectParameter::monochrome_transform>(),
      CreateSetter<&ObjectParameter::monochrome_transform>());
  h.AddSingleObjectCommands(10, "Invert",
                            CreateGetter<&ObjectParameter::invert_transform>(),
                            CreateSetter<&ObjectParameter::invert_transform>());
  h.AddSingleObjectCommands(
      11, "Light", [](const ObjectParameter& param) { return param.light(); },
      [](ObjectParameter& param, int value) { param.SetLight(value); });

  // ---

  h.AddSingleObjectCommands(
      13, "TintR",
      [](const ObjectParameter& param) { return param.tint_colour.r(); },
      [](ObjectParameter& param, int value) {
        param.tint_colour.set_red(value);
      });
  h.AddSingleObjectCommands(
      14, "TintG",
      [](const ObjectParameter& param) { return param.tint_colour.g(); },
      [](ObjectParameter& param, int value) {
        param.tint_colour.set_green(value);
      });
  h.AddSingleObjectCommands(
      15, "TintB",
      [](const ObjectParameter& param) { return param.tint_colour.b(); },
      [](ObjectParameter& param, int value) {
        param.tint_colour.set_blue(value);
      });

  // ---

  h.AddSingleObjectCommands(
      17, "ColR",
      [](const ObjectParameter& param) { return param.blend_colour.r(); },
      [](ObjectParameter& param, int value) {
        param.blend_colour.set_red(value);
      });
  h.AddSingleObjectCommands(
      18, "ColG",
      [](const ObjectParameter& param) { return param.blend_colour.g(); },
      [](ObjectParameter& param, int value) {
        param.blend_colour.set_green(value);
      });
  h.AddSingleObjectCommands(
      19, "ColB",
      [](const ObjectParameter& param) { return param.blend_colour.b(); },
      [](ObjectParameter& param, int value) {
        param.blend_colour.set_blue(value);
      });
  h.AddSingleObjectCommands(
      20, "ColLevel",
      [](const ObjectParameter& param) { return param.blend_colour.a(); },
      [](ObjectParameter& param, int value) {
        param.blend_colour.set_alpha(value);
      });

  // ---

  h.AddSingleObjectCommands(
      36, "AdjustVert", CreateGetter<&ObjectParameter::adjustment_vertical>(),
      CreateSetter<&ObjectParameter::adjustment_vertical>());

  h.AddRepnoObjectCommands(40, "AdjustAlpha",
                           CreateGetter<&ObjectParameter::adjustment_alphas>(),
                           CreateSetter<&ObjectParameter::adjustment_alphas>());

  // --
  h.AddDoubleObjectCommands(46, "Scale",
                            CreateGetter<&ObjectParameter::scale_x_percent>(),
                            CreateSetter<&ObjectParameter::scale_x_percent>(),
                            CreateGetter<&ObjectParameter::scale_y_percent>(),
                            CreateSetter<&ObjectParameter::scale_y_percent>());
  h.AddSingleObjectCommands(47, "Width",
                            CreateGetter<&ObjectParameter::scale_x_percent>(),
                            CreateSetter<&ObjectParameter::scale_x_percent>());
  h.AddSingleObjectCommands(48, "Height",
                            CreateGetter<&ObjectParameter::scale_y_percent>(),
                            CreateSetter<&ObjectParameter::scale_y_percent>());
  h.AddSingleObjectCommands(49, "Rotate",
                            CreateGetter<&ObjectParameter::rotation_div10>(),
                            CreateSetter<&ObjectParameter::rotation_div10>());
  h.AddDoubleObjectCommands(
      50, "RepOrigin", CreateGetter<&ObjectParameter::repetition_origin_x>(),
      CreateSetter<&ObjectParameter::repetition_origin_x>(),
      CreateGetter<&ObjectParameter::repetition_origin_y>(),
      CreateSetter<&ObjectParameter::repetition_origin_y>());
  h.AddSingleObjectCommands(
      51, "RepOriginX", CreateGetter<&ObjectParameter::repetition_origin_x>(),
      CreateSetter<&ObjectParameter::repetition_origin_x>());
  h.AddSingleObjectCommands(
      52, "RepOriginY", CreateGetter<&ObjectParameter::repetition_origin_y>(),
      CreateSetter<&ObjectParameter::repetition_origin_y>());
  h.AddDoubleObjectCommands(53, "Origin",
                            CreateGetter<&ObjectParameter::origin_x>(),
                            CreateSetter<&ObjectParameter::origin_x>(),
                            CreateGetter<&ObjectParameter::origin_y>(),
                            CreateSetter<&ObjectParameter::origin_y>());
  h.AddSingleObjectCommands(54, "OriginX",
                            CreateGetter<&ObjectParameter::origin_x>(),
                            CreateSetter<&ObjectParameter::origin_x>());
  h.AddSingleObjectCommands(55, "OriginY",
                            CreateGetter<&ObjectParameter::origin_y>(),
                            CreateSetter<&ObjectParameter::origin_y>());

  // ---

  h.AddDoubleObjectCommands(
      61, "HqScale",
      CreateGetter<&ObjectParameter::high_quality_scale_x_percent>(),
      CreateSetter<&ObjectParameter::high_quality_scale_x_percent>(),
      CreateGetter<&ObjectParameter::high_quality_scale_y_percent>(),
      CreateSetter<&ObjectParameter::high_quality_scale_y_percent>());
  h.AddSingleObjectCommands(
      62, "HqWidth",
      CreateGetter<&ObjectParameter::high_quality_scale_x_percent>(),
      CreateSetter<&ObjectParameter::high_quality_scale_x_percent>());
  h.AddSingleObjectCommands(
      63, "HqHeight",
      CreateGetter<&ObjectParameter::high_quality_scale_y_percent>(),
      CreateSetter<&ObjectParameter::high_quality_scale_y_percent>());
}

void addObjectFunctions(RLModule& m) {
  m.AddOpcode(
      1004, 0, "objShow",
      new Obj_SetOneIntOnObj(CreateSetter<&ObjectParameter::is_visible>()));
  m.AddOpcode(1005, 0, "objDispArea", new dispArea_0);
  m.AddOpcode(1005, 1, "objDispArea", new dispArea_1);

  m.AddOpcode(1012, 0, "objTint", new tint);

  m.AddOpcode(1016, 0, "objColour", new colour);

  m.AddOpcode(
      1021, 0, "objComposite",
      new Obj_SetOneIntOnObj(CreateSetter<&ObjectParameter::composite_mode>()));

  m.AddOpcode(1022, 0, "objSetRect", new objSetRect_0);
  m.AddOpcode(1022, 1, "objSetRect", new objSetRect_1);

  m.AddOpcode(1024, 0, "objSetText", new objSetText);
  m.AddOpcode(1024, 1, "objSetText", new objSetText);
  m.AddOpcode(1025, 0, "objTextOpts", new objTextOpts);
  m.AddOpcode(1025, 1, "objTextOpts", new objTextOpts);

  m.AddOpcode(
      1026, 0, "objLayer",
      new Obj_SetOneIntOnObj(CreateSetter<&ObjectParameter::z_layer>()));
  m.AddOpcode(
      1027, 0, "objDepth",
      new Obj_SetOneIntOnObj(CreateSetter<&ObjectParameter::z_depth>()));
  m.AddUnsupportedOpcode(1028, 0, "objScrollRate");
  m.AddOpcode(
      1029, 0, "objScrollRateX",
      new Obj_SetOneIntOnObj(CreateSetter<&ObjectParameter::scroll_rate_x>()));
  m.AddOpcode(
      1030, 0, "objScrollRateY",
      new Obj_SetOneIntOnObj(CreateSetter<&ObjectParameter::scroll_rate_y>()));
  m.AddOpcode(1031, 0, "objDriftOpts", new objDriftOpts);
  m.AddOpcode(
      1032, 0, "objOrder",
      new Obj_SetOneIntOnObj(CreateSetter<&ObjectParameter::z_order>()));
  m.AddUnsupportedOpcode(1033, 0, "objQuarterView");

  m.AddOpcode(1034, 0, "objDispRect", new dispArea_0);
  m.AddOpcode(1034, 1, "objDispRect", new dispRect_1);
  m.AddOpcode(1035, 0, "objDispCorner", new dispArea_0);
  m.AddOpcode(1035, 1, "objDispCorner", new dispArea_1);
  m.AddOpcode(1035, 2, "objDispCorner", new dispCorner_1);

  m.AddOpcode(1037, 0, "objSetDigitValue",
              new Obj_SetOneIntOnObj([](ObjectParameter& param, int value) {
                param.digit.value = value;
              }));
  m.AddOpcode(1038, 0, "objNumOpts", new objNumOpts);
  m.AddOpcode(
      1039, 0, "objPattNo",
      new Obj_SetOneIntOnObj(CreateSetter<&ObjectParameter::pattern_number>()));

  m.AddUnsupportedOpcode(1041, 0, "objAdjustAll");
  m.AddUnsupportedOpcode(1042, 0, "objAdjustAllX");
  m.AddUnsupportedOpcode(1043, 0, "objAdjustAllY");

  m.AddUnsupportedOpcode(1056, 0, "objFadeOpts");

  m.AddOpcode(1064, 2, "objButtonOpts", new objButtonOpts);
  m.AddOpcode(1066, 0, "objBtnState",
              new Obj_SetOneIntOnObj([](ObjectParameter& param, int value) {
                param.button.state = value;
              }));

  m.AddOpcode(1070, 0, "objOwnDispArea", new dispOwnArea_0);
  m.AddOpcode(1070, 1, "objOwnDispArea", new dispOwnArea_1);
  m.AddOpcode(1071, 0, "objOwnDispRect", new dispOwnArea_0);
  m.AddOpcode(1071, 1, "objOwnDispRect", new dispOwnRect_1);
}

void addEveObjectFunctions(RLModule& m) {
  m.AddOpcode(
      2004, 0, "objEveDisplay",
      new Obj_SetOneIntOnObj(CreateSetter<&ObjectParameter::is_visible>()));
  m.AddOpcode(2004, 1, "objEveDisplay", new objEveDisplay_1);
  m.AddOpcode(2004, 2, "objEveDisplay", new objEveDisplay_2);
  m.AddOpcode(2004, 3, "objEveDisplay", new objEveDisplay_3);

  m.AddOpcode(3004, 0, "objEveDisplayCheck",
              new Op_MutatorCheck("objEveDisplay"));

  m.AddOpcode(4004, 0, "objEveDisplayWait",
              new Op_MutatorWaitNormal("objEveDisplay"));

  m.AddOpcode(5004, 0, "objEveDisplayWaitC",
              new Op_MutatorWaitCNormal("objEveDisplay"));

  m.AddOpcode(6004, 0, "objEveDisplayEnd",
              new Op_EndObjectMutation_Normal("objEveDisplay"));
}

}  // namespace

// -----------------------------------------------------------------------
// -----------------------------------------------------------------------
// -----------------------------------------------------------------------

ObjFgModule::ObjFgModule() : RLModule("ObjFg", 1, 81), helper_("obj", this) {
  addUnifiedFunctions(helper_);

  addObjectFunctions(*this);
  addEveObjectFunctions(*this);
  SetProperty(P_FGBG, kLayerFg);
}

// -----------------------------------------------------------------------

ObjBgModule::ObjBgModule() : RLModule("ObjBg", 1, 82), helper_("objBg", this) {
  addUnifiedFunctions(helper_);

  addObjectFunctions(*this);
  addEveObjectFunctions(*this);
  SetProperty(P_FGBG, kLayerBg);
}

// -----------------------------------------------------------------------

ChildObjFgModule::ChildObjFgModule()
    : MappedRLModule(ChildObjMappingFun, "ChildObjFg", 2, 81),
      helper_("objChild", this) {
  addUnifiedFunctions(helper_);

  addObjectFunctions(*this);
  addEveObjectFunctions(*this);
  SetProperty(P_FGBG, kLayerFg);
}

// -----------------------------------------------------------------------

ChildObjBgModule::ChildObjBgModule()
    : MappedRLModule(ChildObjMappingFun, "ChildObjBg", 2, 82),
      helper_("objChildBg", this) {
  addUnifiedFunctions(helper_);

  addObjectFunctions(*this);
  addEveObjectFunctions(*this);
  SetProperty(P_FGBG, kLayerBg);
}

// -----------------------------------------------------------------------

ObjRangeFgModule::ObjRangeFgModule()
    : MappedRLModule(RangeMappingFun, "ObjRangeFg", 1, 90),
      helper_("objRange", this) {
  addUnifiedFunctions(helper_);

  addObjectFunctions(*this);
  SetProperty(P_FGBG, kLayerFg);
}

// -----------------------------------------------------------------------

ObjRangeBgModule::ObjRangeBgModule()
    : MappedRLModule(RangeMappingFun, "ObjRangeBg", 1, 91),
      helper_("objRangeBg", this) {
  addUnifiedFunctions(helper_);

  addObjectFunctions(*this);
  SetProperty(P_FGBG, kLayerBg);
}

// -----------------------------------------------------------------------

ChildObjRangeFgModule::ChildObjRangeFgModule()
    : MappedRLModule(ChildRangeMappingFun, "ObjChildRangeFg", 2, 90),
      helper_("objChildRange", this) {
  addUnifiedFunctions(helper_);

  addObjectFunctions(*this);
  SetProperty(P_FGBG, kLayerFg);
}

// -----------------------------------------------------------------------

ChildObjRangeBgModule::ChildObjRangeBgModule()
    : MappedRLModule(ChildRangeMappingFun, "ObjChildRangeBg", 2, 91),
      helper_("objChildRangeBg", this) {
  addUnifiedFunctions(helper_);

  addObjectFunctions(*this);
  SetProperty(P_FGBG, kLayerBg);
}
