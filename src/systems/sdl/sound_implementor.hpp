// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
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

#pragma once

#include "systems/isound_system.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class SDLSoundImpl : public ISoundSystem {
 public:
  SDLSoundImpl();
  ~SDLSoundImpl();

  virtual void InitSystem() const override;
  virtual void QuitSystem() const override;

  virtual void AllocateChannels(int num) const override;
  virtual void OpenAudio(AVSpec spec, int buffer_size = 2048) const override;
  virtual void CloseAudio() const override;

  virtual int FindIdleChannel() const override;
  virtual void SetVolume(int channel, int vol) const override;
  virtual bool IsPlaying(int channel) const override;
  virtual int PlayChannel(int channel, player_t audio) override;
  virtual int FadeOutChannel(int channel, int fadetime) const override;
  virtual void HaltChannel(int channel) const override;
  virtual void HaltAllChannels() const override;

  virtual void PlayBgm(player_t audio) override;
  virtual player_t GetBgm() const override;
  virtual void EnableBgm() override;
  virtual void DisableBgm() override;

  virtual void PlayMovieAudio(player_t audio) override;
  virtual player_t GetMovieAudio() const override;
  virtual void StopMovieAudio() override;

  SDL_AudioFormat ToSDLSoundFormat(AV_SAMPLE_FMT fmt) const;
  AV_SAMPLE_FMT FromSDLSoundFormat(SDL_AudioFormat fmt) const;

 private:
  const char* GetError() const;

  static std::vector<uint8_t> RenderChunk(player_t audio);
  static void PumpPlayer(player_t& player,
                         bool enabled,
                         SDL_AudioStream* stream,
                         int additional);

  static void SDLCALL OnChannelData(void* userdata,
                                    SDL_AudioStream* stream,
                                    int additional,
                                    int total);
  static void SDLCALL OnBgmData(void* userdata,
                                SDL_AudioStream* stream,
                                int additional,
                                int total);
  static void SDLCALL OnMovieData(void* userdata,
                                  SDL_AudioStream* stream,
                                  int additional,
                                  int total);

  struct ChannelInfo {
    player_t player;
    SDL_AudioStream* stream = nullptr;
    float base_gain = 1.0f;
    uint64_t fade_start = 0;
    uint32_t fade_ms = 0;  // 0 means that no fade is active.

    bool IsIdle() const;
    void Reset();
  };

  static std::vector<ChannelInfo> ch_;
  static player_t bgm_player_;
  static player_t movie_player_;
  static std::atomic<bool> bgm_enabled_;
  static AVSpec spec_;
  static SDL_AudioDeviceID device_;
  static SDL_AudioStream* bgm_stream_;
  static SDL_AudioStream* movie_stream_;
};
