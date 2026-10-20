// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
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

#include "libsiglus/bindings/registry.hpp"

#include "core/game_config.hpp"
#include "core/object.hpp"
#include "core/rect.hpp"
#include "libsiglus/bindings/selection.hpp"
#include "libsiglus/bindings/util.hpp"
#include "long_operations/siglus_mwnd_wait.hpp"
#include "long_operations/siglus_wait.hpp"
#include "long_operations/wait_for_selection.hpp"
#include "srbind/srbind.hpp"
#include "systems/graphics_system.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"
#include "systems/text_factory.hpp"
#include "systems/text_page.hpp"
#include "systems/text_system.hpp"
#include "systems/text_waku.hpp"
#include "systems/text_window.hpp"
#include "vm/future.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <utf8.h>

#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace libsiglus::binding {
namespace sb = srbind;
namespace sr = serilang;

struct MwndObjectList {
  enum class Kind { Object, Button, Face };

  std::shared_ptr<TextWindow> window;
  std::function<sr::Value(std::shared_ptr<GraphicsObject>)> make_object;
  Kind kind = Kind::Object;

  std::string Name() const {
    if (kind == Kind::Face)
      return "mwnd.face";
    return kind == Kind::Button ? "mwnd.button" : "mwnd.object";
  }

  TextWaku& Resolve() const {
    if (!window || !window->GetTextboxWaku())
      throw sr::RuntimeError(Name() + " requires a message-window waku");
    return *window->GetTextboxWaku();
  }

  sr::Value Get(int index) const {
    auto& waku = Resolve();
    if (index < 0 || static_cast<std::size_t>(index) >= Size())
      throw sr::RuntimeError(Name() +
                             " index out of range: " + std::to_string(index));
    if (!make_object)
      throw sr::RuntimeError(Name() + " requires the Object binding");
    if (kind == Kind::Face)
      return make_object(waku.TouchFace(index));
    return make_object(kind == Kind::Button ? waku.TouchButton(index)
                                            : waku.TouchObject(index));
  }

  std::size_t Size() const {
    auto& waku = Resolve();
    if (kind == Kind::Face)
      return waku.faces_.size();
    return kind == Kind::Button ? waku.button_objects_.size()
                                : waku.objects_.size();
  }
};

struct KoeCallParams {
  int koe = 0;
  int character = -1;
  bool no_auto_mode = false;
  static KoeCallParams ParseFrom(std::vector<sr::Value> raw_args) {
    CallPacket packet = CallPacket::DecodeFrom(std::move(raw_args));
    KoeCallParams params;

    if (!packet.args.empty())
      params.koe = AsInt(packet.args[0]).value_or(0);
    if (packet.args.size() > 1)
      params.character = AsInt(packet.args[1]).value_or(-1);

    ForEachKeywordId(packet.kwargs, [&](int id, const sr::Value& value) {
      switch (id) {
        case 0:
          params.no_auto_mode = AsInt(value).value_or(0) != 0;
          break;
        default:
          break;
      }
    });

    return params;
  }
};

struct MwndHandle {
  std::shared_ptr<TextWindow> mwnd;
};

struct MwndArrayHandle {
  int layer = kLayerFg;
};

