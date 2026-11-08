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
#include "core/serialization_context.hpp"
#include "systems/event_system.hpp"
#include "systems/system.hpp"
#include "systems/system_error.hpp"

#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
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

static const std::set<std::string> SOUNDFILETYPES{"wav", "ogg", "nwa", "owp",
                                                  "mp3"};

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
      audio_table_(system.gameexe()),
      voice_assets_(system.GetAssetScanner()),
      voice_factory_(system.GetAssetScanner()),
      sound_impl_(std::move(impl)) {
  if (sound_impl_ == nullptr)
    throw std::invalid_argument("Sound implementor is nullptr.");

  sound_impl_->InitSystem();

  /* We're going to be requesting certain things from our audio
     device, so we set them up beforehand */
  sound_quality_ = s_reallive_sound_qualities[std::clamp(
      system_.global_config().reallive.sound_quality, 0, 7)];
  static constexpr int audio_buffer_size = 4096;
  try {
    sound_impl_->OpenAudio(sound_quality_, audio_buffer_size);
    sound_impl_->AllocateChannels(NUM_TOTAL_CHANNELS + NUM_PCM_CHANNELS);
  } catch (std::runtime_error& e) {
    std::ostringstream oss;
    oss << "Couldn't initialize audio: " << e.what();
    throw SystemError(oss.str());
  }

  Gameexe& gexe = system_.gameexe();

  channel_volume_.fill(255);
  channel_categories_.fill(SoundCategory::Pcm);
  channel_categories_[KOE_CHANNEL] = SoundCategory::Voice;

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

    for (int id : koeplay_ids) {
      usekoe_to_koeplay_mapping_.emplace(usekoe_id, id);
    }
  }

  // Siglus CHRKOE preferences address menu groups. Each group lists the raw
  // character numbers whose voice gain and enable flag it controls.
  uses_character_voice_groups_ = gexe("CHRKOE.CNT").Exists();
  const int group_count =
      std::clamp(gexe("CHRKOE.CNT").Int().value_or(64), 0, 256);
  for (int group = 0; group < group_count; ++group) {
    auto source = gexe("CHRKOE", group);
    if (!source.Exists())
      source = gexe("CHRKOE." + std::to_string(group));
    if (!source.Exists())
      continue;
    uses_character_voice_groups_ = true;
    const auto values = source.ToStrVec();
    bool in_character_list = false;
    for (std::size_t i = 3; i < values.size(); ++i) {
      std::string characters = values[i];
      in_character_list |= characters.find('(') != std::string::npos;
      if (!in_character_list)
        continue;
      for (char& ch : characters)
        if (ch == '(' || ch == ')' || ch == ',')
          ch = ' ';
      std::istringstream input(characters);
      for (int character; input >> character;)
        character_voice_groups_.emplace(character, group);
    }
  }
}

SoundSystem::~SoundSystem() {
  sound_impl_->CloseAudio();
  sound_impl_->QuitSystem();
}

void SoundSystem::ExecuteSoundSystem() {
  unsigned int cur_time = system_.event().GetTicks();
  UpdateSeVolume(cur_time);

  auto it = pcm_adjustment_tasks_.begin();
  while (it != pcm_adjustment_tasks_.end()) {
    const bool complete = cur_time >= it->second.end_time;
    channel_volume_.at(it->first) =
        complete ? it->second.final_volume
                 : it->second.calculateVolumeFor(cur_time);
    SetChannelVolumeImpl(it->first);
    if (complete)
      it = pcm_adjustment_tasks_.erase(it);
    else
      ++it;
  }

  if (bgm_adjustment_task_) {
    const bool complete = cur_time >= bgm_adjustment_task_->end_time;
    bgm_script_volume_ =
        complete ? bgm_adjustment_task_->final_volume
                 : bgm_adjustment_task_->calculateVolumeFor(cur_time);
    ApplyBgmVolume();
    if (complete)
      bgm_adjustment_task_.reset();
  }
}

