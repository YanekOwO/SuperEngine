// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 RLVM contributors
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
// -----------------------------------------------------------------------

#include "libsiglus/bindings/selection.hpp"
#include <memory>
#include <utility>
#include "core/event_listener.hpp"
#include "core/gameexe.hpp"
#include "core/input.hpp"
#include "core/kidoku_table.hpp"
#include "core/selection.hpp"
#include "core/stage.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "long_operations/wait_for_selection.hpp"
#include "srbind/srbind.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/sound_system.hpp"
#include "systems/text_page.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"
#include "vm/exception.hpp"
#include "vm/future.hpp"
#include "vm/list.hpp"
#include "vm/vm.hpp"

namespace libsiglus::binding {
namespace sr = serilang;
namespace sb = srbind;

SelectionRequest DecodeSelectionRequest(std::vector<sr::Value> raw,
                                        bool buttons,
                                        bool cancel,
                                        bool context) {
  SelectionRequest r;
  r.kind = buttons ? SelectionRequest::Kind::Buttons
                   : SelectionRequest::Kind::Window;
  r.cancel_enabled = cancel;

  // Strip the compiler's hidden read-history arguments before decoding the
  // usual overload/args/kwargs packet. Ready calls have no such prefix.
  if (context) {
    if (raw.size() < 2)
      throw sr::RuntimeError("Selection is missing scene/read-flag context");
    r.scene = RequireInt(raw[0], "selection scene");
    r.kidoku = RequireInt(raw[1], "selection read flag");
    raw.erase(raw.begin(), raw.begin() + 2);
  }

  // Siglus option groups can be nested lists; flatten them without changing
  // script order so disabled/hidden entries keep their returned indices.
  auto packet = CallPacket::DecodeFrom(std::move(raw));
  std::vector<sr::Value> args;
  auto flatten = [&](auto&& flatten,
                     const std::vector<sr::Value>& values) -> void {
    for (const auto& v : values) {
      if (const auto* list = v.Get_if<sr::List>())
        flatten(flatten, list->items);
      else
        args.push_back(v);
    }
  };
  flatten(flatten, packet.args);

  std::size_t first = 0;
  if (buttons && !args.empty() && args[0].Get_if<int>()) {
    r.template_no = RequireInt(args[0], "selection template");
    first = 1;
  }

  // A string begins a new option; up to two integers modify that option's
  // type and color. Defaults apply when the next string arrives first.
  int field = 0;
  for (std::size_t i = first; i < args.size(); ++i) {
    if (const auto* str = args[i].Get_if<sr::String>()) {
      r.options.push_back(SelectionOption{.text = str->str_});
      field = 0;
    } else {
      const int value = RequireInt(args[i], "selection item parameter");
      if (r.options.empty() || field >= 2)
        throw sr::RuntimeError(
            "Selection item parameters must follow a string");
      if (field++ == 0) {
        if (value < 0 || value > 3)
          throw sr::RuntimeError("Invalid selection item type");
        r.options.back().type = value;
      } else
        r.options.back().color = value;
    }
  }

  ForEachKeywordId(packet.kwargs, [&](int id, const sr::Value& value) {
    if (!buttons)
      throw sr::RuntimeError(
          "Window selections do not accept tagged arguments");
    switch (id) {
      case 1:
        r.capture = RequireInt(value, "selection capture") != 0;
        break;
      case 2:
        if (!value.Get_if<sr::String>())
          throw sr::RuntimeError("Selection start scene must be a string");
        r.start_scene = AsString(value);
        break;
      case 3:
        r.start_entry = RequireInt(value, "selection start entry");
        break;
      case 4:
        r.sync_type = RequireInt(value, "selection sync type");
        break;
      default:
        throw sr::RuntimeError("Unknown selection argument tag: " +
                               std::to_string(id));
    }
  });

  if (r.sync_type < 0 || r.sync_type > 2)
    throw sr::RuntimeError("Invalid selection sync type");
  return r;
}

std::shared_ptr<Selection> Build(SiglusMachine& machine,
                                 SelectionRequest request,
                                 std::shared_ptr<TextWindow> window) {
  const bool buttons = request.kind == SelectionRequest::Kind::Buttons;
  auto config =
      buttons && machine.gameexe
          ? BtnSelTemplate::ParseSiglus(*machine.gameexe, request.template_no)
          : BtnSelTemplate{};
  if (!machine.system) {
    auto s = std::make_shared<Selection>(std::move(request), config);
    s->window = std::move(window);
    s->LayoutButtons(Size(100, config.font_size));
    return s;
  }
  if (!buttons && !window) {
    TextSystem& text = machine.system->text();
    window = request.close_window
                 ? text.GetMwnd(text.mwnd_config().default_selection_window())
                 : text.GetCurrentMwnd();
  }
  auto s = BuildSelection(*machine.system, std::move(request), config,
                          std::move(window));
  if (buttons)
    s->sorter = {machine.stage ? machine.stage->mwnd_order : 1, 0};
  return s;
}

// ------------------------------------------------------------------------------

struct SelEventListener : public EventListener {
  SiglusMachine& machine;
  SelEventListener(SiglusMachine& m) : machine(m) {}