void BindMwnd(SiglusMachine& runtime) {
  sr::VM& vm = *runtime.vm;
  System* sys = runtime.system.get();
  if (!sys || !runtime.stage)
    throw std::runtime_error("Mwnd bindings require a system and stage");

  auto get = [sys](const MwndHandle* h) {
    return h->mwnd ? h->mwnd : sys->text().GetCurrentMwnd();
  };
  auto page = [sys, get](const MwndHandle* h) -> TextPage& {
    return sys->text().GetPageOnWindow(get(h));
  };
  auto context = std::make_shared<lop::PendingCoroutineTasks>();
  auto state = [get](const MwndHandle* h) { return get(h)->message_state(); };
  auto wait = [&runtime, get, context](
                  MwndHandle* h, sr::VM& vm,
                  lop::SiglusMwndWaitOperation::AfterWait after_wait) {
    auto window = get(h);
    auto message_state = window->message_state();
    auto task = std::make_unique<lop::SiglusMwndWaitOperation>(
        runtime, std::move(window), std::move(message_state), after_wait);
    return sr::Value(context->MakeFuture(
        vm, std::move(task),
        runtime.system ? runtime.system->event_ptr().get() : nullptr));
  };

  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  m.def("get_mwnd",
        [&text = sys->text()]() -> int { return text.active_window(); });

  sb::class_<MwndHandle> mwnd(m, "Mwnd");
  mwnd.def(sb::init([sys](int index) {
             if (index < 0 || !sys->text().mwnd_config().HasWindow(index))
               throw sr::RuntimeError("Invalid message-window number: " +
                                      std::to_string(index));
             return std::make_unique<MwndHandle>(sys->text().GetMwnd(index));
           }),
           sb::arg("index"));
  mwnd.inst("mwnd", nullptr);  // Top-level mwnd resolves the active window.

  const auto bind_selection = [&](const char* name, bool cancel, bool close) {
    mwnd.def(
        name,
        [&rt = runtime, get, cancel, close](MwndHandle* h,
                                            std::vector<sr::Value> raw) {
          if (!rt.selection_state)
            throw sr::RuntimeError("Selection runtime is unavailable");
          auto request =
              DecodeSelectionRequest(std::move(raw), false, cancel, true);
          request.close_window = close;
          if (rt.selection_state->active_ &&
              !rt.selection_state->active_->CanResume())
            throw sr::RuntimeError("An active selection is already waiting");

          std::shared_ptr<Selection> sel =
              Build(rt, std::move(request), get(h));
          rt.StartSelection(sel);
          std::shared_ptr<SelState> state = rt.selection_state;
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
  bind_selection("sel", false, true);
  bind_selection("sel_cancel", true, true);
  bind_selection("selmsg", false, false);
  bind_selection("selmsg_cancel", true, false);

  sb::class_<MwndArrayHandle> array(m, "MwndArray");
  array.def(sb::init([stage = runtime.stage.get()](int layer) {
              (void)stage->mwnd[layer];
              return std::make_unique<MwndArrayHandle>(layer);
            }),
            sb::arg("layer") = static_cast<int>(kLayerFg));
  array.add_gc_root(mwnd);
  array.def(
      "__getitem__",
      [sys, mwnd](MwndArrayHandle* array, int index) mutable {
        if (index < 0 || !sys->text().mwnd_config().HasWindow(index))
          throw sr::RuntimeError("Invalid message-window number: " +
                                 std::to_string(index));
        return sr::Value(
            mwnd.make_inst(sys->text().GetMwnd(array->layer, index)));
      },
      sb::arg("index"));
  auto close_all = [sys, stage = runtime.stage.get()](MwndArrayHandle*) {
    sys->text().set_in_pause_state(false);
    for (const auto& layer : stage->mwnd.storage) {
      for (const auto& window : layer)
        if (window) {
          window->SetVisible(false);
          window->ClearMouseCapture();
        }
    }
  };
  array.def("close", close_all);
  array.def("close_nowait", close_all);
  array.def("close_wait", close_all);
  array.def(
      "resize",
      [](MwndArrayHandle*, int) {
        throw sr::RuntimeError("stage.mwnd has a fixed size");
      },
      sb::arg("size"));

  mwnd.prop(
      "layer", [get](MwndHandle* h) { return get(h)->sorter().second; },
      [get](MwndHandle* h, int layer) { get(h)->set_layer(layer); });

  auto open = [get](MwndHandle* h) { get(h)->SetVisible(true); };
  auto close = [get, sys](MwndHandle* h) {
    sys->text().set_in_pause_state(false);
    get(h)->SetVisible(false);
    get(h)->ClearMouseCapture();
  };
  mwnd.def("open", open);
  mwnd.def("open_nowait", open);
  mwnd.def("open_wait", open);
  mwnd.def("check_open",
           [get](MwndHandle* h) { return get(h)->IsVisible() ? 1 : 0; });
  mwnd.def("close", [close](MwndHandle* h) {
    // some script call this on an non-existing mwnd, ignore the error if that's
    // the case
    try {
      close(h);
    } catch (std::exception&) {
    }
  });
  mwnd.def("close_nowait", close);
  mwnd.def("close_wait", close);
  mwnd.def("end_close", [](MwndHandle*) {});

  sb::class_<MwndObjectList> object_list(m, "MwndObjectList", false);
  object_list.def("__getitem__", &MwndObjectList::Get, sb::arg("index"));
  object_list.def("size", [](const MwndObjectList* list) {
    return static_cast<int>(list->Size());
  });
  object_list.def(
      "resize",
      [](MwndObjectList* list, int) {
        throw sr::RuntimeError(list->Name() + " has a fixed size");
      },
      sb::arg("size"));
  mwnd.add_gc_root(object_list);
  // The Object binding is installed after Mwnd. Resolve its factory on access.
  mwnd.prop_r("object", [get, object_list, &runtime](MwndHandle* h) mutable {
    return sr::Value(object_list.make_inst(get(h), runtime.make_object));
  });
  mwnd.prop_r("button", [get, object_list, &runtime](MwndHandle* h) mutable {
    return sr::Value(object_list.make_inst(get(h), runtime.make_object,
                                           MwndObjectList::Kind::Button));
  });
  mwnd.prop_r("face", [get, object_list, &runtime](MwndHandle* h) mutable {
    return sr::Value(object_list.make_inst(get(h), runtime.make_object,
                                           MwndObjectList::Kind::Face));
  });

  mwnd.def(
      "set_waku",
      [get, sys, state](MwndHandle* h, std::vector<sr::Value> args) {
        CallPacket packet = CallPacket::DecodeFrom(std::move(args));
        auto window = get(h);
        const auto& config = sys->text().mwnd_config();
        const auto& defaults = config.GetWindow(window->window_number());
        const int msg_waku = packet.args.empty()
                                 ? defaults.waku_set
                                 : AsInt(packet.args[0]).value_or(0);
        const int name_waku =
            packet.args.size() < 2
                ? (defaults.namebox.has_waku ? defaults.namebox.waku_set : -1)
                : AsInt(packet.args[1]).value_or(-1);
        TextFactory factory(config);
        window->SetTextboxWaku(msg_waku,
                               factory.CreateWaku(*sys, *window, msg_waku, 0));
        const auto& waku = config.GetWaku(msg_waku);
        auto message = state(h);
        message->waku_file = waku.main_file;
        message->filter_file = waku.filter_file;
        if (name_waku >= 0 && window->GetNameMod() == 1)
          window->SetNameboxWaku(
              name_waku, factory.CreateWaku(*sys, *window, name_waku, 0));
      },
      sb::vararg);
  mwnd.def("init_waku_file", [get, sys, state](MwndHandle* h) {
    auto window = get(h);
    const auto& config = sys->text().mwnd_config();
    if (config.HasWaku(window->waku_set())) {
      const auto& waku = config.GetWaku(window->waku_set());
      window->SetWakuMainFile(waku.main_file);
      window->SetWakuFilterFile(waku.filter_file);
      state(h)->waku_file = waku.main_file;
      state(h)->filter_file = waku.filter_file;
    }
  });
  mwnd.def("set_waku_file", [get, state](MwndHandle* h, std::string file) {
    get(h)->SetWakuMainFile(file);
    state(h)->waku_file = std::move(file);
  });
  mwnd.def("get_waku_file",
           [state](MwndHandle* h) { return state(h)->waku_file; });
  mwnd.def("init_filter_file", [get, sys, state](MwndHandle* h) {
    auto window = get(h);
    const auto& config = sys->text().mwnd_config();
    if (config.HasWaku(window->waku_set())) {
      const auto& file = config.GetWaku(window->waku_set()).filter_file;
      window->SetWakuFilterFile(file);
      state(h)->filter_file = file;
    }
  });
  mwnd.def("set_filter_file", [get, state](MwndHandle* h, std::string file) {
    get(h)->SetWakuFilterFile(file);
    state(h)->filter_file = std::move(file);
  });
  mwnd.def("get_filter_file",
           [state](MwndHandle* h) { return state(h)->filter_file; });

  mwnd.def("msg_block", [get, sys, state, page](MwndHandle* h) {
    auto message = state(h);
    if (message->block_started)
      return;
    if (message->clear_ready) {
      auto window = get(h);
      sys->text().Snapshot();
      window->ClearWin();
      sys->text().NewPageOnWindow(std::move(window));
      message->clear_ready = false;
    }
    // Savepoints and read flags are not implemented in the Siglus runtime.
    message->block_started = true;
    message->auto_mode_base_chars = page(h).number_of_chars_on_page();
  });
  mwnd.def("msg_pp_block", [state, page](MwndHandle* h) {
    state(h)->auto_mode_base_chars = page(h).number_of_chars_on_page();
  });
  mwnd.def("msg_wait", [](MwndHandle*, sr::VM& vm) {
    return lop::MakeResolvedFuture(*vm.gc_);
  });
  mwnd.def("pp", [wait](MwndHandle* h, sr::VM& vm) {
    return wait(h, vm, lop::SiglusMwndWaitOperation::AfterWait::None);
  });
  mwnd.def(
      "r", [get, wait, cfg = &runtime.local_config](MwndHandle* h, sr::VM& vm) {
        if (cfg->ignore_r)
          return lop::MakeResolvedFuture(*vm.gc_);
        return wait(h, vm,
                    get(h)->action_on_pause()
                        ? lop::SiglusMwndWaitOperation::AfterWait::Paragraph
                        : lop::SiglusMwndWaitOperation::AfterWait::Clear);
      });
  mwnd.def("page", [wait](MwndHandle* h, sr::VM& vm) {
    return wait(h, vm, lop::SiglusMwndWaitOperation::AfterWait::Clear);
  });
  mwnd.def("indent", [page](MwndHandle* h) { page(h).SetIndentation(); });
  mwnd.def("clear_indent",
           [page](MwndHandle* h) { page(h).ResetIndentation(); });
  mwnd.def("nil", [page](MwndHandle* h) { page(h).HardBrake(); });
  mwnd.def("nl", [page](MwndHandle* h) {
    page(h).ResetIndentation();
    page(h).HardBrake();
  });
  auto clear = [state, page](MwndHandle* h) {
    state(h)->MarkMessageClearReady(page(h));
  };
  mwnd.def("clear", clear);
  mwnd.def("novel_clear", clear);
  mwnd.def("clear_face", [get](MwndHandle* h) {
    if (auto* waku = get(h)->GetTextboxWaku())
      waku->ClearFaces();
  });
  mwnd.def(
      "set_face",
      [get, sys](MwndHandle* h, std::vector<sr::Value> args) {
        CallPacket packet = CallPacket::DecodeFrom(std::move(args));
        if (packet.args.empty())
          return;
        const int slot =
            packet.args.size() == 1 ? 0 : AsInt(packet.args[0]).value_or(0);
        auto* waku = get(h)->GetTextboxWaku();
        if (!waku || slot < 0 ||
            static_cast<std::size_t>(slot) >= waku->faces_.size())
          return;
        const std::string file =
            AsString(packet.args[packet.args.size() == 1 ? 0 : 1]);
        auto face = waku->TouchFace(slot);
        if (file.empty())
          face->FreeOwnObjectData();
        else
          face->ReplaceFileDrawer(sys->graphics(), file);
      },
      sb::vararg);
  mwnd.def(
      "print",
      [page](MwndHandle* h, std::vector<sr::Value> args) {
        CallPacket packet = CallPacket::DecodeFrom(std::move(args));
        if (packet.args.empty())
          return;
        const std::string text = AsString(packet.args.front());
        if (text.empty())
          return;
        TextPage& target = page(h);
        for (auto cur = text.cbegin(), next = cur; cur != text.cend();
             cur = next) {
          next = cur;
          utf8::next(next, text.cend());
          target.Character(std::string(cur, next),
                           std::string(next, text.cend()));
        }
      },
      sb::vararg);
  mwnd.def("ruby_start", [page, state](MwndHandle* h, std::string ruby) {
    state(h)->pending_ruby = std::move(ruby);
    page(h).MarkRubyBegin();
  });
  mwnd.def("ruby_end", [page, state](MwndHandle* h) {
    auto ruby = std::exchange(state(h)->pending_ruby, std::nullopt);
    if (ruby)
      page(h).DisplayRubyText(*ruby);
  });
  mwnd.def(
      "rep_pos",
      [page](MwndHandle* h, std::vector<sr::Value> args) {
        CallPacket packet = CallPacket::DecodeFrom(std::move(args));
        const int x =
            packet.args.empty() ? 0 : AsInt(packet.args[0]).value_or(0);
        const int y =
            packet.args.size() < 2 ? 0 : AsInt(packet.args[1]).value_or(0);
        page(h).SetGlyphRenderOffset(Point(x, y));
      },
      sb::vararg);
  mwnd.def("rep_pos_default",
           [page](MwndHandle* h) { page(h).ResetGlyphRenderOffset(); });
  mwnd.def(
      "size", [page](MwndHandle* h, int size) { page(h).FontSize(size); },
      sb::arg("size"));
  mwnd.def("size_default",
           [page](MwndHandle* h) { page(h).DefaultFontSize(); });

  auto play_koe = [sys, state, page](MwndHandle* h,
                                     std::vector<sr::Value> args) {
    const auto params = KoeCallParams::ParseFrom(std::move(args));
    auto message = state(h);
    message->current_koe = params.koe;
    message->current_character = params.character;
    message->current_koe_played =
        params.character < 0 ||
        sys->sound().ShouldUseKoeForCharacter(params.character) != 0;
    message->current_koe_no_auto_mode = params.no_auto_mode;
    if (params.character >= 0)
      sys->sound().KoePlay(params.koe, params.character);
    else
      sys->sound().KoePlay(params.koe);
    page(h).KoeMarker(params.koe);
  };
  mwnd.def("koe", play_koe, sb::vararg);
  auto wait_koe = [sys](sr::VM& vm, bool key_skip) {
    return lop::MakePollingWaitFuture(
        vm, [sys] { return !sys->sound().KoePlaying(); }, key_skip,
        sys->event_ptr().get());
  };
  mwnd.def(
      "koe_play_wait",
      [play_koe, wait_koe](MwndHandle* h, sr::VM& vm,
                           std::vector<sr::Value> args) {
        play_koe(h, std::move(args));
        return wait_koe(vm, false);
      },
      sb::vararg);
  mwnd.def(
      "koe_play_wait_key",
      [play_koe, wait_koe](MwndHandle* h, sr::VM& vm,
                           std::vector<sr::Value> args) {
        play_koe(h, std::move(args));
        return wait_koe(vm, true);
      },
      sb::vararg);
}

RLVM_REGISTER(SiglusBindingRegistry, "1_mwnd", BindMwnd)

}  // namespace libsiglus::binding
