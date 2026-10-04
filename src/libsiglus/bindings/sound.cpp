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

#include "core/bgm_table.hpp"
#include "libsiglus/bindings/bootstrap.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/bindings/wait_helpers.hpp"
#include "srbind/module.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <format>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace libsiglus::binding {
namespace sr = serilang;
namespace sb = srbind;

class PlaybackState {
 public:
  static constexpr int kFree = 0;
  static constexpr int kPlay = 1;
  static constexpr int kFadeOut = 2;
  static constexpr int kPause = 3;

  void Set(int state) { state_ = state; }
  int state() const { return state_; }

  int Check(const std::function<bool()>& is_playing) const {
    if (state_ == kFadeOut || state_ == kPause)
      return state_;
    if (is_playing && is_playing())
      return kPlay;
    return state_ == kPlay ? kPlay : kFree;
  }

  sr::Value WaitForPlayback(sr::VM& vm,
                            System* system,
                            bool key_skip,
                            bool fade_only,
                            std::function<bool()> is_playing) {
    if (fade_only && state_ != kFadeOut)
      return MakeResolvedFuture(*vm.gc_);

    auto done = [this, is_playing = std::move(is_playing)] {
      if (!is_playing || !is_playing()) {
        if (state_ == kPlay || state_ == kFadeOut)
          state_ = kFree;
        return true;
      }
      return false;
    };
    return MakePollingWaitFuture(vm, std::move(done), key_skip,
                                 system ? system->event_ptr().get() : nullptr);
  }

 private:
  int state_ = kFree;
};

struct ExKoeCallParams {
  int koe = 0;
  int character = -1;
  bool wait = false;
  bool key_skip = false;

  static ExKoeCallParams ParseFrom(std::vector<sr::Value> raw_args,
                                   bool default_wait,
                                   bool default_key_skip) {
    CallPacket packet = CallPacket::DecodeFrom(std::move(raw_args));
    ExKoeCallParams params;
    params.wait = default_wait;
    params.key_skip = default_key_skip;

    if (!packet.args.empty())
      params.koe = AsInt(packet.args[0]).value_or(0);
    if (packet.args.size() > 1)
      params.character = AsInt(packet.args[1]).value_or(-1);

    ForEachKeywordId(packet.kwargs, [&](int id, const sr::Value& value) {
      switch (id) {
        case 0:
          params.koe = AsInt(value).value_or(params.koe);
          break;
        case 1:
          params.character = AsInt(value).value_or(params.character);
          break;
        case 2:
          params.wait = AsInt(value).value_or(params.wait ? 1 : 0) != 0;
          break;
        case 3:
          params.key_skip = AsInt(value).value_or(params.key_skip ? 1 : 0) != 0;
          break;
        case 4:
          // Legacy jitan speed has no voice-rate backend in rlvm yet.
          break;
        default:
          break;
      }
    });

    return params;
  }
};

struct BgmPlayParams {
  std::string registered_name;
  int fade_in_ms = 0;
  int fade_out_ms = 0;
  bool has_name = false;
  bool has_fade_in_ms = false;
  bool has_fade_out_ms = false;

  static BgmPlayParams ParseFrom(std::vector<sr::Value> raw_args) {
    CallPacket packet = CallPacket::DecodeFrom(std::move(raw_args));
    BgmPlayParams params;

    if (!packet.args.empty()) {
      params.registered_name = AsString(packet.args[0]);
      params.has_name = true;
    }
    if (packet.args.size() > 1)
      params.fade_in_ms = AsInt(packet.args[1]).value_or(0);
    if (packet.args.size() > 2)
      params.fade_out_ms = AsInt(packet.args[2]).value_or(0);
    params.has_fade_in_ms = packet.args.size() > 1;
    params.has_fade_out_ms = packet.args.size() > 2;
    return params;
  }
};

struct BgmSetVolumeParams {
  int volume = 0;
  int fade_ms = 0;
  bool has_volume = false;

  static BgmSetVolumeParams ParseFrom(std::vector<sr::Value> raw_args,
                                      int fallback_volume) {
    CallPacket packet = CallPacket::DecodeFrom(std::move(raw_args));
    BgmSetVolumeParams params;
    if (!packet.args.empty()) {
      params.volume = AsInt(packet.args[0]).value_or(fallback_volume);
      params.has_volume = true;
    }
    if (packet.args.size() > 1)
      params.fade_ms = AsInt(packet.args[1]).value_or(0);
    return params;
  }
};

