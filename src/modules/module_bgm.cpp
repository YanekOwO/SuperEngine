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

#include "modules/module_bgm.hpp"

#include <functional>
#include <string>

#include "long_operations/wait_long_operation.hpp"
#include "machine/general_operations.hpp"
#include "machine/long_operation.hpp"
#include "machine/rlmachine.hpp"
#include "machine/rloperation.hpp"
#include "machine/rloperation/default_value_t.hpp"
#include "machine/rloperation/rlop_store.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"

namespace {

bool BgmWait(RLMachine& machine) {
  return !machine.GetSystem().sound().BgmPlaying();
}

std::shared_ptr<LongOperation> MakeBgmWait(RLMachine& machine) {
  auto wait_op = std::make_shared<WaitLongOperation>(machine);
  wait_op->BreakOnEvent(std::bind(BgmWait, std::ref(machine)));
  return wait_op;
}

struct LongOp_bgmWait : public LongOperation {
  bool operator()(RLMachine& machine) override {
    return !machine.GetSystem().sound().BgmPlaying();
  }
};

struct bgmLoop_0 : public RLOpcode<StrConstant_T> {
  void operator()(RLMachine& machine, string filename) {
    machine.GetSystem().sound().BgmPlay(filename, true);
  }
};

struct bgmLoop_1 : public RLOpcode<StrConstant_T, IntConstant_T> {
  void operator()(RLMachine& machine, string filename, int fadein) {
    machine.GetSystem().sound().BgmPlay(filename, true, fadein);
  }
};

struct bgmLoop_2
    : public RLOpcode<StrConstant_T, IntConstant_T, IntConstant_T> {
  void operator()(RLMachine& machine,
                  string filename,
                  int fadein,
                  int fadeout) {
    machine.GetSystem().sound().BgmPlay(filename, true, fadein, fadeout);
  }
};

struct bgmPlay_0 : public RLOpcode<StrConstant_T> {
  void operator()(RLMachine& machine, string filename) {
    machine.GetSystem().sound().BgmPlay(filename, false);
  }
};

struct bgmPlay_1 : public RLOpcode<StrConstant_T, IntConstant_T> {
  void operator()(RLMachine& machine, string filename, int fadein) {
    machine.GetSystem().sound().BgmPlay(filename, false, fadein);
  }
};

struct bgmPlay_2
    : public RLOpcode<StrConstant_T, IntConstant_T, IntConstant_T> {
  void operator()(RLMachine& machine,
                  string filename,
                  int fadein,
                  int fadeout) {
    machine.GetSystem().sound().BgmPlay(filename, false, fadein, fadeout);
  }
};

struct bgmWait : public RLOpcode<> {
  void operator()(RLMachine& machine) {
    machine.PushLongOperation(MakeBgmWait(machine));
  }
};

struct bgmPlaying : public RLStoreOpcode<> {
  int operator()(RLMachine& machine) {
    return machine.GetSystem().sound().BgmPlaying() ;
  }
};

struct bgmSetVolume_0 : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int vol) {
    machine.GetSystem().sound().SetBgmVolumeScript(vol, 0);
  }
};

struct bgmGetVolume : public RLStoreOpcode<> {
  int operator()(RLMachine& machine) {
    return machine.GetSystem().sound().GetBgmVolumeScript();
  }
};

struct bgmFadeOutEx : public RLOpcode<DefaultIntValue_T<1000>> {
  void operator()(RLMachine& machine, int fadeout) {
    machine.GetSystem().sound().BgmFadeOut(fadeout);
    machine.PushLongOperation(MakeBgmWait(machine));
  }
};

struct bgmUnMute_1 : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int fadein) {
    machine.GetSystem().sound().SetBgmVolumeScript(255, fadein);
  }
};

struct bgmMute_1 : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int fadein) {
    machine.GetSystem().sound().SetBgmVolumeScript(0, fadein);
  }
};

struct bgmTimer : public RLStoreOpcode<> {
  int operator()(RLMachine& machine) {
    auto bgmPlayer = machine.GetSystem().sound().GetBgm();
    if (!bgmPlayer)
      return 0;
    return bgmPlayer->GetCurrentTime();
  }
};

}  // namespace

BgmModule::BgmModule() : RLModule("Bgm", 1, 20) {
  // fun \\([A-Za-z0-9]+\\) +<1:Bgm:\\([0-9]+\\), \\([0-9]+\\).*$
  AddOpcode(0, 0, "bgmLoop", new bgmLoop_0);
  AddOpcode(0, 1, "bgmLoop", new bgmLoop_1);
  AddOpcode(0, 2, "bgmLoop", new bgmLoop_2);

  AddUnsupportedOpcode(1, 0, "bgmPlayEx");
  AddUnsupportedOpcode(1, 1, "bgmPlayEx");
  AddUnsupportedOpcode(1, 2, "bgmPlayEx");

  AddOpcode(2, 0, "bgmPlay", new bgmPlay_0);
  AddOpcode(2, 1, "bgmPlay", new bgmPlay_1);
  AddOpcode(2, 2, "bgmPlay", new bgmPlay_2);

  AddOpcode(3, 0, "bgmWait", new bgmWait);
  AddOpcode(4, 0, "bgmPlaying", new bgmPlaying);
  AddOpcode(5, 0, "bgmStop", CallFunction(&SoundSystem::BgmStop));
  AddOpcode(6, 0, "bgmStop2", CallFunction(&SoundSystem::BgmStop));
  AddOpcode(7, 0, "bgmStatus", ReturnIntValue(&SoundSystem::BgmPlaying));
  AddUnsupportedOpcode(8, 0, "bgmRewind");
  AddOpcode(9, 0, "bgmPause", CallFunction(&SoundSystem::BgmPause));
  AddOpcode(10, 0, "bgmUnPause", CallFunction(&SoundSystem::BgmUnPause));
  AddOpcode(11, 0, "bgmVolume", new bgmGetVolume);

  AddOpcode(12, 0, "bgmSetVolume", new bgmSetVolume_0);
  AddOpcode(12, 1, "bgmSetVolume",
            CallFunction(&SoundSystem::SetBgmVolumeScript));
  AddOpcode(13, 0, "bgmUnMute",
            CallFunctionWith(&SoundSystem::SetBgmVolumeScript, 255, 0));
  AddOpcode(13, 1, "bgmUnMute", new bgmUnMute_1);
  AddOpcode(14, 0, "bgmMute",
            CallFunctionWith(&SoundSystem::SetBgmVolumeScript, 0, 0));
  AddOpcode(14, 1, "bgmMute", new bgmMute_1);

  AddOpcode(105, 0, "bgmFadeOut", CallFunction(&SoundSystem::BgmFadeOut));

  AddOpcode(106, 0, "bgmFadeOutEx", new bgmFadeOutEx);
  AddOpcode(106, 1, "bgmFadeOutEx", new bgmFadeOutEx);

  AddOpcode(200, 0, "bgmTimer", new bgmTimer);

  AddUnsupportedOpcode(107, 0, "bgmStatus2");
}
