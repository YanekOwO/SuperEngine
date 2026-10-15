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

// Implements various commands that don't fit in other modules.
//
// VisualArts appears to have used this as a dumping ground for any operations
// that don't otherwise fit into other categories. Because of this, the
// implementation has been split along themes into the different
// Module_Sys_*.cpp files.

#include "modules/module_sys.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#include "core/cgm_table.hpp"
#include "core/gameexe.hpp"
#include "core/haikei.hpp"
#include "core/rlevent_listener.hpp"
#include "effects/fade_effect.hpp"
#include "machine/general_operations.hpp"
#include "machine/rloperation.hpp"
#include "machine/rloperation/default_value_t.hpp"
#include "modules/config_operations.hpp"
#include "modules/jump.hpp"
#include "modules/module_bgr.hpp"
#include "modules/module_grp.hpp"
#include "modules/module_sys_date.hpp"
#include "modules/module_sys_frame.hpp"
#include "modules/module_sys_index_series.hpp"
#include "modules/module_sys_name.hpp"
#include "modules/module_sys_save.hpp"
#include "modules/module_sys_syscom.hpp"
#include "modules/module_sys_timer.hpp"
#include "modules/module_sys_timetable2.hpp"
#include "modules/module_sys_wait.hpp"
#include "systems/graphics_system.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"
#include "utilities/string_utilities.hpp"

inline constexpr float PI = std::numbers::pi;

namespace {

struct title : public RLOpcode<StrConstant_T> {
  void operator()(RLMachine& machine, std::string subtitle) {
    machine.GetSystem().graphics().SetWindowSubtitle(subtitle,
                                                     machine.GetTextEncoding());
  }
};

struct GetTitle : public RLOpcode<StrReference_T> {
  void operator()(RLMachine& machine, StringReferenceIterator dest) {
    *dest = machine.GetSystem().graphics().window_subtitle();
  }
};

struct GetCursorPos_gc1 : public RLOpcode<IntReference_T,
                                          IntReference_T,
                                          IntReference_T,
                                          IntReference_T> {
  void operator()(RLMachine& machine,
                  IntReferenceIterator xit,
                  IntReferenceIterator yit,
                  IntReferenceIterator button1It,
                  IntReferenceIterator button2It) {
    Point pos;
    int button1, button2;
    machine.GetSystem().rlEvent().GetCursorPos(pos, button1, button2);
    *xit = pos.x();
    *yit = pos.y();
    *button1It = button1;
    *button2It = button2;
  }
};

struct GetCursorPos_gc2 : public RLOpcode<IntReference_T, IntReference_T> {
  void operator()(RLMachine& machine,
                  IntReferenceIterator xit,
                  IntReferenceIterator yit) {
    Point pos = machine.GetSystem().rlEvent().GetCursorPos();
    *xit = pos.x();
    *yit = pos.y();
  }
};

struct CallStackPop : RLOpcode<DefaultIntValue_T<1>> {
  void operator()(RLMachine& machine, int frames_to_pop) {
    for (int i = 0; i < frames_to_pop; ++i) {
      machine.GetCallStack().Pop();
    }
  }
};

struct CallStackSize : RLStoreOpcode<> {
  int operator()(RLMachine& machine) { return machine.GetCallStack().Size(); }
};

struct PauseCursor : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int newCursor) {
    machine.GetSystem().text().SetKeyCursor(newCursor);
  }
};

struct GetWakuAll : public RLStoreOpcode<> {
  int operator()(RLMachine& machine) {
    std::shared_ptr<TextWindow> window =
        machine.GetSystem().text().GetCurrentMwnd();
    return window->waku_set();
  }
};

struct rnd_0 : public RLStoreOpcode<IntConstant_T> {
  std::mt19937 generator_;
  rnd_0()
      : generator_(static_cast<std::mt19937::result_type>(std::time(nullptr))) {
  }

  int operator()(RLMachine& machine, int maxVal) {
    return (int)(double(maxVal) * generator_() / (std::mt19937::max() + 1.0));
  }
};

struct rnd_1 : public RLStoreOpcode<IntConstant_T, IntConstant_T> {
  std::mt19937 generator_;
  rnd_1()
      : generator_(static_cast<std::mt19937::result_type>(std::time(nullptr))) {
  }

  int operator()(RLMachine& machine, int minVal, int maxVal) {
    return minVal + (int)(double(maxVal - minVal) * generator_() /
                          (std::mt19937::max() + 1.0));
  }
};

struct pcnt : public RLStoreOpcode<IntConstant_T, IntConstant_T> {
  int operator()(RLMachine& machine, int numenator, int denominator) {
    return int(((float)numenator / (float)denominator) * 100);
  }
};

struct Sys_abs : public RLStoreOpcode<IntConstant_T> {
  int operator()(RLMachine& machine, int var) { return abs(var); }
};

