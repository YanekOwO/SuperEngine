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

#include "modules/module_sel.hpp"

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/gameexe.hpp"
#include "core/object.hpp"
#include "core/rlevent_listener.hpp"
#include "core/selection.hpp"
#include "core/stage.hpp"
#include "libreallive/parser.hpp"
#include "long_operations/button_object_select_long_operation.hpp"
#include "long_operations/wait_for_selection.hpp"
#include "machine/rlmachine.hpp"
#include "machine/rloperation.hpp"
#include "machine/rloperation/rlop_store.hpp"
#include "systems/event_system.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"
#include "utilities/string_utilities.hpp"

using libreallive::CommandElement;
using libreallive::SelectElement;

namespace {

// TODO: All MarkSavepoint() function calls in this file won't work correctly as
// the IP has already move past the select instruction.
// For now, temporarily hack this, but fix it properly later.
inline void MarkSavepoint(RLMachine& machine) {
  machine.RevertIP();
  machine.MarkSavepoint();
  machine.AdvanceIP();
}

SelectionRequest DecodeSelection(RLMachine& machine,
                                 const SelectElement& element,
                                 SelectionRequest::Kind kind) {
  SelectionRequest request;
  request.kind = kind;
  request.close_window = false;
  const auto evaluate = [&](const std::string& expression) {
    const char* location = expression.c_str();
    return libreallive::ExpressionParser::GetExpression(location)
        ->GetIntegerValue(machine);
  };
  for (const auto& param : element.raw_params()) {
    SelectionOption option;
    option.text = cp932toUTF8(libreallive::EvaluatePRINT(machine, param.text),
                              machine.GetTextEncoding());
    bool shown = true, enabled = true;
    for (const auto& condition : param.cond_parsed) {
      switch (condition.effect) {
        case SelectElement::OPTION_HIDE:
          shown =
              !condition.condition.empty() && !evaluate(condition.condition);
          break;
        case SelectElement::OPTION_TITLE:
          enabled =
              !condition.condition.empty() && !evaluate(condition.condition);
          option.color = !enabled && !condition.effect_argument.empty()
                             ? evaluate(condition.effect_argument)
                             : -1;
          break;
        default:
          std::cerr << "Unsupported option in select statement (condition: "
                    << libreallive::ParsableToPrintableString(
                           condition.condition)
                    << ", effect: " << condition.effect << ", effect_argument: "
                    << libreallive::ParsableToPrintableString(
                           condition.effect_argument)
                    << ")" << std::endl;
          break;
      }
    }
    option.type = !shown ? 0 : enabled ? 1 : 2;
    request.options.push_back(std::move(option));
  }
  return request;
}

void StartSelection(RLMachine& machine,
                    const CommandElement& command,
                    SelectionRequest::Kind kind,
                    bool separate_window = false) {
  if (machine.GetScenarioConfig().enable_selcom_savepoint)
    MarkSavepoint(machine);
  const auto& element = dynamic_cast<const SelectElement&>(command);
  auto request = DecodeSelection(machine, element, kind);
  auto config = BtnSelTemplate::ParseReallive(machine.GetGameexe(), 0);
  TextSystem& text = machine.GetSystem().text();
  std::optional<int> restore_window;
  std::shared_ptr<TextWindow> window;
  if (separate_window) {
    const int computed =
        element.GetWindowExpression()->GetIntegerValue(machine);
    const int number = computed == -1
                           ? text.mwnd_config().default_selection_window()
                           : computed;
    window = text.GetMwnd(number);
    restore_window = text.active_window();
    text.HideAllTextWindows();
    text.set_active_window(number);
  } else if (kind == SelectionRequest::Kind::Window) {
    window = text.GetCurrentMwnd();
  }

  std::shared_ptr<Selection> s;
  try {
    s = BuildSelection(machine.GetSystem(), std::move(request), config,
                       machine.image_resources(),
                       machine.presenter().screen_size(), window);
  } catch (...) {
    if (restore_window) {
      text.HideAllTextWindows();
      text.set_active_window(*restore_window);
    }
    throw;
  }
  auto state = std::make_shared<SelState>();
  state->active_ = s;
  state->mouse_ = machine.GetSystem().rlEvent().GetCursorPos();
  // A custom system menu may start another selection on top of this one.
  auto previous_selection = machine.stage().selection[kLayerFg];
  machine.stage().selection[kLayerFg] = s;
  for (const auto& w : machine.stage().mwnd[kLayerFg])
    if (w)
      w->ClearMouseCapture();
  text.set_in_selection_mode(true);
  s->Start();
  s->open_hook_done = true;
  for (std::size_t i = 0; i < s->items.size(); ++i)
    if (s->request.options[i].type == 1) {
      s->focused = static_cast<int>(i);
      break;
    }

  const auto cleanup = [&machine, state, previous_selection,
                        restore_window](std::shared_ptr<Selection> selection) {
    // Revoke input even when a child selection currently owns the stage. This
    // prevents stack teardown from restoring an already-abandoned parent.
    selection->open_hook_done = false;
    state->active_.reset();
    if (machine.stage().selection[kLayerFg] != selection)
      return;
    TextSystem& text = machine.GetSystem().text();
    if (selection->window) {
      selection->window->SetSelectionSize(std::nullopt);
      selection->window->ClearWin();
    }
    auto& foreground = machine.stage().selection[kLayerFg];
    foreground = previous_selection && previous_selection->CanInput()
                     ? previous_selection
                     : nullptr;
    text.set_in_selection_mode(static_cast<bool>(foreground));
    if (restore_window) {
      text.HideAllTextWindows();
      text.set_active_window(*restore_window);
    }
    machine.ForceRefresh();
  };
  const auto input = [&machine, state, s](std::shared_ptr<Event> event) {
    if (!event || !s->Visible())
      return;
    if (machine.stage().selection[kLayerFg] != s) {
      s->open_hook_done = false;
      state->active_.reset();
      return;
    }
    if (const auto* down = std::get_if<MouseDown>(event.get());
        down && down->button == MouseButton::RIGHT) {
      s->pressed.reset();
      state->held_keys_.clear();
      state->cancel_pressed_ = false;
      machine.GetSystem().ShowSyscomMenu(machine);
      *event = std::monostate();
      return;
    }
    const auto feedback = state->HandleEvent(event);
    auto& sound = machine.GetSystem().sound();
    if (feedback.focus_changed && sound.HasSe(0))
      sound.PlaySe(0);
    if (feedback.choice) {
      if (sound.HasSe(1))
        sound.PlaySe(1);
      machine.GetSystem().TakeSelectionSnapshot(machine);
    }
    machine.ForceRefresh();
  };
  machine.PushLongOperation(std::make_shared<lop::WaitForSelection>(
      state, s,
      [&machine, cleanup](std::shared_ptr<Selection> selection) {
        machine.set_store_register(*selection->result);
        cleanup(std::move(selection));
      },
      cleanup, input));
  machine.ForceRefresh();
}

struct Sel_select : public RLOp_SpecialCase {
  void operator()(RLMachine& machine, const CommandElement& ce) override {
    StartSelection(machine, ce, SelectionRequest::Kind::Window);
  }
};

struct Sel_select_s : public RLOp_SpecialCase {
  void operator()(RLMachine& machine, const CommandElement& ce) override {
    StartSelection(machine, ce, SelectionRequest::Kind::Buttons);
  }
};

struct Sel_select_w : public RLOp_SpecialCase {
  void operator()(RLMachine& machine, const CommandElement& ce) override {
    StartSelection(machine, ce, SelectionRequest::Kind::Window, true);
  }
};

struct Sel_select_objbtn : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int group) {
    if (machine.GetScenarioConfig().enable_selcom_savepoint)
      MarkSavepoint(machine);

