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

#include "systems/sound_system.hpp"

#include "core/avdec/audio_decoder.hpp"
#include "core/avspec.hpp"
#include "systems/event_system.hpp"
#include "systems/system.hpp"
#include "systems/system_error.hpp"

#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Changes an incoming RealLive volume (0-256) to the range SDL_Mixer expects
// (0-128).
inline static int realLiveVolumeToSDLMixerVolume(int in_vol) {
  return in_vol / 2;
}

static const std::set<std::string> SOUNDFILETYPES{"wav", "ogg", "nwa", "mp3"};

// -----------------------------------------------------------------------
// RealLive Sound Qualities table
// -----------------------------------------------------------------------

static constexpr AVSpec s_reallive_sound_qualities[] = {
    (AVSpec){11025, AV_SAMPLE_FMT::S8, 2},   // 11 k_hz, 8 bit stereo
    (AVSpec){11025, AV_SAMPLE_FMT::S16, 2},  // 11 k_hz, 16 bit stereo
    (AVSpec){22050, AV_SAMPLE_FMT::S8, 2},   // 22 k_hz, 8 bit stereo
    (AVSpec){22050, AV_SAMPLE_FMT::S16, 2},  // 22 k_hz, 16 bit stereo
    (AVSpec){44100, AV_SAMPLE_FMT::S8, 2},   // 44 k_hz, 8 bit stereo
    (AVSpec){44100, AV_SAMPLE_FMT::S16, 2},  // 44 k_hz, 16 bit stereo
    (AVSpec){48000, AV_SAMPLE_FMT::S8, 2},   // 48 k_hz, 8 bit stereo
    (AVSpec){48000, AV_SAMPLE_FMT::S16, 2}   // 48 k_hz, 16 bit stereo
};

// -----------------------------------------------------------------------
// SoundSystem::VolumeAdjustTask
// -----------------------------------------------------------------------
SoundSystem::VolumeAdjustTask::VolumeAdjustTask(unsigned int current_time,
                                                int in_start_volume,
                                                int in_final_volume,
                                                int fade_time_in_ms)
    : start_time(current_time),
      end_time(current_time + fade_time_in_ms),
      start_volume(in_start_volume),
      final_volume(in_final_volume) {}

int SoundSystem::VolumeAdjustTask::calculateVolumeFor(unsigned int in_time) {
  int end_offset = end_time - start_time;
  int cur_offset = in_time - start_time;
  double percent = static_cast<double>(cur_offset) / end_offset;
  return start_volume +
         static_cast<int>(percent * (final_volume - start_volume));
}

// -----------------------------------------------------------------------
// SoundSystem
// -----------------------------------------------------------------------
SoundSystem::SoundSystem(System& system, std::unique_ptr<ISoundSystem> impl)
    : system_(system),
      settings_(system.gameexe()),
      audio_table_(system.gameexe()),
      voice_assets_(system.GetAssetScanner()),
      voice_factory_(system.GetAssetScanner()),
      sound_impl_(std::move(impl)) {
  if (sound_impl_ == nullptr)
    throw std::invalid_argument("Sound implementor is nullptr.");

  sound_impl_->InitSystem();

  /* We're going to be requesting certain things from our audio
     device, so we set them up beforehand */
  sound_quality_ = s_reallive_sound_qualities[settings_.sound_quality];
  static constexpr int audio_buffer_size = 4096;
  try {
    sound_impl_->OpenAudio(sound_quality_, audio_buffer_size);
    sound_impl_->AllocateChannels(NUM_TOTAL_CHANNELS);
  } catch (std::runtime_error& e) {
    std::ostringstream oss;
    oss << "Couldn't initialize audio: " << e.what();
    throw SystemError(oss.str());
  }

  Gameexe& gexe = system_.gameexe();

  std::fill_n(channel_volume_, NUM_TOTAL_CHANNELS, 255);

  // Read the \#KOEONOFF entries
  for (auto koeonoff : gexe.Filter("KOEONOFF.")) {
    std::vector<std::string> keyparts = koeonoff.GetKeyParts();
    int usekoe_id = std::stoi(keyparts.at(1));

    // Find the corresponding koeplay ids.
    std::vector<int> koeplay_ids;
    const std::string& unprocessed_koeids = keyparts.at(2);
    if (unprocessed_koeids.find('(') != std::string::npos) {
      // We have a list that we need to parse out.
      std::string no_parens =
          unprocessed_koeids.substr(1, unprocessed_koeids.size() - 2);

      std::vector<std::string> string_koeplay_ids;
      boost::split(string_koeplay_ids, no_parens, boost::is_any_of(","));

      for (std::string const& string_id : string_koeplay_ids) {
        koeplay_ids.push_back(std::stoi(string_id));
      }
    } else {
      koeplay_ids.push_back(std::stoi(unprocessed_koeids));
    }

    int onoff = (keyparts.at(3) == "ON") ? 1 : 0;
    for (int id : koeplay_ids) {
      usekoe_to_koeplay_mapping_.emplace(usekoe_id, id);
      settings_.character_koe_enabled[id] = onoff;
    }
  }
}