struct power_0 : public RLStoreOpcode<IntConstant_T> {
  int operator()(RLMachine& machine, int var) { return var * var; }
};

struct power_1 : public RLStoreOpcode<IntConstant_T, IntConstant_T> {
  int operator()(RLMachine& machine, int var1, int var2) {
    return (int)std::pow((float)var1, var2);
  }
};

struct sin_0 : public RLStoreOpcode<IntConstant_T> {
  int operator()(RLMachine& machine, int var1) {
    return int(std::sin(var1 * (PI / 180)) * 32640);
  }
};

struct sin_1 : public RLStoreOpcode<IntConstant_T, IntConstant_T> {
  int operator()(RLMachine& machine, int var1, int var2) {
    return int(std::sin(var1 * (PI / 180)) * 32640 / var2);
  }
};

struct Sys_modulus : public RLStoreOpcode<IntConstant_T,
                                          IntConstant_T,
                                          IntConstant_T,
                                          IntConstant_T> {
  int operator()(RLMachine& machine, int var1, int var2, int var3, int var4) {
    return int(float(var1 - var3) / float(var2 - var4));
  }
};

struct angle : public RLStoreOpcode<IntConstant_T,
                                    IntConstant_T,
                                    IntConstant_T,
                                    IntConstant_T> {
  int operator()(RLMachine& machine, int var1, int var2, int var3, int var4) {
    return int(float(var1 - var3) / float(var2 - var4));
  }
};

struct Sys_min : public RLStoreOpcode<IntConstant_T, IntConstant_T> {
  int operator()(RLMachine& machine, int var1, int var2) {
    return std::min(var1, var2);
  }
};

struct Sys_max : public RLStoreOpcode<IntConstant_T, IntConstant_T> {
  int operator()(RLMachine& machine, int var1, int var2) {
    return std::max(var1, var2);
  }
};

struct constrain
    : public RLStoreOpcode<IntConstant_T, IntConstant_T, IntConstant_T> {
  int operator()(RLMachine& machine, int var1, int var2, int var3) {
    if (var2 < var1)
      return var1;
    else if (var2 > var3)
      return var3;
    else
      return var2;
  }
};

struct cos_0 : public RLStoreOpcode<IntConstant_T> {
  int operator()(RLMachine& machine, int var1) {
    return int(std::cos(var1 * (PI / 180)) * 32640);
  }
};

struct cos_1 : public RLStoreOpcode<IntConstant_T, IntConstant_T> {
  int operator()(RLMachine& machine, int var1, int var2) {
    return int(std::cos(var1 * (PI / 180)) * 32640 / var2);
  }
};

// Implements op<0:Sys:01203, 0>, ReturnMenu.
//
// Jumps the instruction pointer to the begining of the scenario defined in the
// Gameexe key #SEEN_MENU.
//
// This method also resets a LOT of the game state, though this isn't mentioned
// in the rldev manual.
struct ReturnMenu : public RLOpcode<> {
  void operator()(RLMachine& machine) override {
    int scenario = machine.GetSystem().gameexe()("SEEN_MENU").ToInt();
    machine.LocalReset();
    Jump(machine, scenario, 0);
  }
};

struct ReturnPrevSelect : public RLOpcode<> {
  void operator()(RLMachine& machine) override {
    machine.GetSystem().RestoreSelectionSnapshot(machine);
  }
};

struct SetWindowAttr : public RLOpcode<IntConstant_T,
                                       IntConstant_T,
                                       IntConstant_T,
                                       IntConstant_T,
                                       IntConstant_T> {
  void operator()(RLMachine& machine, int r, int g, int b, int a, int f) {
    auto& config = machine.GetGlobalConfig();
    config.filter_color = RGBAColour(r, g, b, a);
    config.reallive.window_filter = f;
    machine.GetSystem().text().ApplyConfiguration();
  }
};

struct GetWindowAttr : public RLOpcode<IntReference_T,
                                       IntReference_T,
                                       IntReference_T,
                                       IntReference_T,
                                       IntReference_T> {
  void operator()(RLMachine& machine,
                  IntReferenceIterator r,
                  IntReferenceIterator g,
                  IntReferenceIterator b,
                  IntReferenceIterator a,
                  IntReferenceIterator f) {
    const auto& config = machine.GetGlobalConfig();
    const auto& colour = config.filter_color;
    *r = colour.r();
    *g = colour.g();
    *b = colour.b();
    *a = colour.a();
    *f = config.reallive.window_filter;
  }
};

struct DefWindowAttr : public RLOpcode<IntReference_T,
                                       IntReference_T,
                                       IntReference_T,
                                       IntReference_T,
                                       IntReference_T> {
  void operator()(RLMachine& machine,
                  IntReferenceIterator r,
                  IntReferenceIterator g,
                  IntReferenceIterator b,
                  IntReferenceIterator a,
                  IntReferenceIterator f) {
    Gameexe& gexe = machine.GetSystem().gameexe();
    std::vector<int> attr = gexe("WINDOW_ATTR").ToIntVec();

    *r = attr.at(0);
    *g = attr.at(1);
    *b = attr.at(2);
    *a = attr.at(3);
    *f = attr.at(4);
  }
};

