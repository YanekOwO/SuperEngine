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

#include "modules/module_msg.hpp"

#include <memory>
#include <string>
#include <vector>

#include "core/gameexe.hpp"
#include "long_operations/pause_long_operation.hpp"
#include "machine/general_operations.hpp"
#include "machine/rlmachine.hpp"
#include "machine/rlmodule.hpp"
#include "machine/rloperation.hpp"
#include "machine/rloperation/default_value_t.hpp"
#include "modules/config_operations.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"
#include "utilities/string_utilities.hpp"

namespace {

struct par : public RLOpcode<> {
  void operator()(RLMachine& machine) {
    TextWindow& page = *machine.GetSystem().text().GetCurrentMwnd();
    page.Submit(msgcmd::ClearIndentation{});
    page.Submit(msgcmd::HardBreak{});
  }
};

struct Msg_pause : public RLOpcode<> {
  void operator()(RLMachine& machine) {
    TextSystem& text = machine.GetSystem().text();
    int windowNum = text.active_window();
    std::shared_ptr<TextWindow> textWindow = text.GetMwnd(windowNum);

    if (textWindow->action_on_pause()) {
      machine.PushLongOperation(std::make_shared<lop::NewParagraphAfterLongop>(
          machine, new lop::PauseLongOperation(machine)));
    } else {
      machine.PushLongOperation(std::make_shared<lop::NewPageOnAllAfterLongop>(
          machine, new lop::PauseLongOperation(machine)));
    }
  }
};

struct Msg_TextWindow : public RLOpcode<DefaultIntValue_T<0>> {
  void operator()(RLMachine& machine, int window) {
    machine.GetSystem().text().set_active_window(window);
  }
};

struct FontColour
    : public RLOpcode<DefaultIntValue_T<0>, DefaultIntValue_T<0>> {
  void operator()(RLMachine& machine, int textColorNum, int shadowColorNum) {
    Gameexe& gexe = machine.GetSystem().gameexe();
    machine.GetSystem().text().GetCurrentMwnd()->Submit(msgcmd::SetFontColor{
        RGBColour(gexe("COLOR_TABLE", textColorNum).ToIntVec())});
  }
};

struct SetFontColour : public RLOpcode<DefaultIntValue_T<0>> {
  void operator()(RLMachine& machine, int textColorNum) {
    Gameexe& gexe = machine.GetSystem().gameexe();
    if (gexe("COLOR_TABLE", textColorNum).Exists()) {
      machine.GetSystem().text().GetCurrentMwnd()->Submit(
          msgcmd::SetDefaultTextColor{
              RGBColour(gexe("COLOR_TABLE", textColorNum).ToIntVec())});
    }
  }
};

struct doruby_display : public RLOpcode<StrConstant_T> {
  void operator()(RLMachine& machine, std::string cpStr) {
    std::string utf8str = cp932toUTF8(cpStr, machine.GetTextEncoding());
    machine.GetSystem().text().GetCurrentMwnd()->Submit(
        msgcmd::RubyText{utf8str});
  }
};

struct msgHide : public RLOpcode<DefaultIntValue_T<0>> {
  void operator()(RLMachine& machine, int unknown) {
    TextSystem& text = machine.GetSystem().text();
    int winNum = text.active_window();
    text.HideTextWindow(winNum);
    text.NewPageOnWindow(winNum);
  }
};

struct msgHideAll : public RLOpcode<> {
  void operator()(RLMachine& machine) {
    TextSystem& text = machine.GetSystem().text();

    for (int window : text.GetActiveWindows()) {
      text.HideTextWindow(window);
      text.NewPageOnWindow(window);
    }
  }
};

struct msgClear : public RLOpcode<> {
  void operator()(RLMachine& machine) {
    TextSystem& text = machine.GetSystem().text();
    int active_window = text.active_window();
    text.Snapshot();
    text.GetMwnd(active_window)->ClearWin();
    text.NewPageOnWindow(active_window);
  }
};

struct msgClearAll : public RLOpcode<> {
  void operator()(RLMachine& machine) {
    TextSystem& text = machine.GetSystem().text();
    std::vector<int> active_windows = text.GetActiveWindows();
    int active_window = text.active_window();

    text.Snapshot();
    for (int window : active_windows) {
      // TODO(erg): Found this during refactoring? Just entirely wrong? Dates
      // all the way back to 2007 in 6938e517e8423e391eeba0fe4b294ad64434243d.
      text.GetMwnd(active_window)->ClearWin();
      text.NewPageOnWindow(window);
    }
  }
};

struct spause : public RLOpcode<> {
  void operator()(RLMachine& machine) {
    machine.PushLongOperation(
        std::make_shared<lop::PauseLongOperation>(machine));
  }
};

struct page : public RLOpcode<> {
  void operator()(RLMachine& machine) {
    machine.PushLongOperation(std::make_shared<lop::NewPageAfterLongop>(
        machine, new lop::PauseLongOperation(machine)));
  }
};

struct TextPos : public RLOpcode<IntConstant_T, IntConstant_T> {
  void operator()(RLMachine& machine, int x, int y) {
    TextWindow& page = *machine.GetSystem().text().GetCurrentMwnd();
    page.Submit(
        msgcmd::SetInsertion{Point(x, page.Layout().GetInsertionPoint().y())});
    page.Submit(msgcmd::SetInsertion{Point(x, y)});
  }
};

struct GetTextPos : public RLOpcode<IntReference_T, IntReference_T> {
  void operator()(RLMachine& machine,
                  IntReferenceIterator x,
                  IntReferenceIterator y) {
    std::shared_ptr<TextWindow> textWindow =
        machine.GetSystem().text().GetCurrentMwnd();

    if (textWindow) {
      const Point point = textWindow->Layout().GetInsertionPoint();
      *x = point.x();
      *y = point.y();
    }
  }
};

struct TextOffset : public RLOpcode<IntConstant_T, IntConstant_T> {
  void operator()(RLMachine& machine, int x, int y) {
    TextWindow& page = *machine.GetSystem().text().GetCurrentMwnd();
    Point point = page.Layout().GetInsertionPoint();
    point.set_x(point.x() + x);
    page.Submit(msgcmd::SetInsertion{point});
    point.set_y(point.y() + y);
    page.Submit(msgcmd::SetInsertion{point});
  }
};

struct FaceOpen : public RLOpcode<StrConstant_T, DefaultIntValue_T<0>> {
  void operator()(RLMachine& machine, std::string file, int index) {
    TextWindow& page = *machine.GetSystem().text().GetCurrentMwnd();
    page.Submit(msgcmd::FaceOpen{file, index});
  }
};

struct FaceClose : public RLOpcode<DefaultIntValue_T<0>> {
  void operator()(RLMachine& machine, int index) {
    TextWindow& page = *machine.GetSystem().text().GetCurrentMwnd();
    page.Submit(msgcmd::FaceClose{index});
  }
};

}  // namespace