SoundSystem::~SoundSystem() {
  sound_impl_->CloseAudio();
  sound_impl_->QuitSystem();
}

void SoundSystem::ExecuteSoundSystem() {
  unsigned int cur_time = system_.event().GetTicks();

  ChannelAdjustmentMap::iterator it = pcm_adjustment_tasks_.begin();
  while (it != pcm_adjustment_tasks_.end()) {
    if (cur_time >= it->second.end_time) {
      SetChannelVolume(it->first, it->second.final_volume);
      pcm_adjustment_tasks_.erase(it++);
    } else {
      int volume = it->second.calculateVolumeFor(cur_time);
      SetChannelVolume(it->first, volume);
      ++it;
    }
  }

  if (bgm_adjustment_task_) {
    if (cur_time >= bgm_adjustment_task_->end_time) {
      SetBgmVolumeScript(bgm_adjustment_task_->final_volume, 0);
      bgm_adjustment_task_.reset();
    } else {
      int volume = bgm_adjustment_task_->calculateVolumeFor(cur_time);
      SetBgmVolumeScript(volume, 0);
    }
  }
}

rlSoundSettings const& SoundSystem::GetSettings() const { return settings_; }

void SoundSystem::SetSettings(const rlSoundSettings& settings) {
  settings_ = settings;
}

void SoundSystem::SetUseKoeForCharacter(const int character,
                                        const int enabled) {
  auto range = usekoe_to_koeplay_mapping_.equal_range(character);
  for (auto it = range.first; it != range.second; ++it) {
    settings_.character_koe_enabled[it->second] = enabled;
  }
}

int SoundSystem::ShouldUseKoeForCharacter(const int character) const {
  auto it = usekoe_to_koeplay_mapping_.find(character);
  if (it != usekoe_to_koeplay_mapping_.end()) {
    int koeplay_id = it->second;

    auto koe_it = settings_.character_koe_enabled.find(koeplay_id);
    if (koe_it != settings_.character_koe_enabled.end()) {
      return koe_it->second;
    }
  }

  // Default to true
  return 1;
}

void SoundSystem::SetBgmEnabled(const int in) {
  if (in)
    sound_impl_->EnableBgm();
  else
    sound_impl_->DisableBgm();
  settings_.bgm_enabled = in;
}

void SoundSystem::SetBgmVolumeMod(const int in) {
  settings_.bgm_volume = in;

  player_t player = sound_impl_->GetBgm();
  if (player) {
    player->SetVolume(static_cast<float>(in) / 255.0f);
  }
}