int SoundSystem::GetUserVolume(SoundCategory category) const {
  return system_.global_config().sound_user_volume.at(
      static_cast<std::size_t>(category));
}

void SoundSystem::SetUserVolume(SoundCategory category, int volume) {
  system_.global_config().sound_user_volume.at(
      static_cast<std::size_t>(category)) = std::clamp(volume, 0, 255);
  ApplyConfiguration();
}

bool SoundSystem::IsSoundEnabled(SoundCategory category) const {
  const auto& config = system_.global_config();
  return config.play_all_sound_check &&
         config.play_sound_check.at(static_cast<std::size_t>(category));
}

void SoundSystem::SetSoundEnabled(SoundCategory category, bool enabled) {
  system_.global_config().play_sound_check.at(
      static_cast<std::size_t>(category)) = enabled;
  ApplyConfiguration();
}

int SoundSystem::EffectiveUserVolume(SoundCategory category) const {
  return IsSoundEnabled(category)
             ? compute_channel_volume(
                   GetUserVolume(category),
                   system_.global_config().all_sound_user_volume)
             : 0;
}

void SoundSystem::ApplyBgmVolume() {
  if (auto player = sound_impl_->GetBgm())
    player->SetVolume(
        static_cast<float>(compute_channel_volume(
            bgm_script_volume_, EffectiveUserVolume(SoundCategory::Bgm))) /
        255.0f);
}

void SoundSystem::ApplyConfiguration() {
  if (IsSoundEnabled(SoundCategory::Bgm))
    sound_impl_->EnableBgm();
  else
    sound_impl_->DisableBgm();
  ApplyBgmVolume();
  for (int channel = 0; channel < NUM_TOTAL_CHANNELS; ++channel)
    SetChannelVolumeImpl(channel);
  ApplyGlobalPcmVolume();
  if (auto player = sound_impl_->GetMovieAudio())
    player->SetVolume(
        static_cast<float>(compute_channel_volume(
            movie_script_volume_, EffectiveUserVolume(SoundCategory::Movie))) /
        255.0f);
}

void SoundSystem::SetUseKoeForCharacter(const int character,
                                        const int enabled) {
  auto range = usekoe_to_koeplay_mapping_.equal_range(character);
  for (auto it = range.first; it != range.second; ++it) {
    system_.global_config().chrkoe[it->second].enable = enabled != 0;
  }
}

int SoundSystem::ShouldUseKoeForCharacter(const int character) const {
  auto it = usekoe_to_koeplay_mapping_.find(character);
  if (it != usekoe_to_koeplay_mapping_.end()) {
    int koeplay_id = it->second;

    auto koe_it = system_.global_config().chrkoe.find(koeplay_id);
    if (koe_it != system_.global_config().chrkoe.end()) {
      return koe_it->second.enable;
    }
  }

  // Default to true
  return 1;
}

void SoundSystem::SetBgmEnabled(const int in) {
  SetSoundEnabled(SoundCategory::Bgm, in != 0);
}

void SoundSystem::SetBgmVolumeMod(const int in) {
  SetUserVolume(SoundCategory::Bgm, in);
}

void SoundSystem::SetBgmVolumeScript(const int level, int fade_in_ms) {
  if (fade_in_ms <= 0) {
    bgm_adjustment_task_.reset();
    bgm_script_volume_ = std::clamp(level, 0, 255);
    ApplyBgmVolume();
  } else {
    const unsigned int cur_time = system_.event().GetTicks();
    bgm_adjustment_task_ = std::make_unique<VolumeAdjustTask>(
        cur_time, bgm_script_volume_, std::clamp(level, 0, 255), fade_in_ms);
  }
}

int SoundSystem::GetBgmVolumeScript() const {
  return bgm_adjustment_task_ ? bgm_adjustment_task_->final_volume
                              : bgm_script_volume_;
}

player_t SoundSystem::GetBgm() const { return sound_impl_->GetBgm(); }