MsgModule::MsgModule() : RLModule("Msg", 0, 003) {
  AddOpcode(3, 0, "par", new par);
  //  AddOpcode(15, 0, /* spause3 */ );
  AddOpcode(17, 0, "pause", new Msg_pause);

  AddUnsupportedOpcode(100, 0, "SetFontColour");
  AddOpcode(100, 1, "SetFontColour", new SetFontColour);
  AddOpcode(100, 2, "SetFontColour", new SetFontColour);
  AddOpcode(101, 0, "FontSize",
            new ConfigIntSetter([](RLMachine& machine, int size) {
              machine.GetSystem().text().GetCurrentMwnd()->Submit(
                  msgcmd::SetFontSize{size});
            }));
  AddOpcode(101, 1, "FontSize", new ConfigAction([](RLMachine& machine) {
              auto window = machine.GetSystem().text().GetCurrentMwnd();
              window->Submit(msgcmd::SetFontSize{window->GetDefaultFontSize()});
            }));

  AddOpcode(102, 0, "TextWindow", new Msg_TextWindow);
  AddOpcode(102, 1, "TextWindow", new Msg_TextWindow);

  AddOpcode(103, 0, "FastText", new ConfigAction([](RLMachine& machine) {
              machine.GetLocalConfig().reallive.fast_text_mode = true;
            }));
  AddOpcode(104, 0, "NormalText", new ConfigAction([](RLMachine& machine) {
              machine.GetLocalConfig().reallive.fast_text_mode = false;
            }));

  AddOpcode(105, 0, "FontColor", new FontColour);
  AddOpcode(105, 1, "FontColor", new FontColour);
  AddOpcode(105, 2, "FontColor", new FontColour);

  AddUnsupportedOpcode(106, 0, "SetFontColourAll");
  AddUnsupportedOpcode(106, 1, "SetFontColourAll");
  AddUnsupportedOpcode(106, 2, "SetFontColourAll");

  AddUnsupportedOpcode(107, 0, "FontSizeAll");

  AddOpcode(109, 0, "message_no_waitOn",
            new ConfigAction([](RLMachine& machine) {
              machine.GetLocalConfig().msg_nowait = true;
            }));
  AddOpcode(110, 0, "message_no_waitOff",
            new ConfigAction([](RLMachine& machine) {
              machine.GetLocalConfig().msg_nowait = false;
            }));

  AddOpcode(111, 0, "activeWindow", ReturnIntValue(&TextSystem::active_window));

  AddOpcode(120, 0, "__doruby_on", new doruby_display);
  AddOpcode(120, 1, "__doruby_off", new ConfigAction([](RLMachine& machine) {
              machine.GetSystem().text().GetCurrentMwnd()->Submit(
                  msgcmd::RubyBegin{});
            }));

  AddOpcode(151, 0, "msgHide", new msgHide);
  AddOpcode(152, 0, "msgClear", new msgClear);

  AddOpcode(161, 0, "msgHideAll", new msgHideAll);
  AddOpcode(162, 0, "msgClearAll", new msgClearAll);
  AddUnsupportedOpcode(170, 0, "msgHideAllTemp");
  AddOpcode(201, 0, "br", new ConfigAction([](RLMachine& machine) {
              machine.GetSystem().text().GetCurrentMwnd()->Submit(
                  msgcmd::HardBreak{});
            }));
  AddOpcode(205, 0, "spause", new spause);
  AddUnsupportedOpcode(206, 0, "spause2");
  AddUnsupportedOpcode(207, 0, "pause_all");
  AddOpcode(210, 0, "page", new page);

  AddOpcode(300, 0, "SetIndent", new ConfigAction([](RLMachine& machine) {
              machine.GetSystem().text().GetCurrentMwnd()->Submit(
                  msgcmd::SetIndentation{});
            }));
  AddOpcode(301, 0, "ClearIndent", new ConfigAction([](RLMachine& machine) {
              machine.GetSystem().text().GetCurrentMwnd()->Submit(
                  msgcmd::ClearIndentation{});
            }));

  AddOpcode(310, 0, "TextPos", new TextPos);
  AddOpcode(311, 0, "TextPosX",
            new ConfigIntSetter([](RLMachine& machine, int x) {
              auto window = machine.GetSystem().text().GetCurrentMwnd();
              Point point = window->Layout().GetInsertionPoint();
              point.set_x(x);
              window->Submit(msgcmd::SetInsertion{point});
            }));
  AddOpcode(312, 0, "TextPosY",
            new ConfigIntSetter([](RLMachine& machine, int y) {
              auto window = machine.GetSystem().text().GetCurrentMwnd();
              Point point = window->Layout().GetInsertionPoint();
              point.set_y(y);
              window->Submit(msgcmd::SetInsertion{point});
            }));
  AddOpcode(320, 0, "TextOffset", new TextOffset);
  AddOpcode(321, 0, "TextOffsetX",
            new ConfigIntSetter([](RLMachine& machine, int offset) {
              auto window = machine.GetSystem().text().GetCurrentMwnd();
              Point point = window->Layout().GetInsertionPoint();
              point.set_x(point.x() + offset);
              window->Submit(msgcmd::SetInsertion{point});
            }));
  AddOpcode(322, 0, "TextOffsetY",
            new ConfigIntSetter([](RLMachine& machine, int offset) {
              auto window = machine.GetSystem().text().GetCurrentMwnd();
              Point point = window->Layout().GetInsertionPoint();
              point.set_y(point.y() + offset);
              window->Submit(msgcmd::SetInsertion{point});
            }));
  AddOpcode(330, 0, "GetTextPos", new GetTextPos);

  AddUnsupportedOpcode(340, 0, "WindowLen");
  AddUnsupportedOpcode(340, 1, "WindowLen");
  AddUnsupportedOpcode(341, 0, "WindowLenAll");

  AddOpcode(1000, 0, "FaceOpen", new FaceOpen);
  AddOpcode(1000, 1, "FaceOpen", new FaceOpen);
  AddOpcode(1001, 0, "FaceClose", new FaceClose);
  AddOpcode(1001, 1, "FaceClose", new FaceClose);
}