void SoundSystem::SetBgmVolumeScript(const int level, int fade_in_ms) {
  if (fade_in_ms == 0) {
    settings_.bgm_volume = level;
  } else {
    unsigned int cur_time = system_.event().GetTicks();

    bgm_adjustment_task_ = std::make_unique<VolumeAdjustTask>(
        cur_time, settings_.bgm_volume, level, fade_in_ms);
  }

  player_t player = sound_impl_->GetBgm();
  if (player) {
    float volume =
        static_cast<float>(settings_.bgm_volume * level) / (255.0f * 255.0f);
    player->SetVolume(volume);
  }
}

player_t SoundSystem::GetBgm() const { return sound_impl_->GetBgm(); }

int SoundSystem::BgmStatus() const {
  auto player = sound_impl_->GetBgm();
  if (player && player->GetStatus() == AudioPlayer::STATUS::PLAYING)
    return 1;
  return 0;
}

void SoundSystem::BgmPlay(const std::string& bgm_name, bool loop) {
  if (!boost::iequals(GetBgmName(), bgm_name)) {
    player_t player = LoadMusic(bgm_name);
    player->SetLoopTimes(loop ? -1 : 0);
    sound_impl_->PlayBgm(player);
  }
}

void SoundSystem::BgmPlay(const std::string& bgm_name,
                          bool loop,
                          int fade_in_ms) {
  if (!boost::iequals(GetBgmName(), bgm_name)) {
    player_t player = LoadMusic(bgm_name);
    player->FadeIn(fade_in_ms);
    player->SetLoopTimes(loop ? -1 : 0);
    sound_impl_->PlayBgm(player);
  }
}

void SoundSystem::BgmPlay(const std::string& bgm_name,
                          bool loop,
                          int fade_in_ms,
                          int fade_out_ms) {
  if (!boost::iequals(GetBgmName(), bgm_name)) {
    player_t player = LoadMusic(bgm_name);
    player->FadeIn(fade_in_ms);
    player->SetLoopTimes(loop ? -1 : 0);
    // TODO: Fade out currently playing bgm
    sound_impl_->PlayBgm(player);
  }
}

void SoundSystem::BgmPrepare(const std::string& bgm_name, bool loop) {
  player_t player = LoadMusic(bgm_name);
  player->SetLoopTimes(loop ? -1 : 0);
  player->Pause();
  sound_impl_->PlayBgm(player);
}

void SoundSystem::BgmStop() {
  player_t player = sound_impl_->GetBgm();
  if (player)
    player->Terminate();
}

void SoundSystem::BgmPause() {
  player_t player = sound_impl_->GetBgm();
  if (player)
    player->Pause();
}

void SoundSystem::BgmUnPause() {
  player_t player = sound_impl_->GetBgm();
  if (player)
    player->Unpause();
}

void SoundSystem::BgmFadeOut(int fade_out_ms) {
  player_t player = sound_impl_->GetBgm();
  if (player)
    player->FadeOut(fade_out_ms);
}

std::string SoundSystem::GetBgmName() const {
  auto player = sound_impl_->GetBgm();
  if (player)
    return player->GetName();
  return "";
}

bool SoundSystem::BgmLooping() const {
  // TODO: maintain the looping status
  // this function is used for serialization
  return true;
}

void SoundSystem::SetChannelVolume(const int channel, const int level) {
  channel_volume_[channel] = level;
  SetChannelVolumeImpl(channel);
}

void SoundSystem::SetChannelVolume(const int channel,
                                   const int level,
                                   const int fade_time_in_ms) {
  unsigned int cur_time = system_.event().GetTicks();

  pcm_adjustment_tasks_.emplace(
      channel, VolumeAdjustTask(cur_time, channel_volume_[channel], level,
                                fade_time_in_ms));
}

int SoundSystem::GetChannelVolume(const int channel) const {
  return channel_volume_[channel];
}