bool SoundSystem::BgmPlaying() const {
  auto player = sound_impl_->GetBgm();
  if (player && player->GetStatus() == AudioPlayer::STATUS::PLAYING)
    return true;
  return false;
}

SoundSystem::PlaybackStatus SoundSystem::GetBgmStatus() const {
  if (auto player = GetBgm()) {
    if (player->GetStatus() == AudioPlayer::STATUS::PAUSED)
      return PlaybackStatus::Paused;
    if (BgmPlaying())
      return PlaybackStatus::Playing;
  }
  return PlaybackStatus::Stopped;
}

bool SoundSystem::BgmFadingOut() const {
  return bgm_state_.fading_out && BgmPlaying();
}

void SoundSystem::BgmPlay(const std::string& name, bool loop) {
  BgmPlay(name, loop, 0);
}

void SoundSystem::BgmPlay(const std::string& name, bool loop, int fade_in_ms) {
  if (boost::iequals(bgm_state_.name, name) && BgmPlaying() &&
      !bgm_state_.fading_out && bgm_state_.loop == loop)
    return;
  auto player = LoadMusic(name);
  player->SetLoopTimes(loop ? -1 : 0);
  player->FadeIn(fade_in_ms);
  sound_impl_->PlayBgm(std::move(player));
  bgm_state_ = {.name = name, .loop = loop, .status = PlaybackStatus::Playing};
}

void SoundSystem::BgmPlay(const std::string& name,
                          bool loop,
                          int fade_in_ms,
                          int fade_out_ms) {
  // Crossfading the previous track is not implemented by the backend.
  BgmPlay(name, loop, fade_in_ms);
}

void SoundSystem::BgmPrepare(const std::string& name, bool loop) {
  auto player = LoadMusic(name);
  player->SetLoopTimes(loop ? -1 : 0);
  player->Pause();
  sound_impl_->PlayBgm(std::move(player));
  bgm_state_ = {.name = name,
                .loop = loop,
                .prepared = true,
                .status = PlaybackStatus::Paused};
}

void SoundSystem::BgmStop() {
  if (auto player = GetBgm())
    player->Terminate();
  bgm_state_ = {};
}

void SoundSystem::BgmPause() {
  if (auto player = GetBgm(); player && BgmPlaying()) {
    player->Pause();
    bgm_state_.status = PlaybackStatus::Paused;
    bgm_state_.fading_out = false;
  }
}

void SoundSystem::BgmUnPause() {
  if (auto player = GetBgm();
      player && player->GetStatus() == AudioPlayer::STATUS::PAUSED) {
    player->Unpause();
    bgm_state_.prepared = false;
    bgm_state_.status = PlaybackStatus::Playing;
    bgm_state_.fading_out = false;
  }
}

void SoundSystem::BgmFadeOut(int fade_out_ms) {
  if (fade_out_ms <= 0) {
    BgmStop();
    return;
  }
  if (auto player = GetBgm(); player && BgmPlaying()) {
    player->FadeOut(fade_out_ms);
    bgm_state_.fading_out = true;
  }
}

std::string SoundSystem::GetBgmName() const { return bgm_state_.name; }

bool SoundSystem::BgmLooping() const { return bgm_state_.loop; }

void SoundSystem::SetChannelVolume(const int channel, const int level) {
  pcm_adjustment_tasks_.erase(channel);
  channel_volume_.at(channel) = std::clamp(level, 0, 255);
  SetChannelVolumeImpl(channel);
}

void SoundSystem::SetChannelVolume(const int channel,
                                   const int level,
                                   const int fade_time_in_ms) {
  if (fade_time_in_ms <= 0) {
    SetChannelVolume(channel, level);
    return;
  }
  unsigned int cur_time = system_.event().GetTicks();
  pcm_adjustment_tasks_.insert_or_assign(
      channel, VolumeAdjustTask(cur_time, channel_volume_.at(channel),
                                std::clamp(level, 0, 255), fade_time_in_ms));
}