struct GetGlobalConfigSetting : public RLStoreOpcode<> {
  GetGlobalConfigSetting(std::function<int(const GlobalConfig&)> fn)
      : fn_(fn) {}

  std::function<int(const GlobalConfig&)> fn_;

  int operator()(RLMachine& machine) {
    const GlobalConfig& settings = machine.GetGlobalConfig();
    return fn_(settings);
  }
};

struct ChangeGlobalConfigSetting : public RLOpcode<IntConstant_T> {
  ChangeGlobalConfigSetting(std::function<void(GlobalConfig&, int)> fn)
      : fn_(fn) {}

  std::function<void(GlobalConfig&, int)> fn_;

  void operator()(RLMachine& machine, int value) {
    fn_(machine.GetGlobalConfig(), value);
    machine.GetSystem().sound().ApplyConfiguration();
  }
};

struct SetGeneric1 : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int value) {
    auto& generics = machine.GetGlobalConfig().reallive.generic;
    generics[0] = value;
  }
};
struct SetGeneric2 : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int value) {
    auto& generics = machine.GetGlobalConfig().reallive.generic;
    generics[1] = value;
  }
};
struct GetGeneric1 : public RLStoreOpcode<IntConstant_T> {
  int operator()(RLMachine& machine, int value) {
    const auto& generics = machine.GetGlobalConfig().reallive.generic;
    return generics[0];
  }
};
struct GetGeneric2 : public RLStoreOpcode<IntConstant_T> {
  int operator()(RLMachine& machine, int value) {
    const auto& generics = machine.GetGlobalConfig().reallive.generic;
    return generics[1];
  }
};

struct StrSetter : public RLOpcode<StrConstant_T> {
  std::string LocalRealLiveConfig::* field;

  explicit StrSetter(std::string LocalRealLiveConfig::* member)
      : field(member) {}

  void operator()(RLMachine& machine, std::string value) override {
    machine.GetLocalConfig().reallive.*field = std::move(value);
  }
};

struct StrGetter : public RLOpcode<StrReference_T> {
  std::string LocalRealLiveConfig::* field;

  explicit StrGetter(std::string LocalRealLiveConfig::* member)
      : field(member) {}

  void operator()(RLMachine& machine, StringReferenceIterator dst) override {
    *dst = machine.GetLocalConfig().reallive.*field;
  }
};

struct CtrlPressed : public RLStoreOpcode<> {
  int operator()(RLMachine& machine) override {
    auto& el = machine.GetSystem().rlEvent();
    return el.CtrlPressed() ? 1 : 0;
  }
};

struct ShiftPressed : public RLStoreOpcode<> {
  int operator()(RLMachine& machine) override {
    auto& el = machine.GetSystem().rlEvent();
    return el.ShiftPressed() ? 1 : 0;
  }
};

struct FlushClick : public RLOpcode<> {
  void operator()(RLMachine& machine) override {
    machine.GetSystem().rlEvent().FlushMouseClicks();
  }
};

}  // namespace

// Implementation isn't in anonymous namespace since it is used elsewhere.
void Sys_MenuReturn::operator()(RLMachine& machine) {
  GraphicsSystem& graphics = machine.GetSystem().graphics();

  // Render the screen as is.
  (void)machine.haikei().GetDC(0);
  std::shared_ptr<SDLSurface> before = graphics.RenderToSurface();

  // Clear everything
  machine.LocalReset();

  std::shared_ptr<SDLSurface> after = graphics.RenderToSurface();

  // First, we jump the instruction pointer to the new location.
  int scenario = machine.GetSystem().gameexe()("SEEN_MENU").ToInt();
  Jump(machine, scenario, 0);

  // Now we push a LongOperation on top of the stack; when this
  // ends, we'll be at SEEN_MENU.
  auto effect = std::make_shared<FadeEffect>(machine, after, before,
                                             after->GetSize(), 1000);
  machine.PushLongOperation(effect);
}