struct PcmPlayParams {
  std::string pcm_name;
  int fade_in_ms = 0;
  bool has_name = false;
  bool has_fade_in_ms = false;
  bool loop = false;
  bool ready = false;

  static PcmPlayParams ParseFrom(std::vector<sr::Value> raw_args,
                                 bool loop,
                                 bool ready) {
    CallPacket packet = CallPacket::DecodeFrom(std::move(raw_args));
    PcmPlayParams params;
    params.loop = loop;
    params.ready = ready;

    if (!packet.args.empty()) {
      params.pcm_name = AsString(packet.args[0]);
      params.has_name = true;
    }
    if (packet.args.size() > 1)
      params.fade_in_ms = AsInt(packet.args[1]).value_or(0);
    params.has_fade_in_ms = packet.args.size() > 1;
    return params;
  }
};

struct PcmVolumeParams {
  int volume = 0;
  int fade_ms = 0;
  bool has_volume = false;

  static PcmVolumeParams ParseFrom(std::vector<sr::Value> raw_args,
                                   int fallback_volume) {
    CallPacket packet = CallPacket::DecodeFrom(std::move(raw_args));
    PcmVolumeParams params;
    if (!packet.args.empty()) {
      params.volume = AsInt(packet.args[0]).value_or(fallback_volume);
      params.has_volume = true;
    }
    if (packet.args.size() > 1)
      params.fade_ms = AsInt(packet.args[1]).value_or(0);
    return params;
  }
};

static int ParseKoeStop(std::vector<sr::Value> raw_args) {
  CallPacket packet = CallPacket::DecodeFrom(std::move(raw_args));
  int fade_ms = 0;
  if (!packet.args.empty())
    fade_ms = AsInt(packet.args[0]).value_or(0);

  ForEachKeywordId(packet.kwargs, [&](int id, const sr::Value& value) {
    if (id == 0)
      fade_ms = AsInt(value).value_or(fade_ms);
  });

  return fade_ms;
}

class SiglusGlobalKoe {
 public:
  explicit SiglusGlobalKoe(System* sys) : system_(sys) {}

  void Play(const ExKoeCallParams& params) {
    if (!system_)
      return;

    system_->sound().KoeStop();
    if (params.character >= 0)
      system_->sound().KoePlay(params.koe, params.character);
    else
      system_->sound().KoePlay(params.koe);
  }

  sr::Value WaitForKoe(sr::VM& vm, bool key_skip) {
    auto done = [system = system_] {
      return !system || !system->sound().KoePlaying();
    };
    return MakePollingWaitFuture(
        vm, std::move(done), key_skip,
        system_ ? system_->event_ptr().get() : nullptr);
  }

  void Stop(int fade_ms) {
    if (!system_)
      return;

    if (fade_ms > 0)
      system_->sound().WavFadeOut(KOE_CHANNEL, fade_ms);
    else
      system_->sound().KoeStop();
  }

  System* system_;
};

class SiglusBgm {
 public:
  explicit SiglusBgm(System* sys, std::shared_ptr<BgmTable> bgm_table)
      : system_(sys), bgm_table_(std::move(bgm_table)) {}

  void Start(const BgmPlayParams& params, bool loop) {
    if (!params.has_name)
      return;

    registered_name_ = params.registered_name;
    playback_.Set(PlaybackState::kPlay);

    if (system_) {
      if (params.has_fade_out_ms)
        system_->sound().BgmPlay(registered_name_, loop, params.fade_in_ms,
                                 params.fade_out_ms);
      else if (params.has_fade_in_ms)
        system_->sound().BgmPlay(registered_name_, loop, params.fade_in_ms);
      else
        system_->sound().BgmPlay(registered_name_, loop);
    }

    bgm_table_->SetListen(registered_name_, true, false);
  }

  void Prepare(const BgmPlayParams& params, bool loop) {
    if (!params.has_name)
      return;

    registered_name_ = params.registered_name;
    playback_.Set(PlaybackState::kPause);

    if (system_)
      system_->sound().BgmPrepare(registered_name_, loop);

    bgm_table_->SetListen(registered_name_, true, false);
  }