void SoundSystem::WavPlay(const std::string& wav_file, bool loop) {
  int channel_number = -1;

  try {
    channel_number = sound_impl_->FindIdleChannel();
  } catch (std::runtime_error& e) {
    std::ostringstream oss;
    oss << "Couldn't find a free channel for wavPlay(): " << e.what();
    throw std::runtime_error(oss.str());
  }

  WavPlayImpl(wav_file, channel_number, loop);
}

void SoundSystem::WavPlay(const std::string& wav_file,
                          bool loop,
                          const int channel) {
  WavPlayImpl(wav_file, channel, loop);
}

void SoundSystem::WavPlay(const std::string& filename,
                          bool loop,
                          const int channel,
                          const int fadein_ms) {
  if (!settings_.pcm_enabled)
    return;

  if (auto wav_file = voice_assets_->FindFile(filename, SOUNDFILETYPES);
      wav_file.has_value()) {
    player_t player = CreateAudioPlayer(wav_file.value());
    player->SetLoopTimes(loop ? -1 : 0);
    player->FadeIn(fadein_ms);
    SetChannelVolumeImpl(channel);
    sound_impl_->PlayChannel(channel, player);
  } else
    throw wav_file.error();
}

bool SoundSystem::WavPlaying(const int channel) {
  return sound_impl_->IsPlaying(channel);
}

void SoundSystem::WavStop(const int channel) {
  if (settings_.pcm_enabled) {
    sound_impl_->HaltChannel(channel);
  }
}

void SoundSystem::WavStopAll() {
  if (settings_.pcm_enabled) {
    sound_impl_->HaltAllChannels();
  }
}

void SoundSystem::WavFadeOut(const int channel, const int fadetime) {
  if (settings_.pcm_enabled)
    sound_impl_->FadeOutChannel(channel, fadetime);
}

int SoundSystem::IsSeEnabled() const { return settings_.se_enabled; }

void SoundSystem::SetIsSeEnabled(const int in) { settings_.se_enabled = in; }

int SoundSystem::GetSeVolumeMod() const { return settings_.se_volume; }

void SoundSystem::SetSeVolumeMod(const int level) {
  settings_.se_volume = level;
}

void SoundSystem::PlaySe(const int se_num) {
  if (!IsSeEnabled())
    return;

  auto se = audio_table_.FindSE(se_num);
  const std::string& file_name = se.file;
  int channel = se.channel;

  if (channel < 0)
    channel = sound_impl_->FindIdleChannel();

  // Make sure there isn't anything playing on the current channel
  sound_impl_->HaltChannel(channel);
  if (file_name == "") {
    // Just stop a channel in case of an empty file name.
    return;
  }

  auto file_path = voice_assets_->FindFile(file_name, SOUNDFILETYPES);
  player_t player = CreateAudioPlayer(file_path.value());
  player->SetLoopTimes(0);

  // SE chunks have no volume other than the modifier.
  sound_impl_->SetVolume(channel,
                         realLiveVolumeToSDLMixerVolume(GetSeVolumeMod()));
  sound_impl_->PlayChannel(channel, player);
}

bool SoundSystem::HasSe(const int se_num) {
  try {
    auto se = audio_table_.FindSE(se_num);
  } catch (std::invalid_argument&) {
    return false;
  }
  return true;
}

void SoundSystem::SetKoeVolume(const int level, const int fadetime) {
  if (fadetime == 0) {
    SetChannelVolume(KOE_CHANNEL, level);
  } else {
    SetChannelVolume(KOE_CHANNEL, level, fadetime);
  }
}

int SoundSystem::GetKoeVolume() const { return GetChannelVolume(KOE_CHANNEL); }

void SoundSystem::KoePlay(int id) {
  if (!system_.ShouldFastForward())
    KoePlayImpl(id);
}

void SoundSystem::KoePlay(int id, int charid) {
  if (!system_.ShouldFastForward()) {
    bool play_voice = true;

    auto koe_it = settings_.character_koe_enabled.find(charid);
    if (koe_it != settings_.character_koe_enabled.end()) {
      play_voice = koe_it->second;
    }

    if (play_voice) {
      KoePlayImpl(id);
    }
  }
}