SysModule::SysModule() : RLModule("Sys", 1, 004) {
  AddOpcode(0, 0, "title", new title);
  AddOpcode(2, 0, "GetTitle", new GetTitle);

  AddOpcode(130, 0, "FlushClick", new FlushClick);
  AddOpcode(133, 0, "GetCursorPos", new GetCursorPos_gc1);

  AddOpcode(202, 0, "GetCursorPos", new GetCursorPos_gc2);

  AddUnsupportedOpcode(320, 0, "CallStackClear");
  AddUnsupportedOpcode(321, 0, "CallStackNop");
  AddUnsupportedOpcode(321, 1, "CallStackNop");
  AddOpcode(322, 0, "CallStackPop", new CallStackPop);
  AddOpcode(322, 1, "CallStackPop", new CallStackPop);
  AddOpcode(323, 0, "CallStackSize", new CallStackSize);
  AddUnsupportedOpcode(324, 0, "CallStackTrunc");

  AddOpcode(204, 0, "ShowCursor", new ConfigAction([](RLMachine& machine) {
              machine.GetLocalConfig().cursor_disp_off = false;
            }));
  AddOpcode(205, 0, "HideCursor", new ConfigAction([](RLMachine& machine) {
              machine.GetLocalConfig().cursor_disp_off = true;
            }));
  AddOpcode(206, 0, "GetMouseCursor",
            new ConfigIntGetter([](RLMachine& machine) {
              return machine.GetLocalConfig().cursor_no;
            }));
  AddOpcode(207, 0, "MouseCursor",
            new ConfigIntSetter([](RLMachine& machine, int value) {
              machine.GetLocalConfig().cursor_no = value;
              machine.GetSystem().graphics().ApplyConfiguration();
            }));

  AddUnsupportedOpcode(330, 0, "EnableSkipMode");
  AddUnsupportedOpcode(331, 0, "DisableSkipMode");
  AddOpcode(332, 0, "LocalSkipMode", ReturnIntValue(&TextSystem::skip_mode));
  AddOpcode(333, 0, "SetLocalSkipMode",
            CallFunctionWith(&TextSystem::SetSkipMode, 1));
  AddOpcode(334, 0, "ClearLocalSkipMode",
            CallFunctionWith(&TextSystem::SetSkipMode, 0));

  AddOpcode(350, 0, "CtrlKeyShip", new ConfigIntGetter([](RLMachine& machine) {
              return !machine.GetLocalConfig().disable_ctrl;
            }));
  AddOpcode(351, 0, "CtrlKeySkipOn", new ConfigAction([](RLMachine& machine) {
              machine.GetLocalConfig().disable_ctrl = false;
            }));
  AddOpcode(352, 0, "CtrlKeySkipOff", new ConfigAction([](RLMachine& machine) {
              machine.GetLocalConfig().disable_ctrl = true;
            }));
  AddOpcode(353, 0, "CtrlPressed", new CtrlPressed);
  AddOpcode(354, 0, "ShiftPressed", new ShiftPressed);

  AddOpcode(364, 0, "PauseCursor", new PauseCursor);

  AddUnsupportedOpcode(400, 0, "GetWindowPos");
  AddUnsupportedOpcode(401, 0, "SetWindowPos");
  AddUnsupportedOpcode(402, 0, "WindowResetPos");
  AddUnsupportedOpcode(403, 0, "GetDefaultWindowPos");
  AddUnsupportedOpcode(404, 0, "SetDefaultWindowPos");
  AddUnsupportedOpcode(405, 0, "DefaultWindowResetPos");

  AddOpcode(410, 0, "GetWakuAll", new GetWakuAll);
  AddUnsupportedOpcode(411, 0, "SetWakuAll");
  AddUnsupportedOpcode(412, 0, "GetWaku");
  AddUnsupportedOpcode(413, 0, "SetWaku");
  AddUnsupportedOpcode(414, 0, "GetWakuMod");
  AddUnsupportedOpcode(415, 0, "SetWakuMod__dwm");
  AddUnsupportedOpcode(416, 0, "SetWakuMod__ewm");

  AddUnsupportedOpcode(460, 0, "EnableWindowAnm");
  AddUnsupportedOpcode(461, 0, "DisableWindowAnm");
  AddUnsupportedOpcode(462, 0, "GetOpenAnmMod");
  AddUnsupportedOpcode(463, 0, "SetOpenAnmMod");
  AddUnsupportedOpcode(464, 0, "GetOpenAnmTime");
  AddUnsupportedOpcode(465, 0, "SetOpenAnmTime");
  AddUnsupportedOpcode(466, 0, "GetCloseAnmMod");
  AddUnsupportedOpcode(467, 0, "SetCloseAnmMod");
  AddUnsupportedOpcode(468, 0, "GetCloseAnmTime");
  AddUnsupportedOpcode(469, 0, "SetCloseAnmTime");

  AddOpcode(1000, 0, "rnd", new rnd_0);
  AddOpcode(1000, 1, "rnd", new rnd_1);
  AddOpcode(1001, 0, "pcnt", new pcnt);
  AddOpcode(1002, 0, "abs", new Sys_abs);
  AddOpcode(1003, 0, "power", new power_0);
  AddOpcode(1003, 1, "power", new power_1);
  AddOpcode(1004, 0, "sin", new sin_0);
  AddOpcode(1004, 1, "sin", new sin_1);
  AddOpcode(1005, 0, "modulus", new Sys_modulus);
  AddOpcode(1006, 0, "angle", new angle);
  AddOpcode(1007, 0, "min", new Sys_min);
  AddOpcode(1008, 0, "max", new Sys_max);
  AddOpcode(1009, 0, "constrain", new constrain);
  AddOpcode(1010, 0, "cos", new cos_0);
  AddOpcode(1010, 1, "cos", new cos_1);
  // sign 01011
  // (unknown) 01012
  // (unknown) 01013

  AddOpcode(1120, 0, "SceneNum", ReturnIntValue(&RLMachine::SceneNumber));

  AddOpcode(1200, 0, "end", CallFunction(&RLMachine::Halt));
  AddOpcode(1201, 0, "MenuReturn", new Sys_MenuReturn);
  AddOpcode(1202, 0, "MenuReturn2", new Sys_MenuReturn);
  AddOpcode(1203, 0, "ReturnMenu", new ReturnMenu);
  AddOpcode(1204, 0, "ReturnPrevSelect", new ReturnPrevSelect);
  AddOpcode(1205, 0, "ReturnPrevSelect2", new ReturnPrevSelect);

  AddOpcode(1130, 0, "DefaultGrp",
            new StrGetter(&LocalRealLiveConfig::default_grp_name));
  AddOpcode(1131, 0, "SetDefaultGrp",
            new StrSetter(&LocalRealLiveConfig::default_grp_name));
  AddOpcode(1132, 0, "DefaultBgr",
            new StrGetter(&LocalRealLiveConfig::default_bgr_name));
  AddOpcode(1133, 0, "SetDefaultBgr",
            new StrSetter(&LocalRealLiveConfig::default_bgr_name));

  AddUnsupportedOpcode(1302, 0, "nwSingle");
  AddUnsupportedOpcode(1303, 0, "nwMulti");
  AddUnsupportedOpcode(1312, 0, "nwSingleLocal");
  AddUnsupportedOpcode(1313, 0, "nwMultiLocal");

  AddOpcode(1500, 0, "cgGetTotal", ReturnIntValue(&CGMTable::GetTotal));
  AddOpcode(1501, 0, "cgGetViewed", ReturnIntValue(&CGMTable::GetViewed));
  AddOpcode(1502, 0, "cgGetViewedPcnt", ReturnIntValue(&CGMTable::GetPercent));
  AddOpcode(1503, 0, "cgGetFlag", ReturnIntValue(&CGMTable::GetFlag));
  AddOpcode(1504, 0, "cgStatus", ReturnIntValue(&CGMTable::GetStatus));

  AddUnsupportedOpcode(2050, 0, "SetCursorMono");
  AddUnsupportedOpcode(2000, 0, "CursorMono");
  AddOpcode(2051, 0, "SetSkipAnimations",
            new ConfigIntSetter([](RLMachine& machine, int value) {
              machine.GetGlobalConfig().system.disable_wipe_anime = value != 0;
            }));
  AddOpcode(2001, 0, "SkipAnimations",
            new ConfigIntGetter([](RLMachine& machine) {
              return machine.GetGlobalConfig().system.disable_wipe_anime;
            }));
  AddOpcode(
      2052, 0, "SetLowPriority",
      std::make_shared<ConfigIntSetter>([](RLMachine& machine, int value) {
        machine.GetGlobalConfig().system.low_priority = value;
      }));
  AddOpcode(2002, 0, "LowPriority",
            std::make_shared<ConfigIntGetter>([](RLMachine& machine) {
              return machine.GetGlobalConfig().system.low_priority;
            }));

  AddOpcode(2223, 0, "SetMessageSpeed",
            new ConfigIntSetter([](RLMachine& machine, int value) {
              machine.GetGlobalConfig().message_speed = value;
            }));
  AddOpcode(2323, 0, "MessageSpeed",
            new ConfigIntGetter([](RLMachine& machine) {
              return machine.GetGlobalConfig().message_speed;
            }));
  AddOpcode(2600, 0, "DefaultMessageSpeed",
            new ReturnGameexeInt("INIT_MESSAGE_SPEED", 0));

  AddOpcode(2224, 0, "SetMessageNoWait",
            new ConfigIntSetter([](RLMachine& machine, int value) {
              machine.GetGlobalConfig().message_speed_nowait = value != 0;
            }));
  AddOpcode(2324, 0, "MessageNoWait",
            new ConfigIntGetter([](RLMachine& machine) {
              return machine.GetGlobalConfig().message_speed_nowait;
            }));
  AddOpcode(2601, 0, "DefMessageNoWait",
            new ReturnGameexeInt("INIT_MESSAGE_SPEED_MOD", 0));

  AddOpcode(2250, 0, "SetAutoMode",
            new ConfigIntSetter([](RLMachine& machine, int value) {
              machine.GetGlobalConfig().enable_auto_mode = value != 0;
            }));
  AddOpcode(2350, 0, "AutoMode", new ConfigIntGetter([](RLMachine& machine) {
              return machine.GetGlobalConfig().enable_auto_mode;
            }));
  AddOpcode(2604, 0, "DefAutoMode",
            new ReturnGameexeInt("MESSAGE_KEY_WAIT_USE", 0));

  AddOpcode(2251, 0, "SetAutoCharTime",
            new ConfigIntSetter([](RLMachine& machine, int value) {
              machine.GetGlobalConfig().auto_mode_moji_wait = value;
            }));
  AddOpcode(2351, 0, "AutoCharTime",
            new ConfigIntGetter([](RLMachine& machine) {
              return machine.GetGlobalConfig().auto_mode_moji_wait;
            }));
  AddOpcode(2605, 0, "DefAutoCharTime",
            new ReturnGameexeInt("INIT_MESSAGE_SPEED", 0));

  AddOpcode(2252, 0, "SetAutoBaseTime",
            new ConfigIntSetter([](RLMachine& machine, int value) {
              machine.GetGlobalConfig().auto_mode_min_wait = value;
            }));
  AddOpcode(2352, 0, "AutoBaseTime",
            new ConfigIntGetter([](RLMachine& machine) {
              return machine.GetGlobalConfig().auto_mode_min_wait;
            }));
  AddOpcode(2606, 0, "DefAutoBaseTime",
            new ReturnGameexeInt("MESSAGE_KEY_WAIT_TIME", 0));

  AddOpcode(2325, 0, "KoeMode",
            new GetGlobalConfigSetting([](const GlobalConfig& settings) {
              return settings.koe_mode;
            }));
  AddOpcode(
      2225, 0, "SetKoeMode",
      new ChangeGlobalConfigSetting([](GlobalConfig& settings, int value) {
        settings.koe_mode = value;
      }));
  AddOpcode(2326, 0, "BgmKoeFadeVol",
            new GetGlobalConfigSetting([](const GlobalConfig& settings) {
              return settings.bgmfade_volume;
            }));
  AddOpcode(
      2226, 0, "SetBgmKoeFadeVol",
      new ChangeGlobalConfigSetting([](GlobalConfig& settings, int value) {
        settings.bgmfade_volume = value;
      }));
  AddUnsupportedOpcode(2602, 0, "DefBgmKoeFadeVol");
  AddOpcode(2327, 0, "BgmKoeFade",
            new GetGlobalConfigSetting([](const GlobalConfig& settings) {
              return settings.bgmfade_use_check;
            }));
  AddOpcode(
      2227, 0, "SetBgmKoeFade",
      new ChangeGlobalConfigSetting([](GlobalConfig& settings, int value) {
        settings.bgmfade_use_check = value;
      }));
  AddUnsupportedOpcode(2603, 0, "DefBgmKoeFade");
  AddOpcode(2330, 0, "BgmVolMod",
            new GetGlobalConfigSetting([](const GlobalConfig& settings) {
              return settings.sound_user_volume[SoundIndex(SoundCategory::Bgm)];
            }));
  AddOpcode(
      2230, 0, "SetBgmVolMod",
      new ChangeGlobalConfigSetting([](GlobalConfig& settings, int value) {
        settings.sound_user_volume[SoundIndex(SoundCategory::Bgm)] = value;
      }));

  AddOpcode(
      2331, 0, "KoeVolMod",
      new GetGlobalConfigSetting([](const GlobalConfig& settings) {
        return settings.sound_user_volume[SoundIndex(SoundCategory::Voice)];
      }));
  AddOpcode(
      2231, 0, "SetKoeVolMod",
      new ChangeGlobalConfigSetting([](GlobalConfig& settings, int value) {
        settings.sound_user_volume[SoundIndex(SoundCategory::Voice)] = value;
      }));
  AddOpcode(2332, 0, "PcmVolMod",
            new GetGlobalConfigSetting([](const GlobalConfig& settings) {
              return settings.sound_user_volume[SoundIndex(SoundCategory::Pcm)];
            }));
  AddOpcode(
      2232, 0, "SetPcmVolMod",
      new ChangeGlobalConfigSetting([](GlobalConfig& settings, int value) {
        settings.sound_user_volume[SoundIndex(SoundCategory::Pcm)] = value;
      }));

  AddOpcode(2333, 0, "SeVolMod",
            new GetGlobalConfigSetting([](const GlobalConfig& settings) {
              return settings.sound_user_volume[SoundIndex(SoundCategory::Se)];
            }));
  AddOpcode(
      2233, 0, "SetSeVolMod",
      new ChangeGlobalConfigSetting([](GlobalConfig& settings, int value) {
        settings.sound_user_volume[SoundIndex(SoundCategory::Se)] = value;
      }));
  AddOpcode(2340, 0, "BgmEnabled",
            new GetGlobalConfigSetting([](const GlobalConfig& settings) {
              return settings.play_sound_check[SoundIndex(SoundCategory::Bgm)];
            }));
  AddOpcode(
      2240, 0, "SetBgmEnabled",
      new ChangeGlobalConfigSetting([](GlobalConfig& settings, int value) {
        settings.play_sound_check[SoundIndex(SoundCategory::Bgm)] = value;
      }));
  AddOpcode(
      2341, 0, "KoeEnabled",
      new GetGlobalConfigSetting([](const GlobalConfig& settings) {
        return settings.play_sound_check[SoundIndex(SoundCategory::Voice)];
      }));
  AddOpcode(
      2241, 0, "SetKoeEnabled",
      new ChangeGlobalConfigSetting([](GlobalConfig& settings, int value) {
        settings.play_sound_check[SoundIndex(SoundCategory::Voice)] = value;
      }));

  AddOpcode(2342, 0, "PcmEnabled",
            new GetGlobalConfigSetting([](const GlobalConfig& settings) {
              return settings.play_sound_check[SoundIndex(SoundCategory::Pcm)];
            }));
  AddOpcode(
      2242, 0, "SetPcmEnabled",
      new ChangeGlobalConfigSetting([](GlobalConfig& settings, int value) {
        settings.play_sound_check[SoundIndex(SoundCategory::Pcm)] = value;
      }));
  AddOpcode(2343, 0, "SeEnabled",
            new GetGlobalConfigSetting([](const GlobalConfig& settings) {
              return settings.play_sound_check[SoundIndex(SoundCategory::Se)];
            }));
  AddOpcode(
      2243, 0, "SetSeEnabled",
      new ChangeGlobalConfigSetting([](GlobalConfig& settings, int value) {
        settings.play_sound_check[SoundIndex(SoundCategory::Se)] = value;
      }));

  AddOpcode(2256, 0, "SetFontWeight",
            new ConfigIntSetter([](RLMachine& machine, int value) {
              machine.GetGlobalConfig().font_futoku = value != 0;
            }));
  AddOpcode(2356, 0, "FontWeight", new ConfigIntGetter([](RLMachine& machine) {
              return machine.GetGlobalConfig().font_futoku;
            }));
  AddOpcode(2257, 0, "SetFontShadow",
            new ConfigIntSetter([](RLMachine& machine, int value) {
              machine.GetGlobalConfig().font_shadow = value & 3;
            }));
  AddOpcode(2357, 0, "FontShadow", new ConfigIntGetter([](RLMachine& machine) {
              return machine.GetGlobalConfig().font_shadow;
            }));

  AddUnsupportedOpcode(2054, 0, "SetReduceDistortion");
  AddUnsupportedOpcode(2004, 0, "ReduceDistortion");
  AddUnsupportedOpcode(2059, 0, "SetSoundQuality");
  AddUnsupportedOpcode(2009, 0, "SoundQuality");

  AddOpcode(2221, 0, "SetGeneric1", new SetGeneric1);
  AddOpcode(2620, 0, "DefGeneric1",
            new ReturnGameexeInt("INIT_ORIGINALSETING1_MOD", 0));
  AddOpcode(2321, 0, "Generic1", new GetGeneric1);
  AddOpcode(2222, 0, "SetGeneric2", new SetGeneric2);
  AddOpcode(2621, 0, "DefGeneric2",
            new ReturnGameexeInt("INIT_ORIGINALSETING2_MOD", 0));
  AddOpcode(2322, 0, "Generic2", new GetGeneric2);

  AddOpcode(2260, 0, "SetWindowAttrR",
            new ConfigIntSetter([](RLMachine& machine, int value) {
              machine.GetGlobalConfig().filter_color.set_red(value);
              machine.GetSystem().text().ApplyConfiguration();
            }));
  AddOpcode(2261, 0, "SetWindowAttrG",
            new ConfigIntSetter([](RLMachine& machine, int value) {
              machine.GetGlobalConfig().filter_color.set_green(value);
              machine.GetSystem().text().ApplyConfiguration();
            }));
  AddOpcode(2262, 0, "SetWindowAttrB",
            new ConfigIntSetter([](RLMachine& machine, int value) {
              machine.GetGlobalConfig().filter_color.set_blue(value);
              machine.GetSystem().text().ApplyConfiguration();
            }));
  AddOpcode(2263, 0, "SetWindowAttrA",
            new ConfigIntSetter([](RLMachine& machine, int value) {
              machine.GetGlobalConfig().filter_color.set_alpha(value);
              machine.GetSystem().text().ApplyConfiguration();
            }));
  AddOpcode(2264, 0, "SetWindowAttrF",
            new ConfigIntSetter([](RLMachine& machine, int value) {
              machine.GetGlobalConfig().reallive.window_filter = value;
              machine.GetSystem().text().ApplyConfiguration();
            }));

  AddOpcode(2267, 0, "SetWindowAttr", new SetWindowAttr);

  AddUnsupportedOpcode(2273, 0, "SetClassifyText");
  AddUnsupportedOpcode(2373, 0, "ClassifyText");
  AddOpcode(2274, 0, "SetUseKoe",
            new ConfigIntIntSetter(
                [](RLMachine& machine, int character, int enabled) {
                  machine.GetSystem().sound().SetUseKoeForCharacter(character,
                                                                    enabled);
                }));
  // Note: I don't understand how this overload differs, but CLANNAD_FV treats
  // it just like the previous one.
  AddOpcode(2274, 1, "SetUseKoe",
            new ConfigIntIntSetter(
                [](RLMachine& machine, int character, int enabled) {
                  machine.GetSystem().sound().SetUseKoeForCharacter(character,
                                                                    enabled);
                }));
  AddOpcode(
      2374, 0, "UseKoe",
      new ConfigIntArgumentGetter([](RLMachine& machine, int character) {
        return machine.GetSystem().sound().ShouldUseKoeForCharacter(character);
      }));
  AddOpcode(2275, 0, "SetScreenMode",
            new ConfigIntSetter([](RLMachine& machine, int value) {
              machine.GetGlobalConfig().screen_size_mode = value == 0 ? 1 : 0;
              machine.GetSystem().graphics().ApplyConfiguration();
            }));
  AddOpcode(2375, 0, "ScreenMode", new ConfigIntGetter([](RLMachine& machine) {
              return machine.GetGlobalConfig().screen_size_mode == 1 ? 0 : 1;
            }));

  AddOpcode(2360, 0, "WindowAttrR", new ConfigIntGetter([](RLMachine& machine) {
              return machine.GetGlobalConfig().filter_color.r();
            }));
  AddOpcode(2361, 0, "WindowAttrG", new ConfigIntGetter([](RLMachine& machine) {
              return machine.GetGlobalConfig().filter_color.g();
            }));
  AddOpcode(2362, 0, "WindowAttrB", new ConfigIntGetter([](RLMachine& machine) {
              return machine.GetGlobalConfig().filter_color.b();
            }));
  AddOpcode(2363, 0, "WindowAttrA", new ConfigIntGetter([](RLMachine& machine) {
              return machine.GetGlobalConfig().filter_color.a();
            }));
  AddOpcode(2364, 0, "WindowAttrF", new ConfigIntGetter([](RLMachine& machine) {
              return machine.GetGlobalConfig().reallive.window_filter;
            }));

  AddOpcode(2367, 0, "GetWindowAttr", new GetWindowAttr);

  AddOpcode(2610, 0, "DefWindowAttrR", new ReturnGameexeInt("WINDOW_ATTR", 0));
  AddOpcode(2611, 0, "DefWindowAttrG", new ReturnGameexeInt("WINDOW_ATTR", 1));
  AddOpcode(2612, 0, "DefWindowAttrB", new ReturnGameexeInt("WINDOW_ATTR", 2));
  AddOpcode(2613, 0, "DefWindowAttrA", new ReturnGameexeInt("WINDOW_ATTR", 3));
  AddOpcode(2614, 0, "DefWindowAttrF", new ReturnGameexeInt("WINDOW_ATTR", 4));

  AddOpcode(2617, 0, "DefWindowAttr", new DefWindowAttr);

  AddOpcode(2270, 0, "SetShowObject1",
            new ConfigIntSetter([](RLMachine& machine, int value) {
              machine.GetGlobalConfig().object_disp_flag.at(0) = value;
            }));
  AddOpcode(2370, 0, "ShowObject1", new ConfigIntGetter([](RLMachine& machine) {
              return machine.GetGlobalConfig().object_disp_flag.at(0);
            }));
  AddOpcode(2271, 0, "SetShowObject2",
            new ConfigIntSetter([](RLMachine& machine, int value) {
              machine.GetGlobalConfig().object_disp_flag.at(1) = value;
            }));
  AddOpcode(2371, 0, "ShowObject2", new ConfigIntGetter([](RLMachine& machine) {
              return machine.GetGlobalConfig().object_disp_flag.at(1);
            }));
  AddOpcode(2272, 0, "SetShowWeather",
            new ConfigIntSetter([](RLMachine& machine, int value) {
              machine.GetGlobalConfig().object_disp_flag.at(2) = value;
            }));
  AddOpcode(2372, 0, "ShowWeather", new ConfigIntGetter([](RLMachine& machine) {
              return machine.GetGlobalConfig().object_disp_flag.at(2);
            }));

  // Sys is hueg liek xbox, so lets group some of the operations by
  // what they do.
  AddWaitAndMouseOpcodes(*this);
  AddSysTimerOpcodes(*this);
  AddSysFrameOpcodes(*this);
  AddSysSaveOpcodes(*this);
  AddSysSyscomOpcodes(*this);
  AddSysDateOpcodes(*this);
  AddSysNameOpcodes(*this);
  AddIndexSeriesOpcode(*this);
  AddTimetable2Opcode(*this);
}
