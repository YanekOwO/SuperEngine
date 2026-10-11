// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
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

#include "core/gameexe.hpp"
#include "core/rect.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/bindings/wait_helpers.hpp"
#include "srbind/srbind.hpp"
#include "systems/event_system.hpp"
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

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace libsiglus::binding {
namespace sb = srbind;
namespace sr = serilang;

namespace {

bool ConfigFlag(const std::shared_ptr<Gameexe>& cfg, std::string const& key) {
  if (!cfg)
    return false;
  return (*cfg)(key).Int().value_or(0) != 0;
}

bool MessageNowait(System* system, const std::shared_ptr<Gameexe>& cfg) {
  if (!system)
    return true;
  TextSystem& text = system->text();
  return system->ShouldFastForward() || text.message_no_wait() ||
         text.script_message_nowait() || ConfigFlag(cfg, "message_nowait");
}

int ConfigInt(const std::shared_ptr<Gameexe>& cfg,
              std::string const& key,
              int fallback) {
  if (!cfg)
    return fallback;
  return (*cfg)(key).Int().value_or(fallback);
}

bool AutoModeEnabled(System* system, const std::shared_ptr<Gameexe>& cfg) {
  return system &&
         (system->text().auto_mode() != 0 || ConfigFlag(cfg, "auto_mode"));
}

int CurrentPageCharCount(System* system) {
  if (!system)
    return 0;
  return system->text().GetCurrentPage().number_of_chars_on_page();
}

struct MwndMessageState {
  bool block_started = false;
  bool clear_ready = false;
  int auto_mode_base_chars = 0;
  int current_koe = -1;
  int current_character = -1;
  bool current_koe_played = false;
  bool current_koe_no_auto_mode = false;

  void ClearKoe() {
    current_koe = -1;
    current_character = -1;
    current_koe_played = false;
    current_koe_no_auto_mode = false;
  }

  void MarkNovelClear(System& system) {
    block_started = false;
    auto_mode_base_chars =
        system.text().GetCurrentPage().number_of_chars_on_page();
    ClearKoe();
  }
  void MarkMessageClearReady(System& system) {
    clear_ready = true;
    MarkNovelClear(system);
  }
};

struct WakuSelection {
  int msg_waku_no = 0;
  int name_waku_no = -1;
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

class MwndWaitTask : public CoroutineTask {
 public:
  enum class AfterWait { None, Paragraph, Clear };

  MwndWaitTask(sr::VM& vm,
               System* system,
               std::shared_ptr<Gameexe> local_config,
               std::shared_ptr<MwndMessageState> message_state,
               AfterWait after_wait)
      : CoroutineTask(vm, system ? system->event_ptr().get() : nullptr),
        system_(system),
        local_config_(std::move(local_config)),
        message_state_(std::move(message_state)),
        after_wait_(after_wait) {}

  TaskCoroutine Run() override {
    if (!system_ || MessageNowait(system_, local_config_)) {
      FinishWait();
      co_return 0;
    }

    PauseGuard pause(system_, after_wait_ == AfterWait::Clear);

    try {
      TextSystem& text = system_->text();
      const unsigned int start_ticks = system_->event().GetTicks();
      const int auto_time = text.GetAutoTime(AutoModeCharCount());

      while (true) {
        if ((co_await WaitFor(std::chrono::milliseconds(5), true)) ==
            WaitOutcome::InterruptedByInput) {
          pause.Reset();
          FinishWait();
          co_return 1;
        }

        if (MessageNowait(system_, local_config_)) {
          pause.Reset();
          FinishWait();
          co_return 0;
        }

        if (AutoModeEnabled(system_, local_config_)) {
          const unsigned int elapsed =
              system_->event().GetTicks() - start_ticks;
          if (elapsed >= static_cast<unsigned int>(std::max(auto_time, 0))) {
            pause.Reset();
            FinishWait();
            co_return 0;
          }
        }
      }
    } catch (...) {
      pause.Reset();
      throw;
    }
  }

 private:
  class PauseGuard {
   public:
    PauseGuard(System* system, bool page_icon)
        : text_(system ? &system->text() : nullptr) {
      if (text_) {
        text_->set_in_pause_state(true);
        text_->GetCurrentWindow()->ShowWaitIcon(page_icon);
      }
    }

    ~PauseGuard() { Reset(); }

    void Reset() {
      if (!text_)
        return;

      if (text_->HasCurrentWindow())
        text_->GetCurrentWindow()->HideWaitIcon();
      text_->set_in_pause_state(false);
      text_ = nullptr;
    }

   private:
    TextSystem* text_;
  };

  void FinishWait() {
    if (!system_ || !message_state_)
      return;
    switch (after_wait_) {
      case AfterWait::None:
        return;
      case AfterWait::Paragraph: {
        message_state_->MarkNovelClear(*system_);
        TextPage& page = system_->text().GetCurrentPage();
        page.ResetIndentation();
        page.HardBrake();
        return;
      }
      case AfterWait::Clear:
        message_state_->MarkMessageClearReady(*system_);
        return;
    }
  }

  int AutoModeCharCount() {
    const int configured_count =
        ConfigInt(local_config_, "auto_mode_moji_cnt", 0);
    if (configured_count > 0)
      return configured_count;

    const int current_count =
        system_->text().GetCurrentPage().number_of_chars_on_page();
    if (!message_state_)
      return current_count;
    return std::max(current_count - message_state_->auto_mode_base_chars, 0);
  }

  System* system_;
  std::shared_ptr<Gameexe> local_config_;
  std::shared_ptr<MwndMessageState> message_state_;
  AfterWait after_wait_;
};

struct MwndBindingState {
  MwndBindingState(sr::VM& vm,
                   System* system,
                   std::shared_ptr<Gameexe> local_config)
      : vm(vm),
        system(system),
        local_config(std::move(local_config)),
        message_state(std::make_shared<MwndMessageState>()) {
    if (system) {
      const auto& config = system->text().mwnd_config();
      const auto& window = config.GetWindow(config.default_window());
      current_waku_set = window.waku_set;
      current_name_waku_set =
          window.namebox.has_waku ? window.namebox.waku_set : -1;
      InitWakuFiles();
    }
  }

  sr::Value Wait(MwndWaitTask::AfterWait after_wait) {
    auto wait_task = std::make_unique<MwndWaitTask>(vm, system, local_config,
                                                    message_state, after_wait);
    return sr::Value(pending_waits.MakeFuture(*vm.gc_, std::move(wait_task)));
  }

  sr::Value WaitForR() {
    if (!system)
      return Wait(MwndWaitTask::AfterWait::None);
    const bool novel_mode =
        system->text().GetCurrentWindow()->action_on_pause();
    return Wait(novel_mode ? MwndWaitTask::AfterWait::Paragraph
                           : MwndWaitTask::AfterWait::Clear);
  }

  void Open() {
    window_open = true;
    if (!system)
      return;

    TextSystem& text = system->text();
    text.GetCurrentWindow()->SetVisible(true);
  }

  void Close() {
    window_open = false;
    if (!system)
      return;

    TextSystem& text = system->text();
    text.set_in_pause_state(false);
    text.HideTextWindow(text.active_window());
  }

  int CheckOpen() {
    if (!system)
      return window_open ? 1 : 0;
    return system->text().GetCurrentWindow()->IsVisible() ? 1 : 0;
  }

  void SetWaku(std::optional<int> msg_waku_no,
               std::optional<int> name_waku_no) {
    if (!system) {
      if (msg_waku_no)
        current_waku_set = *msg_waku_no;
      if (name_waku_no)
        current_name_waku_set = *name_waku_no;
      return;
    }

    TextSystem& text = system->text();
    const int window_number = text.active_window();
    const WakuSelection defaults = GetDefaultWakuSelection(window_number);
    const int resolved_msg_waku = msg_waku_no.value_or(defaults.msg_waku_no);
    const int resolved_name_waku = name_waku_no.value_or(defaults.name_waku_no);

    current_waku_set = resolved_msg_waku;
    current_name_waku_set = resolved_name_waku;
    std::shared_ptr<TextWindow> window = text.GetCurrentWindow();
    TextFactory waku_factory(text.mwnd_config());
    window->SetTextboxWaku(
        resolved_msg_waku,
        waku_factory.CreateWaku(*system, *window, resolved_msg_waku, 0));
    const auto& waku_config = text.mwnd_config().GetWaku(resolved_msg_waku);
    current_waku_file = waku_config.main_file;
    current_filter_file = waku_config.filter_file;
    for (std::size_t i = 0;
         i < waku_config.face_positions.size() && i < MwndConfig::kNumFaceSlots;
         ++i) {
      window->SetFaceSlotPosition(static_cast<int>(i),
                                  waku_config.face_positions[i]);
    }

    if (resolved_name_waku >= 0 && window->GetNameMod() == 1) {
      window->SetNameboxWaku(
          resolved_name_waku,
          waku_factory.CreateWaku(*system, *window, resolved_name_waku, 0));
    }
  }

  void InitWakuFiles() {
    if (!system)
      return;
    const auto& config = system->text().mwnd_config();
    if (!config.HasWaku(current_waku_set))
      return;
    const auto& waku = config.GetWaku(current_waku_set);
    SetWakuFile(waku.main_file);
    SetFilterFile(waku.filter_file);
  }

  void SetWakuFile(std::string file) {
    current_waku_file = std::move(file);
    if (system)
      system->text().GetCurrentWindow()->SetWakuMainFile(current_waku_file);
  }

  void SetFilterFile(std::string file) {
    current_filter_file = std::move(file);
    if (system)
      system->text().GetCurrentWindow()->SetWakuFilterFile(current_filter_file);
  }

  WakuSelection GetDefaultWakuSelection(int window_number) {
    auto [it, inserted] = default_waku_selections.try_emplace(window_number);
    if (inserted) {
      const MwndConfig::Window& config =
          system->text().mwnd_config().GetWindow(window_number);
      it->second.msg_waku_no = config.waku_set;
      it->second.name_waku_no =
          config.namebox.has_waku ? config.namebox.waku_set : -1;
    }
    return it->second;
  }

  void Print(std::string text) {
    if (!system || text.empty())
      return;

    TextPage& page = system->text().GetCurrentPage();
    for (auto cur = text.cbegin(), next = cur; cur != text.cend(); cur = next) {
      next = cur;
      utf8::next(next, text.cend());
      const std::string current(cur, next);
      const std::string rest(next, text.cend());
      page.Character(current, rest);
    }
  }

  void RubyStart(std::string ruby) {
    pending_ruby = std::move(ruby);
    if (system)
      system->text().GetCurrentPage().MarkRubyBegin();
  }

  void RubyEnd() {
    auto ruby = std::exchange(pending_ruby, std::nullopt);
    if (system && ruby)
      system->text().GetCurrentPage().DisplayRubyText(*ruby);
  }

  void RepPos(int x, int y) {
    glyph_render_offset = Point(x, y);
    if (!system)
      return;

    system->text().GetCurrentPage().SetGlyphRenderOffset(glyph_render_offset);
  }

  void RepPosDefault() {
    glyph_render_offset = Point(0, 0);
    if (!system)
      return;

    system->text().GetCurrentPage().ResetGlyphRenderOffset();
  }

  void PlayKoe(KoeCallParams params) {
    const bool character_enabled =
        !system || params.character < 0 ||
        system->sound().ShouldUseKoeForCharacter(params.character) != 0;

    if (message_state) {
      message_state->current_koe = params.koe;
      message_state->current_character = params.character;
      message_state->current_koe_played = character_enabled;
      message_state->current_koe_no_auto_mode = params.no_auto_mode;
    }

    if (!system)
      return;

    if (params.character >= 0)
      system->sound().KoePlay(params.koe, params.character);
    else
      system->sound().KoePlay(params.koe);
    system->text().GetCurrentPage().KoeMarker(params.koe);
  }

  sr::Value WaitKoe(sr::VM& vm, bool key_skip) {
    auto done = [system = system] {
      return !system || !system->sound().KoePlaying();
    };
    return MakePollingWaitFuture(vm, std::move(done), key_skip,
                                 system ? system->event_ptr().get() : nullptr);
  }

  sr::VM& vm;
  System* system;
  std::shared_ptr<Gameexe> local_config;
  std::shared_ptr<MwndMessageState> message_state;
  bool window_open = false;
  int current_waku_set = 0;
  int current_name_waku_set = -1;
  Point glyph_render_offset = Point(0, 0);
  std::string current_waku_file;
  std::string current_filter_file;
  std::optional<std::string> pending_ruby;
  std::unordered_map<int, WakuSelection> default_waku_selections;
  PendingCoroutineTasks pending_waits;
};

}  // namespace

void BindMwnd(SiglusMachine& runtime) {
  sr::VM& vm = *runtime.vm;

  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  sb::class_<MwndBindingState> mwnd_cls(m, "Mwnd", false);
  auto mwnd =
      mwnd_cls.inst("mwnd", vm, runtime.system.get(), runtime.local_config);
  auto close = [](MwndBindingState* state) { state->Close(); };
  auto open = [](MwndBindingState* state) { state->Open(); };
  mwnd.def("open", open);
  mwnd.def("open_nowait", open);
  mwnd.def("open_wait", open);
  mwnd.def("check_open", &MwndBindingState::CheckOpen);
  mwnd.def(
      "set_waku",
      [](MwndBindingState* state, std::vector<sr::Value> args) {
        CallPacket packet = CallPacket::DecodeFrom(std::move(args));
        std::optional<int> msg_waku_no;
        std::optional<int> name_waku_no;

        if (packet.args.size() >= 1)
          msg_waku_no = AsInt(packet.args[0]).value_or(0);
        if (packet.args.size() >= 2)
          name_waku_no = AsInt(packet.args[1]).value_or(-1);

        state->SetWaku(msg_waku_no, name_waku_no);
      },
      sb::vararg);
  mwnd.def("init_waku_file", &MwndBindingState::InitWakuFiles);
  mwnd.def("set_waku_file", &MwndBindingState::SetWakuFile);
  mwnd.def("get_waku_file",
           [](MwndBindingState* state) { return state->current_waku_file; });
  mwnd.def("init_filter_file", [](MwndBindingState* state) {
    if (!state || !state->system)
      return;
    const auto& config = state->system->text().mwnd_config();
    if (config.HasWaku(state->current_waku_set))
      state->SetFilterFile(config.GetWaku(state->current_waku_set).filter_file);
  });
  mwnd.def("set_filter_file", &MwndBindingState::SetFilterFile);
  mwnd.def("get_filter_file",
           [](MwndBindingState* state) { return state->current_filter_file; });
  mwnd.def("close", close);
  mwnd.def("close_nowait", close);
  mwnd.def("close_wait", close);
  mwnd.def("end_close", [](MwndBindingState*) {});
  mwnd.def("msg_block", [](MwndBindingState* state) {
    auto msgstate = state->message_state;
    if (!msgstate)
      return;

    if (msgstate->block_started)
      return;

    if (msgstate->clear_ready) {
      auto* system = state->system;
      if (!system)
        return;

      TextSystem& text = system->text();
      const int active_window = text.active_window();
      text.Snapshot();
      text.GetTextWindow(active_window)->ClearWin();
      text.NewPageOnWindow(active_window);

      msgstate->clear_ready = false;
    }

    // Legacy Siglus also updates savepoints/backlog/read flags here. Those
    // subsystems do not exist in the current Siglus runtime yet.
    msgstate->block_started = true;
    msgstate->auto_mode_base_chars = CurrentPageCharCount(state->system);
  });
  mwnd.def("msg_pp_block", [](MwndBindingState* state) {
    auto msgstate = state->message_state;
    if (!msgstate)
      return;
    msgstate->auto_mode_base_chars = CurrentPageCharCount(state->system);
  });
  mwnd.def("msg_wait", [](MwndBindingState*, sr::VM& vm) -> sr::Value {
    return MakeResolvedFuture(*vm.gc_);
  });
  mwnd.def("pp", [](MwndBindingState* state, sr::VM&) -> sr::Value {
    return state->Wait(MwndWaitTask::AfterWait::None);
  });
  mwnd.def("r", [](MwndBindingState* state, sr::VM& vm) -> sr::Value {
    if (ConfigFlag(state->local_config, "ignore_r"))
      return MakeResolvedFuture(*vm.gc_);
    return state->WaitForR();
  });
  mwnd.def("page", [](MwndBindingState* state, sr::VM&) -> sr::Value {
    return state->Wait(MwndWaitTask::AfterWait::Clear);
  });
  mwnd.def("indent", [](MwndBindingState* state) {
    if (state && state->system)
      state->system->text().GetCurrentPage().SetIndentation();
  });
  mwnd.def("clear_indent", [](MwndBindingState* state) {
    if (state && state->system)
      state->system->text().GetCurrentPage().ResetIndentation();
  });
  mwnd.def("nil", [](MwndBindingState* state) {
    if (state && state->system)
      state->system->text().GetCurrentPage().HardBrake();
  });
  mwnd.def("nl", [](MwndBindingState* state) {
    if (!state || !state->system)
      return;
    TextPage& page = state->system->text().GetCurrentPage();
    page.ResetIndentation();
    page.HardBrake();
  });
  mwnd.def("clear", [](MwndBindingState* state) {
    if (!state || !state->system)
      return;
    state->message_state->MarkMessageClearReady(*state->system);
  });
  mwnd.def("novel_clear", [](MwndBindingState* state) {
    if (!state || !state->system)
      return;
    state->message_state->MarkMessageClearReady(*state->system);
  });
  mwnd.def("clear_face", [](MwndBindingState* state) {
    if (!state || !state->system)
      return;
    auto window = state->system->text().GetCurrentWindow();
    for (int i = 0; i < MwndConfig::kNumFaceSlots; ++i)
      window->FaceClose(i);
  });
  mwnd.def(
      "set_face",
      [](MwndBindingState* state, std::vector<sr::Value> args) {
        if (!state || !state->system || args.empty())
          return;
        int slot = 0;
        std::string file;
        if (args.size() == 1) {
          file = AsString(args[0]);
        } else {
          slot = AsInt(args[0]).value_or(0);
          file = AsString(args[1]);
        }
        if (slot >= 0 && slot < MwndConfig::kNumFaceSlots)
          state->system->text().GetCurrentWindow()->FaceOpen(file, slot);
      },
      sb::vararg);
  mwnd.def(
      "print",
      [](MwndBindingState* state, std::vector<sr::Value> args) {
        CallPacket packet = CallPacket::DecodeFrom(std::move(args));
        std::string text =
            packet.args.empty() ? std::string() : AsString(packet.args.front());
        state->Print(std::move(text));
      },
      sb::vararg);
  mwnd.def("ruby_start", &MwndBindingState::RubyStart);
  mwnd.def("ruby_end", &MwndBindingState::RubyEnd);
  mwnd.def(
      "rep_pos",
      [](MwndBindingState* state, std::vector<sr::Value> args) {
        CallPacket packet = CallPacket::DecodeFrom(std::move(args));
        const int x =
            packet.args.empty() ? 0 : AsInt(packet.args[0]).value_or(0);
        const int y =
            packet.args.size() < 2 ? 0 : AsInt(packet.args[1]).value_or(0);
        state->RepPos(x, y);
      },
      sb::vararg);
  mwnd.def("rep_pos_default", &MwndBindingState::RepPosDefault);
  mwnd.def(
      "size",
      [](MwndBindingState* state, int size) {
        if (!state->system)
          return;
        state->system->text().GetCurrentPage().FontSize(size);
      },
      sb::arg("size"));
  mwnd.def("size_default", [](MwndBindingState* state) {
    if (!state->system)
      return;
    state->system->text().GetCurrentPage().DefaultFontSize();
  });
  mwnd.def(
      "koe",
      [](MwndBindingState* state, std::vector<sr::Value> args) {
        state->PlayKoe(KoeCallParams::ParseFrom(std::move(args)));
      },
      sb::vararg);
  mwnd.def(
      "koe_play_wait",
      [](MwndBindingState* state, sr::VM& vm,
         std::vector<sr::Value> args) -> sr::Value {
        state->PlayKoe(KoeCallParams::ParseFrom(std::move(args)));
        return state->WaitKoe(vm, false);
      },
      sb::vararg);
  mwnd.def(
      "koe_play_wait_key",
      [](MwndBindingState* state, sr::VM& vm,
         std::vector<sr::Value> args) -> sr::Value {
        state->PlayKoe(KoeCallParams::ParseFrom(std::move(args)));
        return state->WaitKoe(vm, true);
      },
      sb::vararg);
}

RLVM_REGISTER(SiglusBindingRegistry, "1_mwnd", BindMwnd)

}  // namespace libsiglus::binding
