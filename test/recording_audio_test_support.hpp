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

#pragma once

#include "systems/isound_system.hpp"

#include <cstdint>
#include <string>
#include <vector>

class ResourceAudioBackend final : public ISoundSystem {
 public:
  void InitSystem() const override {}
  void QuitSystem() const override {}
  void AllocateChannels(int count) const override { channels_.resize(count); }
  void OpenAudio(AVSpec, int) const override {}
  void CloseAudio() const override {}
  int FindIdleChannel() const override { return 0; }
  void SetVolume(int channel, int volume) const override {
    channels_.at(channel).gain = volume;
  }
  bool IsPlaying(int channel) const override {
    const auto& player = channels_.at(channel).player;
    return player && player->IsPlaying();
  }
  int PlayChannel(int channel, player_t audio) override {
    channels_.at(channel).player = std::move(audio);
    return channel;
  }
  int FadeOutChannel(int channel, int) const override {
    channels_.at(channel) = {};
    return channel;
  }
  void HaltChannel(int channel) const override { channels_.at(channel) = {}; }
  void HaltAllChannels() const override {
    for (auto& channel : channels_)
      channel = {};
  }
  void PlayBgm(player_t audio) override { bgm_ = std::move(audio); }
  player_t GetBgm() const override { return bgm_; }
  player_t ChannelPlayer(int channel) const {
    return channels_.at(channel).player;
  }
  int ChannelVolume(int channel) const { return channels_.at(channel).gain; }
  void EnableBgm() override {}
  void DisableBgm() override {}
  void PlayMovieAudio(player_t audio) override { movie_ = std::move(audio); }
  player_t GetMovieAudio() const override { return movie_; }
  void StopMovieAudio() override { movie_.reset(); }

 private:
  struct Channel {
    player_t player;
    float gain = 1;
  };
  mutable std::vector<Channel> channels_;
  player_t bgm_, movie_;
};

inline std::string ResourceWave() {
  constexpr int samples = 1024;
  std::string bytes;
  const auto word = [&](std::uint32_t value, int count) {
    for (int i = 0; i < count; ++i)
      bytes.push_back((value >> (8 * i)) & 255);
  };
  bytes += "RIFF";
  word(36 + samples * 2, 4);
  bytes += "WAVEfmt ";
  word(16, 4);
  word(1, 2);
  word(1, 2);
  word(8000, 4);
  word(16000, 4);
  word(2, 2);
  word(16, 2);
  bytes += "data";
  word(samples * 2, 4);
  for (int i = 0; i < samples; ++i)
    word(1000 + (i % 16) * 100, 2);
  return bytes;
}