  void OnEvent(std::shared_ptr<Event> event) override {
    if (!event || !machine.selection_state->active_ ||
        !machine.selection_state->active_->Visible())
      return;

    auto state = machine.selection_state;
    auto s = state->active_;
    const auto feedback = state->HandleEvent(event);
    const auto sound = [&](int number) {
      if (machine.system && machine.system->sound().HasSe(number))
        machine.system->sound().PlaySe(number);
    };
    if (feedback.focus_changed)
      sound(0);
    if (feedback.choice)
      machine.OnSelectionChosen(*s);
    if (machine.system)
      machine.system->graphics().ForceRefresh();
  }
};

void BindSelection(SiglusMachine& rt) {
  auto state = std::make_shared<SelState>();
  rt.selection_state = state;
  std::shared_ptr<EventListener> listener =
      std::make_shared<SelEventListener>(rt);
  rt.selection_event_listener = listener;
  if (rt.system)
    rt.system->event().AddListener(20, listener);

  sb::module_ m(rt.vm->gc_.get(), rt.vm->globals_.get());
  const auto bind = [&](const char* name, bool buttons, bool cancel,
                        bool close) {
    m.def(
        name,
        [state, &rt, buttons, cancel, close](std::vector<sr::Value> raw) {
          SelectionRequest request =
              DecodeSelectionRequest(std::move(raw), buttons, cancel, true);
          request.close_window = close;
          if (state->active_ && !state->active_->CanResume())
            throw sr::RuntimeError("An active selection is already waiting");

          std::shared_ptr<Selection> sel =
              Build(rt, std::move(request), /*mwnd=*/nullptr);

          rt.StartSelection(sel);
          auto wait = std::make_unique<lop::WaitForSelection>(
              state, sel,
              [&rt](std::shared_ptr<Selection> sel) {
                rt.FinishSelection(sel);
              },
              [&rt](std::shared_ptr<Selection> sel) {
                rt.CancelSelection(sel);
              });
          sr::Future* fut = state->pending_->MakeFuture(
              *rt.vm, std::move(wait), rt.system->event_ptr().get());
          return sr::Value(fut);
        },
        sb::vararg);
  };
  bind("sel", false, false, true);
  bind("sel_cancel", false, true, true);
  bind("selmsg", false, false, false);
  bind("selmsg_cancel", false, true, false);
  bind("selbtn", true, false, false);
  bind("selbtn_cancel", true, true, false);
  for (bool cancel : {false, true}) {
    m.def(
        cancel ? "selbtn_cancel_ready" : "selbtn_ready",
        [state, &rt, cancel](std::vector<sr::Value> raw) -> void {
          if (state->active_ && !state->active_->CanResume())
            throw sr::RuntimeError("A selection is already waiting");

          SelectionRequest request =
              DecodeSelectionRequest(std::move(raw), true, cancel, false);

          // ignore capture and start-call tags, scene and kidoku will be set
          // later
          request.capture = false;
          request.start_scene.clear();
          std::ignore = request.scene, std::ignore = request.kidoku;

          std::shared_ptr<Selection> sel =
              Build(rt, std::move(request), /*mwnd=*/nullptr);
          state->prepared_ = sel;
          rt.stage->selection[kLayerFg] = sel;
          rt.system->graphics().ForceRefresh();
        },
        sb::vararg);
  }
  m.def(
      "selbtn_start",
      [&rt, state](std::vector<sr::Value> raw) {
        // Consumes the prepared menu, attaches selbtn_start's read key, and
        // forces sync_type=0. Returns a future for the original option index or
        // -1;
        SelectionRequest request =
            DecodeSelectionRequest(std::move(raw), true, false, true);
        if (!request.options.empty())
          throw sr::RuntimeError("selbtn_start accepts no options");

        std::shared_ptr<Selection> sel =
            std::exchange(state->prepared_, nullptr);
        if (!sel)
          throw sr::RuntimeError("selbtn_start requires selbtn_ready");

        sel->request.scene = request.scene;
        sel->request.kidoku = request.kidoku;

        // always waits through the full close animation?
        sel->request.sync_type = 0;

        rt.StartSelection(sel);
        auto wait = std::make_unique<lop::WaitForSelection>(
            state, sel,
            [&rt](std::shared_ptr<Selection> sel) { rt.FinishSelection(sel); },
            [&rt](std::shared_ptr<Selection> sel) { rt.CancelSelection(sel); });
        sr::Future* fut = state->pending_->MakeFuture(
            *rt.vm, std::move(wait), rt.system->event_ptr().get());
        return sr::Value(fut);
      },
      sb::vararg);
  m.def("get_last_sel_msg", [state] { return state->last_message_; });

  m.def("__debug_register_sel_choice",
        [&rt](int scene, int kidoku, int choice) {
          rt.RegisterDebugSelectionChoice(scene, kidoku, choice);
        });
}

RLVM_REGISTER(SiglusBindingRegistry, "0_selection", BindSelection)

}  // namespace libsiglus::binding