  void Stop(int fade_ms) {
    if (system_) {
      if (fade_ms > 0)
        system_->sound().BgmFadeOut(fade_ms);
      else
        system_->sound().BgmStop();
    }
    playback_.Set(fade_ms > 0 ? PlaybackState::kFadeOut : PlaybackState::kFree);
  }

  void Pause() {
    if (system_)
      system_->sound().BgmPause();
    playback_.Set(PlaybackState::kPause);
  }

  void Resume() {
    if (system_)
      system_->sound().BgmUnPause();
    playback_.Set(PlaybackState::kPlay);
  }

  int Check() const {
    return playback_.Check(
        [this] { return system_ && system_->sound().BgmStatus(); });
  }

  void SetVolume(const BgmSetVolumeParams& params) {
    if (!params.has_volume)
      return;

    volume_ = params.volume;
    if (system_)
      system_->sound().SetBgmVolumeScript(volume_, params.fade_ms);
  }

  std::string GetRegistName() const {
    if (!registered_name_.empty())
      return registered_name_;
    if (system_)
      return system_->sound().GetBgmName();
    return "";
  }

  int GetPlayPos() const {
    if (!system_)
      return 0;
    auto player = system_->sound().GetBgm();
    return player ? static_cast<int>(player->GetCurrentTime()) : 0;
  }

  sr::Value WaitForPlayback(sr::VM& vm, bool key_skip, bool fade_only) {
    return playback_.WaitForPlayback(vm, system_, key_skip, fade_only, [this] {
      return system_ && system_->sound().BgmStatus();
    });
  }

  System* system_;
  std::shared_ptr<BgmTable> bgm_table_;
  PlaybackState playback_;
  int volume_ = 255;
  int volume_max_ = 255;
  int volume_min_ = 0;
  std::string registered_name_;
};

class SiglusPcmch {
 public:
  SiglusPcmch(System* sys, int channel) : system_(sys), channel_(channel) {}

  void Stop(int fade_ms) {
    if (system_ && IsValidChannel()) {
      if (fade_ms > 0)
        system_->sound().WavFadeOut(channel_, fade_ms);
      else
        system_->sound().WavStop(channel_);
    }
    playback_.Set(fade_ms > 0 ? PlaybackState::kFadeOut : PlaybackState::kFree);
    loop_ = false;
  }

  void Pause() { playback_.Set(PlaybackState::kPause); }

  void Resume(int fade_ms) {
    ready_ = false;
    if (!pcm_name_.empty())
      StartPlayback(fade_ms);
    playback_.Set(PlaybackState::kPlay);
  }

  sr::Value ResumeWait(sr::VM& vm, int fade_ms) {
    Resume(fade_ms);
    return WaitForPlayback(vm, false, false);
  }

  int Check() const {
    return playback_.Check([this] {
      return system_ && IsValidChannel() &&
             system_->sound().WavPlaying(channel_);
    });
  }

  void SetVolume(const PcmVolumeParams& params) {
    if (!params.has_volume)
      return;

    SetVolume(params.volume, params.fade_ms);
  }

  void Play(PcmPlayParams params) {
    if (!params.has_name)
      return;

    pcm_name_ = std::move(params.pcm_name);
    fade_in_ms_ = params.fade_in_ms;
    loop_ = params.loop;
    ready_ = params.ready;

    if (ready_) {
      playback_.Set(PlaybackState::kFree);
      return;
    }

    StartPlayback(fade_in_ms_);
  }

  void StartPlayback(int fade_ms) {
    playback_.Set(PlaybackState::kPlay);
    if (system_ && IsValidChannel() && !pcm_name_.empty())
      system_->sound().WavPlay(pcm_name_, loop_, channel_, fade_ms);
  }

  void SetVolume(int volume, int fade_ms) {
    volume_ = volume;
    if (!system_ || !IsValidChannel())
      return;

    if (fade_ms > 0)
      system_->sound().SetChannelVolume(channel_, volume_, fade_ms);
    else
      system_->sound().SetChannelVolume(channel_, volume_);
  }

  bool IsValidChannel() const {
    return channel_ >= 0 && channel_ < NUM_TOTAL_CHANNELS;
  }

