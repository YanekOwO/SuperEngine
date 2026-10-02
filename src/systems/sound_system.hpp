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

#pragma once

#include <boost/serialization/access.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/split_member.hpp>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "core/audio_player.hpp"
#include "core/audio_table.hpp"
#include "core/sound_settings.hpp"
#include "core/voice_factory.hpp"
#include "systems/isound_system.hpp"

class Gameexe;
class IAssetScanner;
class System;

constexpr int NUM_BASE_CHANNELS = 16;
constexpr int NUM_EXTRA_WAVPLAY_CHANNELS = 8;
constexpr int NUM_KOE_CHANNELS = 1;
constexpr int NUM_TOTAL_CHANNELS =
    NUM_BASE_CHANNELS + NUM_EXTRA_WAVPLAY_CHANNELS + NUM_KOE_CHANNELS;

// The koe channel is the last one.
[[maybe_unused]] constexpr int KOE_CHANNEL =
    NUM_BASE_CHANNELS + NUM_EXTRA_WAVPLAY_CHANNELS;

// -----------------------------------------------------------------------

class SoundSystem {
 public:
  explicit SoundSystem(System& system,
                       std::unique_ptr<ISoundSystem> impl = nullptr);
  ~SoundSystem();

  void ExecuteSoundSystem();

  rlSoundSettings const& GetSettings() const;
  void SetSettings(const rlSoundSettings& settings);

  void SetUseKoeForCharacter(const int usekoe_id, const int enabled);
  int ShouldUseKoeForCharacter(const int usekoe_id) const;

  void SetBgmEnabled(const int in);
  void SetBgmVolumeMod(const int in);
  void SetBgmVolumeScript(const int level, const int fade_in_ms);

  player_t GetBgm() const;

  int BgmStatus() const;

  void BgmPlay(const std::string& bgm_name, bool loop);
  void BgmPlay(const std::string& bgm_name, bool loop, int fade_in_ms);
  void BgmPlay(const std::string& bgm_name,
               bool loop,
               int fade_in_ms,
               int fade_out_ms);
  void BgmStop();
  void BgmPause();
  void BgmUnPause();
  void BgmFadeOut(int fade_out_ms);

  std::string GetBgmName() const;
  bool BgmLooping() const;

  void SetChannelVolume(const int channel, const int level);
  void SetChannelVolume(const int channel,
                        const int level,
                        const int fade_time_in_ms);

  int GetChannelVolume(const int channel) const;

  void WavPlay(const std::string& wav_file, bool loop);
  void WavPlay(const std::string& wav_file, bool loop, const int channel);
  void WavPlay(const std::string& wav_file,
               bool loop,
               const int channel,
               const int fadein_ms);
  bool WavPlaying(const int channel);
  void WavStop(const int channel);
  void WavStopAll();
  void WavFadeOut(const int channel, const int fadetime);

  int IsSeEnabled() const;
  void SetIsSeEnabled(const int in);

  int GetSeVolumeMod() const;
  void SetSeVolumeMod(const int in);

  void PlaySe(const int se_num);

  bool HasSe(const int se_num);

  int GetKoeVolume() const;
  void SetKoeVolume(const int level, const int fadetime);

  void KoePlay(int id);
  void KoePlay(int id, int charid);

  bool KoePlaying() const;
  void KoeStop();

  void PlayMovieAudio(const std::filesystem::path& path, int volume = 255);
  void StopMovieAudio();
  bool MovieAudioPlaying() const;
  int MovieAudioTimeMs() const;

  void Reset();

  System& system();

 private:
  void KoePlayImpl(int id);

  // Implementation to play a wave file. Two wavPlay() versions use this
  // underlying implementation, which is split out so the one that takes a raw
  // channel can verify its input.
  //
  // Both NUM_BASE_CHANNELS and NUM_EXTRA_WAVPLAY_CHANNELS are legal inputs for
  // |channel|.
  void WavPlayImpl(const std::string& wav_file, const int channel, bool loop);

  // Computes and passes a volume to the audio backend for |channel|.
  void SetChannelVolumeImpl(int channel);

  // Creates a player object from a name. Throws if the bgm isn't found.
  player_t LoadMusic(const std::string& bgm_name);

  // Computes the actual volume for a channel based on the per channel
  // and the per system volume.
  int compute_channel_volume(const int channel_volume,
                             const int system_volume) {
    return (channel_volume * system_volume) / 255;
  }

  // Stores data about an ongoing volume adjustment (such as those started by
  // fun wavSetVolume(int, int, int).)
  struct VolumeAdjustTask {
    VolumeAdjustTask(unsigned int current_time,
                     int in_start_volume,
                     int in_final_volume,
                     int fade_time_in_ms);

    unsigned int start_time;
    unsigned int end_time;

    int start_volume;
    int final_volume;

    // Calculate the volume for in_time
    int calculateVolumeFor(unsigned int in_time);
  };

  typedef std::map<int, VolumeAdjustTask> ChannelAdjustmentMap;

  System& system_;

  rlSoundSettings settings_;

  AudioTable audio_table_;

  // Per channel volume
  unsigned char channel_volume_[NUM_TOTAL_CHANNELS];

  // Open tasks that adjust the volume of a wave channel. We do this
  // because the audio backend doesn't provide this functionality and
  // I'm guessing other mixers don't either.
  ChannelAdjustmentMap pcm_adjustment_tasks_;

  std::unique_ptr<VolumeAdjustTask> bgm_adjustment_task_;

  // Maps each UseKoe id to one or more koePlay ids.
  std::multimap<int, int> usekoe_to_koeplay_mapping_;

  std::shared_ptr<IAssetScanner> voice_assets_;

  VoiceFactory voice_factory_;

  AVSpec sound_quality_;

  // The bridge to sdl sound implementor
  std::unique_ptr<ISoundSystem> sound_impl_;

  // boost::serialization support
  friend class boost::serialization::access;

  template <class Archive>
  void save(Archive& ar, const unsigned int file_version) const;

  template <class Archive>
  void load(Archive& ar, const unsigned int file_version);

  BOOST_SERIALIZATION_SPLIT_MEMBER();
};