void SoundSystem::KoePlayImpl(int id) {
  if (!settings_.koe_enabled) {
    return;
  }

  VoiceClip voice_sample = voice_factory_.LoadSample(id);
  AudioDecoder decoder(voice_sample.content, voice_sample.format_name);
  auto player = std::make_shared<AudioPlayer>(std::move(decoder));

  SetChannelVolumeImpl(KOE_CHANNEL);
  sound_impl_->PlayChannel(KOE_CHANNEL, player);
}

bool SoundSystem::KoePlaying() const {
  return sound_impl_->IsPlaying(KOE_CHANNEL);
}

void SoundSystem::KoeStop() { sound_impl_->HaltChannel(KOE_CHANNEL); }

void SoundSystem::PlayMovieAudio(const std::filesystem::path& path,
                                 int volume) {
  player_t player = CreateAudioPlayer(path);
  player->SetVolume(static_cast<float>(std::clamp(volume, 0, 255)) / 255.0f);
  sound_impl_->PlayMovieAudio(player);
}

void SoundSystem::StopMovieAudio() { sound_impl_->StopMovieAudio(); }

bool SoundSystem::MovieAudioPlaying() const {
  player_t player = sound_impl_->GetMovieAudio();
  return player && player->GetStatus() == AudioPlayer::STATUS::PLAYING;
}

int SoundSystem::MovieAudioTimeMs() const {
  player_t player = sound_impl_->GetMovieAudio();
  if (!player)
    return 0;
  return static_cast<int>(player->GetCurrentTime());
}

void SoundSystem::Reset() {
  BgmStop();
  StopMovieAudio();
  WavStopAll();
}

System& SoundSystem::system() { return system_; }

void SoundSystem::SetChannelVolumeImpl(int channel) {
  int base =
      channel == KOE_CHANNEL ? settings_.koe_volume : settings_.pcm_volume;
  int adjusted = compute_channel_volume(GetChannelVolume(channel), base);

  sound_impl_->SetVolume(channel, realLiveVolumeToSDLMixerVolume(adjusted));
}

player_t SoundSystem::LoadMusic(const std::string& bgm_name) {
  auto track = audio_table_.FindBgm(bgm_name);
  auto file_path = voice_assets_->FindFile(track.file, SOUNDFILETYPES).value();

  player_t player = CreateAudioPlayer(file_path);
  player->SetName(track.name);
  player->SetPLoop(track.from, track.to, track.loop);
  return player;
}

void SoundSystem::WavPlayImpl(const std::string& filename,
                              const int channel,
                              bool loop) {
  if (!settings_.pcm_enabled)
    return;

  if (auto wav_file = voice_assets_->FindFile(filename, SOUNDFILETYPES);
      wav_file.has_value()) {
    player_t player = CreateAudioPlayer(wav_file.value());
    player->SetLoopTimes(loop ? -1 : 0);
    SetChannelVolumeImpl(channel);
    sound_impl_->PlayChannel(channel, player);
  } else
    throw wav_file.error();
}

template <class Archive>
void SoundSystem::save(Archive& ar, unsigned int version) const {
  std::string track_name;
  bool looping = false;

  if (BgmStatus() == 1) {
    track_name = GetBgmName();
    looping = BgmLooping();
  }

  ar & track_name & looping;
}

template <class Archive>
void SoundSystem::load(Archive& ar, unsigned int version) {
  std::string track_name;
  bool looping;
  ar & track_name & looping;

  if (track_name != "")
    BgmPlay(track_name, looping);
}

// Explicit instantiations for text archives (since we hide the implementation)
template void SoundSystem::save<boost::archive::text_oarchive>(
    boost::archive::text_oarchive& ar,
    unsigned int version) const;

template void SoundSystem::load<boost::archive::text_iarchive>(
    boost::archive::text_iarchive& ar,
    unsigned int version);