    machine.PushLongOperation(
        std::make_shared<lop::ButtonObjectSelectLongOperation>(machine, group));
  }
};

struct Sel_select_objbtn_cancel_0 : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int group) {
    if (machine.GetScenarioConfig().enable_selcom_savepoint)
      MarkSavepoint(machine);

    auto obj =
        std::make_shared<lop::ButtonObjectSelectLongOperation>(machine, group);
    obj->set_cancelable();
    machine.PushLongOperation(obj);
  }
};

struct Sel_select_objbtn_cancel_1
    : public RLOpcode<IntConstant_T, IntConstant_T> {
  void operator()(RLMachine& machine, int group, int se) {
    if (machine.GetScenarioConfig().enable_selcom_savepoint)
      MarkSavepoint(machine);

    auto obj =
        std::make_shared<lop::ButtonObjectSelectLongOperation>(machine, group);
    obj->set_cancelable();
    machine.PushLongOperation(obj);
  }
};

struct Sel_select_objbtn_cancel_2 : public RLOpcode<> {
  void operator()(RLMachine& machine) {
    if (machine.GetScenarioConfig().enable_selcom_savepoint)
      MarkSavepoint(machine);

    auto& fg_objs = machine.stage().objects[kLayerFg];
    int group = 0;
    for (auto& obj : fg_objs)
      if (obj && obj->Param().IsButton()) {
        group = obj->Param().GetButtonGroup();
        break;
      }

    auto obj =
        std::make_shared<lop::ButtonObjectSelectLongOperation>(machine, group);
    obj->set_cancelable();
    machine.PushLongOperation(obj);
  }
};

// Our system doesn't need an explicit initialize.
struct objbtn_init_0 : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int ignored) {}
};

struct objbtn_init_1 : public RLOpcode<> {
  void operator()(RLMachine& machine) {}
};

struct btnobjnow_hit : public RLStoreOpcode<> {
  int operator()(RLMachine& machine) override { return -1; }
};

struct btnobjnow_push : public RLStoreOpcode<> {
  int operator()(RLMachine& machine) override { return -1; }
};

struct btnobjnow_decide : public RLStoreOpcode<> {
  int operator()(RLMachine& machine) override { return -2; }
};
}  // namespace

SelModule::SelModule() : RLModule("Sel", 0, 2) {
  AddOpcode(0, 0, "select_w", new Sel_select_w);
  AddOpcode(1, 0, "select", new Sel_select);
  AddOpcode(2, 0, "select_s2", new Sel_select_s);
  AddOpcode(3, 0, "select_s", new Sel_select_s);
  AddOpcode(4, 0, "select_objbtn", new Sel_select_objbtn);
  AddOpcode(14, 0, "select_objbtn_cancel", new Sel_select_objbtn_cancel_0);
  AddOpcode(14, 1, "select_objbtn_cancel", new Sel_select_objbtn_cancel_1);
  AddOpcode(14, 2, "select_objbtn_cancel", new Sel_select_objbtn_cancel_2);

  AddOpcode(20, 0, "objbtn_init", new objbtn_init_0);
  AddOpcode(20, 1, "objbtn_init", new objbtn_init_1);

  AddOpcode(30, 0, "select_btnobjnow_hit", new btnobjnow_hit);
  AddOpcode(31, 0, "select_btnobjnow_push", new btnobjnow_push);
  AddOpcode(32, 0, "select_btnobjnow_decide", new btnobjnow_decide);
}