int SoundSystem::GetChannelVolume(const int channel) const {
  if (auto it = pcm_adjustment_tasks_.find(channel);
      it != pcm_adjustment_tasks_.end())
    return it->second.final_volume;
  return channel_volume_.at(channel);
}

void SoundSystem::WavPlay(const std::string& wav_file, bool loop) {
  int channel_number = -1;

  try {
    channel_number = FindIdleChannel();
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

void SoundSystem::WavPlay(const std::string& name,
                          bool loop,
                          const int channel,
                          const int fadein_ms) {
  auto& state = channel_state_.at(channel);
  if (!IsSoundEnabled(SoundCategory::Pcm))
    return;
  auto player = LoadWave(name, loop);
  player->FadeIn(fadein_ms);
  channel_categories_.at(channel) = SoundCategory::Pcm;
  se_fading_.at(channel) = false;
  SetChannelVolumeImpl(channel);
  sound_impl_->PlayChannel(channel, std::move(player));
  state = {.name = name, .loop = loop, .status = PlaybackStatus::Playing};
}

void SoundSystem::WavPrepare(const std::string& name, bool loop, int channel) {
  auto& state = channel_state_.at(channel);
  sound_impl_->HaltChannel(channel);
  state = {.name = name, .loop = loop, .prepared = true};
  channel_categories_.at(channel) = SoundCategory::Pcm;
  se_fading_.at(channel) = false;
}

void SoundSystem::WavPause(int channel) {
  auto& state = channel_state_.at(channel);
  if (state.name.empty() ||
      (!state.prepared && GetWavStatus(channel) == PlaybackStatus::Stopped))
    return;
  sound_impl_->HaltChannel(channel);
  state.status = PlaybackStatus::Paused;
  state.fading_out = false;
}

void SoundSystem::WavResume(int channel, int fade_in_ms) {
  const auto state = channel_state_.at(channel);
  if (!state.name.empty())
    WavPlay(state.name, state.loop, channel, fade_in_ms);
}

bool SoundSystem::WavPlaying(const int channel) const {
  return GetWavStatus(channel) == PlaybackStatus::Playing;
}

SoundSystem::PlaybackStatus SoundSystem::GetWavStatus(int channel) const {
  const auto& state = channel_state_.at(channel);
  if (state.status == PlaybackStatus::Paused)
    return PlaybackStatus::Paused;
  return sound_impl_->IsPlaying(channel) ? PlaybackStatus::Playing
                                         : PlaybackStatus::Stopped;
}

bool SoundSystem::WavFadingOut(int channel) const {
  return channel_state_.at(channel).fading_out && WavPlaying(channel);
}

void SoundSystem::WavStop(const int channel) {
  auto& state = channel_state_.at(channel);
  sound_impl_->HaltChannel(channel);
  state = {};
  se_fading_.at(channel) = false;
}

void SoundSystem::WavStopAll() {
  sound_impl_->HaltAllChannels();
  channel_state_.fill({});
  se_fading_.fill(false);
}

void SoundSystem::WavFadeOut(const int channel, const int fadetime) {
  auto& state = channel_state_.at(channel);
  if (fadetime <= 0 || !WavPlaying(channel)) {
    WavStop(channel);
    return;
  }
  sound_impl_->FadeOutChannel(channel, fadetime);
  state.fading_out = true;
  state.prepared = false;
}

int SoundSystem::FindIdleChannel() const {
  // Automatic WAV and SE playback must not borrow the global PCM pool.
  for (int channel = 0; channel < NUM_TOTAL_CHANNELS; ++channel)
    if (!sound_impl_->IsPlaying(channel) && !channel_state_[channel].prepared &&
        channel_state_[channel].status != PlaybackStatus::Paused)
      return channel;
  throw std::runtime_error("All channels are busy.");
}

void SoundSystem::ApplyGlobalPcmVolume() {
  const int volume =
      realLiveVolumeToSDLMixerVolume(EffectiveUserVolume(SoundCategory::Pcm));
  for (int slot = 0; slot < NUM_PCM_CHANNELS; ++slot)
    sound_impl_->SetVolume(NUM_TOTAL_CHANNELS + slot, volume);
}

void SoundSystem::PlayPcm(const std::string& filename) {
  if (!IsSoundEnabled(SoundCategory::Pcm))
    return;

  for (int i = 0; i < NUM_PCM_CHANNELS; ++i) {
    if (!sound_impl_->IsPlaying(NUM_TOTAL_CHANNELS + next_pcm_))
      break;
    next_pcm_ = (next_pcm_ + 1) % NUM_PCM_CHANNELS;
  }

  auto file = voice_assets_->FindFile(filename, SOUNDFILETYPES);
  if (!file)
    throw file.error();
  player_t player = CreateAudioPlayer(file.value());
  player->SetLoopTimes(0);
  const int channel = NUM_TOTAL_CHANNELS + next_pcm_;
  sound_impl_->SetVolume(channel, realLiveVolumeToSDLMixerVolume(
                                      EffectiveUserVolume(SoundCategory::Pcm)));
  sound_impl_->PlayChannel(channel, std::move(player));
  next_pcm_ = (next_pcm_ + 1) % NUM_PCM_CHANNELS;
}

void SoundSystem::StopPcm() {
  for (int slot = 0; slot < NUM_PCM_CHANNELS; ++slot)
    sound_impl_->FadeOutChannel(NUM_TOTAL_CHANNELS + slot, 100);
}

int SoundSystem::IsSeEnabled() const {
  return IsSoundEnabled(SoundCategory::Se);
}

void SoundSystem::SetIsSeEnabled(const int in) {
  SetSoundEnabled(SoundCategory::Se, in != 0);
}

int SoundSystem::GetSeVolumeMod() const {
  return GetUserVolume(SoundCategory::Se);
}

void SoundSystem::SetSeVolumeMod(const int level) {
  SetUserVolume(SoundCategory::Se, level);
}

void SoundSystem::PlaySe(const int se_num) {
  if (!IsSeEnabled())
    return;

  auto se = audio_table_.FindSE(se_num);
  const std::string& file_name = se.file;
  int channel = se.channel;

  if (channel < 0)
    channel = FindIdleChannel();

  // Make sure there isn't anything playing on the current channel
  WavStop(channel);
  if (file_name == "") {
    // Just stop a channel in case of an empty file name.
    return;
  }

  auto file_path = voice_assets_->FindFile(file_name, SOUNDFILETYPES);
  if (!file_path)
    throw file_path.error();
  StartSe(CreateAudioPlayer(file_path.value()), channel);
}

void SoundSystem::PlaySeByFileName(const std::string& filename) {
  if (!IsSeEnabled())
    return;
  const int channel = FindIdleChannel();
  auto file = voice_assets_->FindFile(filename, SOUNDFILETYPES);
  if (!file)
    throw file.error();
  StartSe(CreateAudioPlayer(file.value()), channel);
}

void SoundSystem::PlaySeByKoeNo(int id) {
  if (!IsSeEnabled())
    return;
  const int channel = FindIdleChannel();
  const VoiceClip clip = voice_factory_.LoadSample(id);
  StartSe(std::make_shared<AudioPlayer>(
              AudioDecoder(clip.content, clip.format_name)),
          channel);
}

void SoundSystem::StartSe(player_t player, int channel) {
  channel_state_.at(channel) = {};
  UpdateSeVolume(system_.event().GetTicks());
  player->SetLoopTimes(0);
  channel_categories_.at(channel) = SoundCategory::Se;
  se_fading_.at(channel) = false;
  SetChannelVolumeImpl(channel);
  sound_impl_->PlayChannel(channel, std::move(player));
}

void SoundSystem::StopSe(int fade_ms) {
  for (int channel = 0; channel < NUM_TOTAL_CHANNELS; ++channel) {
    if (channel_categories_[channel] != SoundCategory::Se ||
        !sound_impl_->IsPlaying(channel))
      continue;
    se_fading_[channel] = fade_ms > 0;
    if (fade_ms > 0)
      sound_impl_->FadeOutChannel(channel, fade_ms);
    else
      sound_impl_->HaltChannel(channel);
  }
}

int SoundSystem::CheckSe() const {
  bool fading = false;
  for (int channel = 0; channel < NUM_TOTAL_CHANNELS; ++channel) {
    if (channel_categories_[channel] != SoundCategory::Se ||
        !sound_impl_->IsPlaying(channel))
      continue;
    if (!se_fading_[channel])
      return kSePlaying;
    fading = true;
  }
  return fading ? kSeFading : kSeFree;
}

void SoundSystem::ApplySeVolume() {
  for (int channel = 0; channel < NUM_TOTAL_CHANNELS; ++channel)
    if (channel_categories_[channel] == SoundCategory::Se)
      SetChannelVolumeImpl(channel);
}

void SoundSystem::UpdateSeVolume(unsigned int ticks) {
  if (!se_adjustment_task_)
    return;
  const bool complete = ticks >= se_adjustment_task_->end_time;
  se_script_volume_ = complete ? se_adjustment_task_->final_volume
                               : se_adjustment_task_->calculateVolumeFor(ticks);
  if (complete)
    se_adjustment_task_.reset();
  ApplySeVolume();
}

void SoundSystem::SetSeVolumeScript(int volume, int fade_ms) {
  const unsigned int ticks = system_.event().GetTicks();
  UpdateSeVolume(ticks);
  se_target_volume_ = volume;
  const int target = std::clamp(volume, 0, 255);
  if (fade_ms <= 0) {
    se_adjustment_task_.reset();
    se_script_volume_ = target;
  } else {
    se_adjustment_task_ = std::make_unique<VolumeAdjustTask>(
        ticks, se_script_volume_, target, fade_ms);
  }
  ApplySeVolume();
}

int SoundSystem::GetSeVolumeScript() const { return se_target_volume_; }

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

CharacterVoiceConfig SoundSystem::VoicePreference(int character) const {
  const auto& preferences = system_.global_config().chrkoe;
  CharacterVoiceConfig result;
  if (!uses_character_voice_groups_) {
    if (auto it = preferences.find(character); it != preferences.end())
      return it->second;
    return result;
  }
  const auto [first, last] = character_voice_groups_.equal_range(character);
  for (auto it = first; it != last; ++it) {
    if (auto config = preferences.find(it->second);
        config != preferences.end()) {
      result.enable &= config->second.enable;
      result.volume =
          compute_channel_volume(result.volume, config->second.volume);
    }
  }
  return result;
}

void SoundSystem::KoePlay(int id, int charid) {
  if (!system_.ShouldFastForward()) {
    const auto preference = VoicePreference(charid);
    if (preference.enable)
      KoePlayImpl(id, preference.volume);
  }
}

void SoundSystem::KoePlayImpl(int id, int character_volume) {
  if (!IsSoundEnabled(SoundCategory::Voice)) {
    return;
  }

  VoiceClip voice_sample = voice_factory_.LoadSample(id);
  AudioDecoder decoder(voice_sample.content, voice_sample.format_name);
  auto player = std::make_shared<AudioPlayer>(std::move(decoder));

  channel_state_[KOE_CHANNEL] = {};
  se_fading_[KOE_CHANNEL] = false;
  channel_categories_[KOE_CHANNEL] = SoundCategory::Voice;
  SetChannelVolumeImpl(KOE_CHANNEL);
  player->SetVolume(static_cast<float>(std::clamp(character_volume, 0, 255)) /
                    255.0f);
  sound_impl_->PlayChannel(KOE_CHANNEL, player);
}

bool SoundSystem::KoePlaying() const {
  return sound_impl_->IsPlaying(KOE_CHANNEL);
}

void SoundSystem::KoeStop() { WavStop(KOE_CHANNEL); }

void SoundSystem::PlayMovieAudio(const std::filesystem::path& path,
                                 int volume) {
  if (!IsSoundEnabled(SoundCategory::Movie))
    return;
  movie_script_volume_ = std::clamp(volume, 0, 255);
  player_t player = CreateAudioPlayer(path);
  player->SetVolume(
      static_cast<float>(compute_channel_volume(
          movie_script_volume_, EffectiveUserVolume(SoundCategory::Movie))) /
      255.0f);
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
  se_fading_.fill(false);
  se_adjustment_task_.reset();
  se_script_volume_ = se_target_volume_ = 255;
  ApplySeVolume();
}

System& SoundSystem::system() { return system_; }

void SoundSystem::SetChannelVolumeImpl(int channel) {
  const auto category = channel_categories_.at(channel);
  const int base = EffectiveUserVolume(category);
  // SE uses its global script gain instead of the WAV channel gain.
  const int adjusted =
      category == SoundCategory::Se
          ? compute_channel_volume(se_script_volume_, base)
          : compute_channel_volume(channel_volume_.at(channel), base);

  sound_impl_->SetVolume(channel, realLiveVolumeToSDLMixerVolume(adjusted));
}

player_t SoundSystem::LoadMusic(const std::string& bgm_name) {
  auto track = audio_table_.FindBgm(bgm_name);
  auto file_path = voice_assets_->FindFile(track.file, SOUNDFILETYPES);
  if (!file_path)
    throw file_path.error();

  player_t player = CreateAudioPlayer(file_path.value());
  player->SetName(track.name);
  player->SetPLoop(track.from, track.to, track.loop);
  player->SetVolume(
      static_cast<float>(compute_channel_volume(
          bgm_script_volume_, EffectiveUserVolume(SoundCategory::Bgm))) /
      255.0f);
  return player;
}

player_t SoundSystem::LoadWave(const std::string& name, bool loop) {
  const auto path = voice_assets_->FindFile(name, SOUNDFILETYPES);
  if (!path)
    throw path.error();
  auto player = CreateAudioPlayer(*path);
  player->SetLoopTimes(loop ? -1 : 0);
  return player;
}

void SoundSystem::WavPlayImpl(const std::string& name, int channel, bool loop) {
  WavPlay(name, loop, channel, 0);
}

template <class Archive>
void SoundSystem::save(Archive& ar, unsigned int) const {
  auto bgm = bgm_state_;
  bgm.status = GetBgmStatus();
  if (bgm.fading_out || bgm.name.empty() ||
      (bgm.status != PlaybackStatus::Paused &&
       (bgm.status != PlaybackStatus::Playing || !bgm.loop)))
    bgm = {};

  auto pcm = channel_state_;
  auto volumes = channel_volume_;
  for (int channel = 0; channel < NUM_TOTAL_CHANNELS; ++channel) {
    auto& state = pcm[channel];
    state.status = GetWavStatus(channel);
    if (state.fading_out || state.name.empty() ||
        (!state.prepared &&
         (!state.loop || state.status == PlaybackStatus::Stopped)))
      state = {};
    volumes[channel] = GetChannelVolume(channel);
  }
  const int bgm_volume = GetBgmVolumeScript();
  const int se_volume = std::clamp(GetSeVolumeScript(), 0, 255);
  ar & bgm & pcm & volumes & bgm_volume & se_volume & movie_script_volume_;
}

template <class Archive>
void SoundSystem::load(Archive& ar, unsigned int) {
  PlaybackState bgm;
  std::array<PlaybackState, NUM_TOTAL_CHANNELS> pcm;
  std::array<int, NUM_TOTAL_CHANNELS> volumes;
  int bgm_volume, se_volume, movie_volume;
  ar & bgm & pcm & volumes & bgm_volume & se_volume & movie_volume;

  const auto valid_volume = [](int value) {
    return value >= 0 && value <= 255;
  };
  if (!std::all_of(volumes.begin(), volumes.end(), valid_volume) ||
      !valid_volume(bgm_volume) || !valid_volume(se_volume) ||
      !valid_volume(movie_volume))
    throw std::runtime_error("Invalid saved script volume");
  const auto validate = [](const PlaybackState& state, bool music) {
    if (state.status != PlaybackStatus::Stopped &&
        state.status != PlaybackStatus::Playing &&
        state.status != PlaybackStatus::Paused)
      throw std::runtime_error("Invalid saved playback state");
    if (state.name.empty()) {
      if (state.loop || state.prepared ||
          state.status != PlaybackStatus::Stopped)
        throw std::runtime_error("Saved playback requires an asset name");
    } else if (music) {
      if ((state.prepared && state.status != PlaybackStatus::Paused) ||
          (state.status != PlaybackStatus::Paused &&
           (state.status != PlaybackStatus::Playing || !state.loop)))
        throw std::runtime_error("Invalid saved music state");
    } else if ((state.prepared && state.status == PlaybackStatus::Playing) ||
               (!state.prepared &&
                (!state.loop || state.status == PlaybackStatus::Stopped))) {
      throw std::runtime_error("Invalid saved PCM state");
    }
  };
  validate(bgm, true);
  for (const auto& state : pcm)
    validate(state, false);

  player_t music;
  if (!bgm.name.empty()) {
    music = LoadMusic(bgm.name);
    music->SetLoopTimes(bgm.loop ? -1 : 0);
    if (bgm.status == PlaybackStatus::Paused)
      music->Pause();
  }
  std::array<player_t, NUM_TOTAL_CHANNELS> players;
  for (int channel = 0; channel < NUM_TOTAL_CHANNELS; ++channel) {
    const auto& state = pcm[channel];
    if (!state.name.empty())
      players[channel] = LoadWave(state.name, state.loop);
  }

  auto commit = [this, bgm = std::move(bgm), pcm = std::move(pcm), volumes,
                 bgm_volume, se_volume, movie_volume, music = std::move(music),
                 players = std::move(players)]() mutable {
    Reset();
    pcm_adjustment_tasks_.clear();
    bgm_adjustment_task_.reset();
    channel_volume_ = volumes;
    channel_categories_.fill(SoundCategory::Pcm);
    channel_categories_[KOE_CHANNEL] = SoundCategory::Voice;
    bgm_script_volume_ = bgm_volume;
    se_script_volume_ = se_target_volume_ = se_volume;
    movie_script_volume_ = movie_volume;
    next_pcm_ = 0;
    bgm_state_ = std::move(bgm);
    channel_state_ = std::move(pcm);
    if (music)
      sound_impl_->PlayBgm(std::move(music));
    for (int channel = 0; channel < NUM_TOTAL_CHANNELS; ++channel) {
      const auto& state = channel_state_[channel];
      if (players[channel] && !state.prepared &&
          state.status == PlaybackStatus::Playing) {
        channel_categories_[channel] = SoundCategory::Pcm;
        sound_impl_->PlayChannel(channel, std::move(players[channel]));
      }
    }
    ApplyConfiguration();
  };
  auto& context = SerializationContext::Get(ar);
  if (context.defer_commit)
    context.defer_commit(std::move(commit));
  else
    commit();
}

template void SoundSystem::save<boost::archive::text_oarchive>(
    boost::archive::text_oarchive&,
    unsigned int) const;
template void SoundSystem::load<boost::archive::text_iarchive>(
    boost::archive::text_iarchive&,
    unsigned int);
template void SoundSystem::save<boost::archive::binary_oarchive>(
    boost::archive::binary_oarchive&,
    unsigned int) const;
template void SoundSystem::load<boost::archive::binary_iarchive>(
    boost::archive::binary_iarchive&,
    unsigned int);