  sr::Value WaitForPlayback(sr::VM& vm, bool key_skip, bool fade_only) {
    return playback_.WaitForPlayback(vm, system_, key_skip, fade_only, [this] {
      return system_ && IsValidChannel() &&
             system_->sound().WavPlaying(channel_);
    });
  }

  static constexpr int kVolumeMin = 0;
  static constexpr int kVolumeMax = 255;

  System* system_;
  int channel_;
  PlaybackState playback_;
  int volume_ = kVolumeMax;
  int fade_in_ms_ = 0;
  bool loop_ = false;
  bool ready_ = false;
  std::string pcm_name_;
};

void BindSound(SiglusRuntime& runtime) {
  sr::VM& vm = *runtime.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());

  runtime.bgm_table =
      std::make_shared<BgmTable>(BgmTable::CreateFromSiglus(*runtime.gameexe));
  auto bgm_table = runtime.bgm_table;
  sb::module_ tab(vm, "bgm_table");
  tab.def("cnt", [bgm_table]() -> int { return bgm_table->Count(); });
  tab.def("set_listen", [bgm_table](std::string name, int val) {
    bgm_table->SetListen(std::move(name), val);
  });
  tab.def("get_listen", [bgm_table](std::string name) -> int {
    return bgm_table->GetListen(std::move(name));
  });
  tab.def("set_listen_all",
          [bgm_table](int val) { bgm_table->SetListenAll(val); });

  auto global_koe = std::make_shared<SiglusGlobalKoe>(runtime.system.get());
  m.def(
      "exkoe",
      [global_koe](std::vector<sr::Value> args) -> int {
        global_koe->Play(
            ExKoeCallParams::ParseFrom(std::move(args), false, false));
        return 0;
      },
      sb::vararg);
  m.def(
      "exkoe_play_wait",
      [global_koe](sr::VM& vm, std::vector<sr::Value> args) -> sr::Value {
        const auto params =
            ExKoeCallParams::ParseFrom(std::move(args), true, false);
        global_koe->Play(params);
        return params.wait ? global_koe->WaitForKoe(vm, params.key_skip)
                           : MakeResolvedFuture(*vm.gc_);
      },
      sb::vararg);
  m.def(
      "exkoe_play_wait_key",
      [global_koe](sr::VM& vm, std::vector<sr::Value> args) -> sr::Value {
        const auto params =
            ExKoeCallParams::ParseFrom(std::move(args), true, true);
        global_koe->Play(params);
        return params.wait ? global_koe->WaitForKoe(vm, params.key_skip)
                           : MakeResolvedFuture(*vm.gc_);
      },
      sb::vararg);
  m.def(
      "koe_stop",
      [global_koe](std::vector<sr::Value> args) {
        global_koe->Stop(ParseKoeStop(std::move(args)));
      },
      sb::vararg);

  sb::class_<SiglusBgm> bgm_cls(m, "Bgm", false);
  auto bgm = bgm_cls.inst("bgm", runtime.system.get(), bgm_table);
  bgm.def(
      "play",
      [](SiglusBgm* bgm, std::vector<sr::Value> args) {
        bgm->Start(BgmPlayParams::ParseFrom(std::move(args)), true);
      },
      sb::vararg);
  bgm.def(
      "play_oneshot",
      [](SiglusBgm* bgm, std::vector<sr::Value> args) {
        bgm->Start(BgmPlayParams::ParseFrom(std::move(args)), false);
      },
      sb::vararg);
  bgm.def(
      "play_wait",
      [](SiglusBgm* bgm, sr::VM& vm, std::vector<sr::Value> args) -> sr::Value {
        bgm->Start(BgmPlayParams::ParseFrom(std::move(args)), false);
        return bgm->WaitForPlayback(vm, false, false);
      },
      sb::vararg);
  bgm.def(
      "ready",
      [](SiglusBgm* bgm, std::vector<sr::Value> args) {
        bgm->Prepare(BgmPlayParams::ParseFrom(std::move(args)), true);
      },
      sb::vararg);
  bgm.def(
      "ready_oneshot",
      [](SiglusBgm* bgm, std::vector<sr::Value> args) {
        bgm->Prepare(BgmPlayParams::ParseFrom(std::move(args)), false);
      },
      sb::vararg);
  bgm.def(
      "stop",
      [](SiglusBgm* bgm, std::vector<sr::Value> args) {
        const int fade_ms = args.empty() ? 0 : AsInt(args[0]).value_or(0);
        bgm->Stop(fade_ms);
      },
      sb::vararg);
  bgm.def(
      "pause", [](SiglusBgm* bgm, std::vector<sr::Value>) { bgm->Pause(); },
      sb::vararg);
  bgm.def(
      "resume", [](SiglusBgm* bgm, std::vector<sr::Value>) { bgm->Resume(); },
      sb::vararg);
  bgm.def(
      "resume_wait",
      [](SiglusBgm* bgm, sr::VM& vm, std::vector<sr::Value>) -> sr::Value {
        bgm->Resume();
        return bgm->WaitForPlayback(vm, false, false);
      },
      sb::vararg);
  bgm.def(
      "wait",
      [](SiglusBgm* bgm, sr::VM& vm, std::vector<sr::Value>) -> sr::Value {
        return bgm->WaitForPlayback(vm, false, false);
      },
      sb::vararg);
  bgm.def(
      "wait_key",
      [](SiglusBgm* bgm, sr::VM& vm, std::vector<sr::Value>) -> sr::Value {
        return bgm->WaitForPlayback(vm, true, false);
      },
      sb::vararg);
  bgm.def(
      "wait_fade",
      [](SiglusBgm* bgm, sr::VM& vm, std::vector<sr::Value>) -> sr::Value {
        return bgm->WaitForPlayback(vm, false, true);
      },
      sb::vararg);
  bgm.def(
      "wait_fade_key",
      [](SiglusBgm* bgm, sr::VM& vm, std::vector<sr::Value>) -> sr::Value {
        return bgm->WaitForPlayback(vm, true, true);
      },
      sb::vararg);
  bgm.def(
      "check",
      [](SiglusBgm* bgm, std::vector<sr::Value>) -> int {
        return bgm->Check();
      },
      sb::vararg);
  bgm.def(
      "set_volume",
      [](SiglusBgm* bgm, std::vector<sr::Value> args) {
        bgm->SetVolume(
            BgmSetVolumeParams::ParseFrom(std::move(args), bgm->volume_));
      },
      sb::vararg);
  bgm.def(
      "set_volume_max",
      [](SiglusBgm* bgm, std::vector<sr::Value> args) {
        if (args.empty())
          return;
        const int value = AsInt(args[0]).value_or(bgm->volume_max_);
        bgm->volume_max_ = value;
      },
      sb::vararg);
  bgm.def(
      "set_volume_min",
      [](SiglusBgm* bgm, std::vector<sr::Value> args) {
        if (args.empty())
          return;
        const int value = AsInt(args[0]).value_or(bgm->volume_max_);
        bgm->volume_min_ = value;
      },
      sb::vararg);
  bgm.def(
      "get_volume",
      [](SiglusBgm* bgm, std::vector<sr::Value>) -> int {
        return bgm->volume_;
      },
      sb::vararg);
  bgm.def(
      "get_regist_name",
      [](SiglusBgm* bgm, std::vector<sr::Value>) -> std::string {
        return bgm->GetRegistName();
      },
      sb::vararg);
  bgm.def(
      "get_play_pos",
      [](SiglusBgm* bgm, std::vector<sr::Value>) -> int {
        return bgm->GetPlayPos();
      },
      sb::vararg);

  sb::class_<SiglusPcmch> pcmch(m, "__SiglusPcmch");
  pcmch.def(sb::init([sys = runtime.system.get()](int channel) -> SiglusPcmch* {
              return new SiglusPcmch(sys, channel);
            }),
            sb::arg("channel"));
  pcmch.def(
      "play",
      [](SiglusPcmch* pcmch, std::vector<sr::Value> args) {
        pcmch->Play(PcmPlayParams::ParseFrom(std::move(args), false, false));
      },
      sb::vararg);
  pcmch.def(
      "play_loop",
      [](SiglusPcmch* pcmch, std::vector<sr::Value> args) {
        pcmch->Play(PcmPlayParams::ParseFrom(std::move(args), true, false));
      },
      sb::vararg);
  pcmch.def(
      "play_wait",
      [](SiglusPcmch* pcmch, sr::VM& vm,
         std::vector<sr::Value> args) -> sr::Value {
        pcmch->Play(PcmPlayParams::ParseFrom(std::move(args), false, false));
        return pcmch->WaitForPlayback(vm, false, false);
      },
      sb::vararg);
  pcmch.def(
      "ready",
      [](SiglusPcmch* pcmch, std::vector<sr::Value> args) {
        pcmch->Play(PcmPlayParams::ParseFrom(std::move(args), false, true));
      },
      sb::vararg);
  pcmch.def(
      "ready_loop",
      [](SiglusPcmch* pcmch, std::vector<sr::Value> args) {
        pcmch->Play(PcmPlayParams::ParseFrom(std::move(args), true, true));
      },
      sb::vararg);
  pcmch.def(
      "stop",
      [](SiglusPcmch* pcmch, std::vector<sr::Value> args) {
        const int fade_ms = args.empty() ? 0 : AsInt(args[0]).value_or(0);
        pcmch->Stop(fade_ms);
      },
      sb::vararg);
  pcmch.def(
      "pause",
      [](SiglusPcmch* pcmch, std::vector<sr::Value>) { pcmch->Pause(); },
      sb::vararg);
  pcmch.def(
      "resume",
      [](SiglusPcmch* pcmch, std::vector<sr::Value> args) {
        const int fade_ms = args.empty() ? 0 : AsInt(args[0]).value_or(0);
        pcmch->Resume(fade_ms);
      },
      sb::vararg);
  pcmch.def(
      "resume_wait",
      [](SiglusPcmch* pcmch, sr::VM& vm,
         std::vector<sr::Value> args) -> sr::Value {
        const int fade_ms = args.empty() ? 0 : AsInt(args[0]).value_or(0);
        return pcmch->ResumeWait(vm, fade_ms);
      },
      sb::vararg);
  pcmch.def(
      "wait",
      [](SiglusPcmch* pcmch, sr::VM& vm, std::vector<sr::Value>) -> sr::Value {
        return pcmch->WaitForPlayback(vm, false, false);
      },
      sb::vararg);
  pcmch.def(
      "wait_key",
      [](SiglusPcmch* pcmch, sr::VM& vm, std::vector<sr::Value>) -> sr::Value {
        return pcmch->WaitForPlayback(vm, true, false);
      },
      sb::vararg);
  pcmch.def(
      "wait_fade",
      [](SiglusPcmch* pcmch, sr::VM& vm, std::vector<sr::Value>) -> sr::Value {
        return pcmch->WaitForPlayback(vm, false, true);
      },
      sb::vararg);
  pcmch.def(
      "wait_fade_key",
      [](SiglusPcmch* pcmch, sr::VM& vm, std::vector<sr::Value>) -> sr::Value {
        return pcmch->WaitForPlayback(vm, true, true);
      },
      sb::vararg);
  pcmch.def(
      "check",
      [](SiglusPcmch* pcmch, std::vector<sr::Value>) -> int {
        return pcmch->Check();
      },
      sb::vararg);
  pcmch.def(
      "set_volume",
      [](SiglusPcmch* pcmch, std::vector<sr::Value> args) {
        pcmch->SetVolume(
            PcmVolumeParams::ParseFrom(std::move(args), pcmch->volume_));
      },
      sb::vararg);
  pcmch.def(
      "set_vol_max",
      [](SiglusPcmch* pcmch, std::vector<sr::Value> args) {
        const int fade_ms = args.empty() ? 0 : AsInt(args[0]).value_or(0);
        pcmch->SetVolume(pcmch->kVolumeMax, fade_ms);
      },
      sb::vararg);
  pcmch.def(
      "set_vol_min",
      [](SiglusPcmch* pcmch, std::vector<sr::Value> args) {
        const int fade_ms = args.empty() ? 0 : AsInt(args[0]).value_or(0);
        pcmch->SetVolume(pcmch->kVolumeMin, fade_ms);
      },
      sb::vararg);
  pcmch.def(
      "get_volume",
      [](SiglusPcmch* pcmch, std::vector<sr::Value>) -> int {
        return pcmch->volume_;
      },
      sb::vararg);

  std::string src =
      std::format(kIndexedFactory, "__SiglusPcmchList", "__SiglusPcmch");
  src += "pcmch_list = __SiglusPcmchList();";
  Execute(vm, std::move(src));
}

RLVM_REGISTER(SiglusBindingRegistry, "sound", BindSound)

}  // namespace libsiglus::binding
