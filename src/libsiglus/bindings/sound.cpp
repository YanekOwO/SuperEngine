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

#include "libsiglus/bindings/sound.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "vm/object.hpp"
#include "vm/serialization.hpp"

#include "core/bgm_table.hpp"
#include "libsiglus/bindings/bootstrap.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/native_wait_serialization.hpp"
#include "long_operations/siglus_wait.hpp"
#include "srbind/module.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"
#include "vm/string.hpp"
#include "vm/vm.hpp"

#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace libsiglus::binding {
namespace sr = serilang;
namespace sb = srbind;

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

void BindSound(SiglusMachine& runtime) {
  sr::VM& vm = *runtime.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());

  sb::module_ pcm(vm, "pcm");
  pcm.def("play", [sys = runtime.host().system.get()](std::string filename) {
    if (sys)
      sys->sound().PlayPcm(filename);
  });
  pcm.def("stop", [sys = runtime.host().system.get()] {
    if (sys)
      sys->sound().StopPcm();
  });

  System* sys = runtime.host().system.get();
  sb::module_ se(vm, "se");
  auto play_se = [sys](int number) {
    if (sys)
      sys->sound().PlaySe(number);
  };
  se.def("play", play_se);
  se.def("play_by_se_no", play_se);
  se.def("play_by_file_name", [sys](std::string filename) {
    if (sys)
      sys->sound().PlaySeByFileName(filename);
  });
  se.def("play_by_koe_no", [sys](int number) {
    if (sys)
      sys->sound().PlaySeByKoeNo(number);
  });
  se.def(
      "stop",
      [sys](int fade_ms) {
        if (sys)
          sys->sound().StopSe(fade_ms);
      },
      sb::arg("fade_ms") = 100);
  se.def("check",
         [sys] { return sys ? sys->sound().CheckSe() : SoundSystem::kSeFree; });
  auto wait_se = [sys, &runtime](sr::VM& vm, bool key_skip) {
    return lop::MakePollingWaitFuture(
        vm,
        [sys] {
          return !sys || sys->ShouldFastForward() ||
                 sys->sound().CheckSe() != SoundSystem::kSePlaying;
        },
        key_skip, sys ? sys->event_ptr().get() : nullptr,
        std::chrono::milliseconds(5), {},
        DescribePollingWait(
            runtime.WaitScope(),
            {.kind = PollingWaitDescription::Kind::SoundEffect}));
  };
  se.def("wait", [wait_se](sr::VM& vm) { return wait_se(vm, false); });
  se.def("wait_key", [wait_se](sr::VM& vm) { return wait_se(vm, true); });
  se.def(
      "set_volume",
      [sys](int volume, int fade_ms) {
        if (sys)
          sys->sound().SetSeVolumeScript(volume, fade_ms);
      },
      sb::arg("volume"), sb::arg("fade_ms") = 0);
  se.def(
      "set_volume_max",
      [sys](int fade_ms) {
        if (sys)
          sys->sound().SetSeVolumeScript(255, fade_ms);
      },
      sb::arg("fade_ms") = 0);
  se.def(
      "set_volume_min",
      [sys](int fade_ms) {
        if (sys)
          sys->sound().SetSeVolumeScript(0, fade_ms);
      },
      sb::arg("fade_ms") = 0);
  se.def("get_volume",
         [sys] { return sys ? sys->sound().GetSeVolumeScript() : 255; });

  if (!runtime.host().bgm_table)
    runtime.host().bgm_table = std::make_shared<BgmTable>(
        BgmTable::CreateFromSiglus(*runtime.host().gameexe));
  auto bgm_table = runtime.host().bgm_table;
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

  auto play_koe = [sys](const ExKoeCallParams& params) {
    if (!sys)
      return;
    sys->sound().KoeStop();
    if (params.character >= 0)
      sys->sound().KoePlay(params.koe, params.character);
    else
      sys->sound().KoePlay(params.koe);
  };
  auto wait_koe = [sys](sr::VM& vm, bool key_skip, lop::NativeWaitScope scope) {
    return lop::MakePollingWaitFuture(
        vm, [sys] { return !sys || !sys->sound().KoePlaying(); }, key_skip,
        sys ? sys->event_ptr().get() : nullptr, std::chrono::milliseconds(5),
        {},
        DescribePollingWait(std::move(scope),
                            {.kind = PollingWaitDescription::Kind::Voice}));
  };
  m.def(
      "exkoe",
      [play_koe](std::vector<sr::Value> args) -> int {
        play_koe(ExKoeCallParams::ParseFrom(std::move(args), false, false));
        return 0;
      },
      sb::vararg);
  m.def(
      "exkoe_play_wait",
      [play_koe, wait_koe, &runtime](sr::VM& vm,
                                     std::vector<sr::Value> args) -> sr::Value {
        const auto params =
            ExKoeCallParams::ParseFrom(std::move(args), true, false);
        play_koe(params);
        return params.wait ? wait_koe(vm, params.key_skip, runtime.WaitScope())
                           : lop::MakeResolvedFuture(*vm.gc_);
      },
      sb::vararg);
  m.def(
      "exkoe_play_wait_key",
      [play_koe, wait_koe, &runtime](sr::VM& vm,
                                     std::vector<sr::Value> args) -> sr::Value {
        const auto params =
            ExKoeCallParams::ParseFrom(std::move(args), true, true);
        play_koe(params);
        return params.wait ? wait_koe(vm, params.key_skip, runtime.WaitScope())
                           : lop::MakeResolvedFuture(*vm.gc_);
      },
      sb::vararg);
  m.def(
      "koe_stop",
      [sys](std::vector<sr::Value> args) {
        int fade_ms = ParseKoeStop(std::move(args));
        if (!sys)
          return;
        if (fade_ms > 0)
          sys->sound().WavFadeOut(KOE_CHANNEL, fade_ms);
        else
          sys->sound().KoeStop();
      },
      sb::vararg);

  auto valid_channel = [](int channel) {
    return channel >= 0 && channel < NUM_TOTAL_CHANNELS;
  };
  auto script_status = [](SoundSystem::PlaybackStatus status) {
    switch (status) {
      case SoundSystem::PlaybackStatus::Playing:
        return 1;
      case SoundSystem::PlaybackStatus::Paused:
        return 2;
      case SoundSystem::PlaybackStatus::Stopped:
        return 0;
    }
    return 0;
  };
  auto wait = [sys](sr::VM& vm, bool key_skip, bool fade_only, bool fading,
                    auto playing, lop::NativeWaitScope scope) {
    if (fade_only && !fading)
      return lop::MakeResolvedFuture(*vm.gc_);
    return lop::MakePollingWaitFuture(
        vm, [playing] { return !playing(); }, key_skip,
        sys ? sys->event_ptr().get() : nullptr, std::chrono::milliseconds(5),
        {}, std::move(scope));
  };
  auto bgm_start = [sys, bgm_table](BgmHandle*, const BgmPlayParams& params,
                                    bool loop) {
    if (!sys || !params.has_name)
      return;
    sys->sound().BgmPlay(params.registered_name, loop, params.fade_in_ms,
                         params.fade_out_ms);
    bgm_table->SetListen(params.registered_name, true, false);
  };
  auto bgm_prepare = [sys, bgm_table](BgmHandle*, const BgmPlayParams& params,
                                      bool loop) {
    if (!sys || !params.has_name)
      return;
    sys->sound().BgmPrepare(params.registered_name, loop);
    bgm_table->SetListen(params.registered_name, true, false);
  };
  auto bgm_stop = [sys](BgmHandle*, int fade_ms) {
    if (sys)
      sys->sound().BgmFadeOut(fade_ms);
  };
  auto bgm_pause = [sys](BgmHandle*) {
    if (sys)
      sys->sound().BgmPause();
  };
  auto bgm_resume = [sys](BgmHandle*) {
    if (sys)
      sys->sound().BgmUnPause();
  };
  auto bgm_check = [sys, script_status](BgmHandle*) {
    return sys ? script_status(sys->sound().GetBgmStatus()) : 0;
  };
  auto bgm_volume = [sys](BgmHandle*) {
    return sys ? sys->sound().GetBgmVolumeScript() : 255;
  };
  auto bgm_set_volume = [sys](BgmHandle*, const BgmSetVolumeParams& params) {
    if (sys && params.has_volume)
      sys->sound().SetBgmVolumeScript(params.volume, params.fade_ms);
  };
  auto bgm_name = [sys](BgmHandle*) {
    return sys ? sys->sound().GetBgmName() : std::string();
  };
  auto bgm_wait = [sys, wait](BgmHandle*, sr::VM& vm, bool key_skip,
                              bool fade_only, lop::NativeWaitScope scope) {
    return wait(
        vm, key_skip, fade_only, sys && sys->sound().BgmFadingOut(),
        [sys] {
          return sys && !sys->ShouldFastForward() && sys->sound().BgmPlaying();
        },
        DescribePollingWait(std::move(scope),
                            {.kind = PollingWaitDescription::Kind::Bgm}));
  };
  auto pcm_play = [sys, valid_channel](PcmHandle* h,
                                       const PcmPlayParams& params) {
    if (!sys || !valid_channel(h->channel) || !params.has_name)
      return;
    if (params.ready)
      sys->sound().WavPrepare(params.pcm_name, params.loop, h->channel);
    else
      sys->sound().WavPlay(params.pcm_name, params.loop, h->channel,
                           params.fade_in_ms);
  };
  auto pcm_stop = [sys, valid_channel](PcmHandle* h, int fade_ms) {
    if (sys && valid_channel(h->channel))
      sys->sound().WavFadeOut(h->channel, fade_ms);
  };
  auto pcm_pause = [sys, valid_channel](PcmHandle* h) {
    if (sys && valid_channel(h->channel))
      sys->sound().WavPause(h->channel);
  };
  auto pcm_resume = [sys, valid_channel](PcmHandle* h, int fade_ms) {
    if (sys && valid_channel(h->channel))
      sys->sound().WavResume(h->channel, fade_ms);
  };
  auto pcm_check = [sys, valid_channel, script_status](PcmHandle* h) {
    return sys && valid_channel(h->channel)
               ? script_status(sys->sound().GetWavStatus(h->channel))
               : 0;
  };
  auto pcm_volume = [sys, valid_channel](PcmHandle* h) {
    return sys && valid_channel(h->channel)
               ? sys->sound().GetChannelVolume(h->channel)
               : 255;
  };
  auto pcm_set_volume = [sys, valid_channel](PcmHandle* h,
                                             const PcmVolumeParams& params) {
    if (sys && valid_channel(h->channel) && params.has_volume)
      sys->sound().SetChannelVolume(h->channel, params.volume, params.fade_ms);
  };
  auto pcm_wait = [sys, valid_channel, wait](PcmHandle* h, sr::VM& vm,
                                             bool key_skip, bool fade_only,
                                             lop::NativeWaitScope scope) {
    const int channel = h->channel;
    return wait(
        vm, key_skip, fade_only,
        sys && valid_channel(channel) && sys->sound().WavFadingOut(channel),
        [sys, valid_channel, channel] {
          return sys && valid_channel(channel) &&
                 sys->sound().WavPlaying(channel);
        },
        DescribePollingWait(
            std::move(scope),
            {.kind = PollingWaitDescription::Kind::Pcm, .index = channel}));
  };

  sb::class_<BgmHandle> bgm_cls(m, "Bgm", false);
  RegisterBindingClass(runtime, bgm_cls);
  bgm_cls.def_serialization(
      [](const BgmHandle& h, serilang::ObjectWriter& out) {},
      [](serilang::ObjectReader& in) { return std::make_unique<BgmHandle>(); });

  auto bgm = bgm_cls.inst("bgm");
  bgm.def(
      "play",
      [bgm_start](BgmHandle* bgm, std::vector<sr::Value> args) {
        bgm_start(bgm, BgmPlayParams::ParseFrom(std::move(args)), true);
      },
      sb::vararg);
  bgm.def(
      "play_oneshot",
      [bgm_start](BgmHandle* bgm, std::vector<sr::Value> args) {
        bgm_start(bgm, BgmPlayParams::ParseFrom(std::move(args)), false);
      },
      sb::vararg);
  bgm.def(
      "play_wait",
      [bgm_start, bgm_wait, &runtime](
          BgmHandle* bgm, sr::VM& vm,
          std::vector<sr::Value> args) -> sr::Value {
        bgm_start(bgm, BgmPlayParams::ParseFrom(std::move(args)), false);
        return bgm_wait(bgm, vm, false, false, runtime.WaitScope());
      },
      sb::vararg);
  bgm.def(
      "ready",
      [bgm_prepare](BgmHandle* bgm, std::vector<sr::Value> args) {
        bgm_prepare(bgm, BgmPlayParams::ParseFrom(std::move(args)), true);
      },
      sb::vararg);
  bgm.def(
      "ready_oneshot",
      [bgm_prepare](BgmHandle* bgm, std::vector<sr::Value> args) {
        bgm_prepare(bgm, BgmPlayParams::ParseFrom(std::move(args)), false);
      },
      sb::vararg);
  bgm.def(
      "stop",
      [bgm_stop](BgmHandle* bgm, std::vector<sr::Value> args) {
        const int fade_ms = args.empty() ? 0 : AsInt(args[0]).value_or(0);
        bgm_stop(bgm, fade_ms);
      },
      sb::vararg);
  bgm.def(
      "pause",
      [bgm_pause](BgmHandle* bgm, std::vector<sr::Value>) { bgm_pause(bgm); },
      sb::vararg);
  bgm.def(
      "resume",
      [bgm_resume](BgmHandle* bgm, std::vector<sr::Value>) { bgm_resume(bgm); },
      sb::vararg);
  bgm.def(
      "resume_wait",
      [bgm_resume, bgm_wait, &runtime](BgmHandle* bgm, sr::VM& vm,
                                       std::vector<sr::Value>) -> sr::Value {
        bgm_resume(bgm);
        return bgm_wait(bgm, vm, false, false, runtime.WaitScope());
      },
      sb::vararg);
  bgm.def(
      "wait",
      [bgm_wait, &runtime](BgmHandle* bgm, sr::VM& vm,
                           std::vector<sr::Value>) -> sr::Value {
        return bgm_wait(bgm, vm, false, false, runtime.WaitScope());
      },
      sb::vararg);
  bgm.def(
      "wait_key",
      [bgm_wait, &runtime](BgmHandle* bgm, sr::VM& vm,
                           std::vector<sr::Value>) -> sr::Value {
        return bgm_wait(bgm, vm, true, false, runtime.WaitScope());
      },
      sb::vararg);
  bgm.def(
      "wait_fade",
      [bgm_wait, &runtime](BgmHandle* bgm, sr::VM& vm,
                           std::vector<sr::Value>) -> sr::Value {
        return bgm_wait(bgm, vm, false, true, runtime.WaitScope());
      },
      sb::vararg);
  bgm.def(
      "wait_fade_key",
      [bgm_wait, &runtime](BgmHandle* bgm, sr::VM& vm,
                           std::vector<sr::Value>) -> sr::Value {
        return bgm_wait(bgm, vm, true, true, runtime.WaitScope());
      },
      sb::vararg);
  bgm.def(
      "check",
      [bgm_check](BgmHandle* bgm, std::vector<sr::Value>) -> int {
        return bgm_check(bgm);
      },
      sb::vararg);
  bgm.def(
      "set_volume",
      [bgm_set_volume, bgm_volume](BgmHandle* bgm,
                                   std::vector<sr::Value> args) {
        bgm_set_volume(bgm, BgmSetVolumeParams::ParseFrom(std::move(args),
                                                          bgm_volume(bgm)));
      },
      sb::vararg);
  bgm.def(
      "set_volume_max",
      [bgm_set_volume](BgmHandle* bgm, std::vector<sr::Value> args) {
        const int fade_ms = args.empty() ? 0 : AsInt(args[0]).value_or(0);
        bgm_set_volume(bgm,
                       {.volume = 255, .fade_ms = fade_ms, .has_volume = true});
      },
      sb::vararg);
  bgm.def(
      "set_volume_min",
      [bgm_set_volume](BgmHandle* bgm, std::vector<sr::Value> args) {
        const int fade_ms = args.empty() ? 0 : AsInt(args[0]).value_or(0);
        bgm_set_volume(bgm,
                       {.volume = 0, .fade_ms = fade_ms, .has_volume = true});
      },
      sb::vararg);
  bgm.def(
      "get_volume",
      [bgm_volume](BgmHandle* bgm, std::vector<sr::Value>) -> int {
        return bgm_volume(bgm);
      },
      sb::vararg);
  bgm.def(
      "get_regist_name",
      [bgm_name](BgmHandle* bgm, std::vector<sr::Value>) -> std::string {
        return bgm_name(bgm);
      },
      sb::vararg);
  bgm.def(
      "get_play_pos",
      [sys](BgmHandle* bgm, std::vector<sr::Value>) -> int {
        auto player = sys ? sys->sound().GetBgm() : nullptr;
        return player ? static_cast<int>(player->GetCurrentTime()) : 0;
      },
      sb::vararg);

  sb::class_<PcmHandle> pcmch(m, "__SiglusPcmch");
  RegisterBindingClass(runtime, pcmch);
  pcmch.def_serialization(
      [](const PcmHandle& h, serilang::ObjectWriter& out) {
        out.Put(h.channel);
      },
      [](serilang::ObjectReader& in) {
        auto channel = in.Get<int>();
        return std::make_unique<PcmHandle>(channel);
      });

  pcmch.def(sb::init([](int channel) {
              return std::make_unique<PcmHandle>(channel);
            }),
            sb::arg("channel"));
  pcmch.def(
      "play",
      [pcm_play](PcmHandle* pcmch, std::vector<sr::Value> args) {
        pcm_play(pcmch,
                 PcmPlayParams::ParseFrom(std::move(args), false, false));
      },
      sb::vararg);
  pcmch.def(
      "play_loop",
      [pcm_play](PcmHandle* pcmch, std::vector<sr::Value> args) {
        pcm_play(pcmch, PcmPlayParams::ParseFrom(std::move(args), true, false));
      },
      sb::vararg);
  pcmch.def(
      "play_wait",
      [pcm_play, pcm_wait, &runtime](PcmHandle* pcmch, sr::VM& vm,
                                     std::vector<sr::Value> args) -> sr::Value {
        pcm_play(pcmch,
                 PcmPlayParams::ParseFrom(std::move(args), false, false));
        return pcm_wait(pcmch, vm, false, false, runtime.WaitScope());
      },
      sb::vararg);
  pcmch.def(
      "ready",
      [pcm_play](PcmHandle* pcmch, std::vector<sr::Value> args) {
        pcm_play(pcmch, PcmPlayParams::ParseFrom(std::move(args), false, true));
      },
      sb::vararg);
  pcmch.def(
      "ready_loop",
      [pcm_play](PcmHandle* pcmch, std::vector<sr::Value> args) {
        pcm_play(pcmch, PcmPlayParams::ParseFrom(std::move(args), true, true));
      },
      sb::vararg);
  pcmch.def(
      "stop",
      [pcm_stop](PcmHandle* pcmch, std::vector<sr::Value> args) {
        const int fade_ms = args.empty() ? 0 : AsInt(args[0]).value_or(0);
        pcm_stop(pcmch, fade_ms);
      },
      sb::vararg);
  pcmch.def(
      "pause",
      [pcm_pause](PcmHandle* pcmch, std::vector<sr::Value>) {
        pcm_pause(pcmch);
      },
      sb::vararg);
  pcmch.def(
      "resume",
      [pcm_resume](PcmHandle* pcmch, std::vector<sr::Value> args) {
        const int fade_ms = args.empty() ? 0 : AsInt(args[0]).value_or(0);
        pcm_resume(pcmch, fade_ms);
      },
      sb::vararg);
  pcmch.def(
      "resume_wait",
      [pcm_resume, pcm_wait, &runtime](
          PcmHandle* pcmch, sr::VM& vm,
          std::vector<sr::Value> args) -> sr::Value {
        const int fade_ms = args.empty() ? 0 : AsInt(args[0]).value_or(0);
        pcm_resume(pcmch, fade_ms);
        return pcm_wait(pcmch, vm, false, false, runtime.WaitScope());
      },
      sb::vararg);
  pcmch.def(
      "wait",
      [pcm_wait, &runtime](PcmHandle* pcmch, sr::VM& vm,
                           std::vector<sr::Value>) -> sr::Value {
        return pcm_wait(pcmch, vm, false, false, runtime.WaitScope());
      },
      sb::vararg);
  pcmch.def(
      "wait_key",
      [pcm_wait, &runtime](PcmHandle* pcmch, sr::VM& vm,
                           std::vector<sr::Value>) -> sr::Value {
        return pcm_wait(pcmch, vm, true, false, runtime.WaitScope());
      },
      sb::vararg);
  pcmch.def(
      "wait_fade",
      [pcm_wait, &runtime](PcmHandle* pcmch, sr::VM& vm,
                           std::vector<sr::Value>) -> sr::Value {
        return pcm_wait(pcmch, vm, false, true, runtime.WaitScope());
      },
      sb::vararg);
  pcmch.def(
      "wait_fade_key",
      [pcm_wait, &runtime](PcmHandle* pcmch, sr::VM& vm,
                           std::vector<sr::Value>) -> sr::Value {
        return pcm_wait(pcmch, vm, true, true, runtime.WaitScope());
      },
      sb::vararg);
  pcmch.def(
      "check",
      [pcm_check](PcmHandle* pcmch, std::vector<sr::Value>) -> int {
        return pcm_check(pcmch);
      },
      sb::vararg);
  pcmch.def(
      "set_volume",
      [pcm_set_volume, pcm_volume](PcmHandle* pcmch,
                                   std::vector<sr::Value> args) {
        pcm_set_volume(pcmch, PcmVolumeParams::ParseFrom(std::move(args),
                                                         pcm_volume(pcmch)));
      },
      sb::vararg);
  pcmch.def(
      "set_vol_max",
      [pcm_set_volume](PcmHandle* pcmch, std::vector<sr::Value> args) {
        const int fade_ms = args.empty() ? 0 : AsInt(args[0]).value_or(0);
        pcm_set_volume(pcmch,
                       {.volume = 255, .fade_ms = fade_ms, .has_volume = true});
      },
      sb::vararg);
  pcmch.def(
      "set_vol_min",
      [pcm_set_volume](PcmHandle* pcmch, std::vector<sr::Value> args) {
        const int fade_ms = args.empty() ? 0 : AsInt(args[0]).value_or(0);
        pcm_set_volume(pcmch,
                       {.volume = 0, .fade_ms = fade_ms, .has_volume = true});
      },
      sb::vararg);
  pcmch.def(
      "get_volume",
      [pcm_volume](PcmHandle* pcmch, std::vector<sr::Value>) -> int {
        return pcm_volume(pcmch);
      },
      sb::vararg);

  std::string src =
      std::format(kIndexedFactory, "__SiglusPcmchList", "__SiglusPcmch");
  src += "pcmch_list = __SiglusPcmchList();";
  Execute(vm, std::move(src));
}

RLVM_REGISTER(SiglusBindingRegistry, "sound", BindSound)

}  // namespace libsiglus::binding
