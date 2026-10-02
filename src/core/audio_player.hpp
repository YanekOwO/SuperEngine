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

#include "core/avdec/audio_decoder.hpp"
#include "core/avspec.hpp"

#include <atomic>
#include <deque>
#include <list>
#include <mutex>

class AudioPlayer {
 public:
  using time_ms_t = long long;
  using sample_count_t = long long;

  static constexpr size_t npos = std::numeric_limits<int32_t>::max();

  enum class STATUS { TERMINATED, PAUSED, PLAYING };

  AudioPlayer(AudioDecoder&& dec);

  AVSpec GetSpec() const;
  time_ms_t GetCurrentTime() const;
  AudioData LoadPCM(sample_count_t nsamples);
  AudioData LoadRemain();
  bool IsLoopingEnabled() const;
  void SetLoop(size_t ab_loop_a = 0, size_t ab_loop_b = npos);
  void SetLoopImpl(size_t fr, size_t to);
  void SetPLoop(size_t from, size_t to, size_t loop);
  void SetLoopTimes(int N);
  bool IsPlaying() const;
  STATUS GetStatus() const;
  void Terminate();
  void TerminateImpl();
  void SetName(std::string);
  std::string GetName() const;

  void FadeIn(float fadein_ms);
  void FadeOut(float fadeout_ms, bool should_then_terminate = true);
  void SetVolume(float vol);
  float GetVolume() const;
  void Pause();
  void Unpause();

  struct AudioFrame {
    AudioData ad;
    long long cur;
    size_t SampleCount() const { return ad.SampleCount(); }
  };

  class ICommand {
   public:
    virtual ~ICommand() = default;
    virtual std::string Name() const = 0;
    virtual void Execute(AudioFrame&) = 0;
    virtual bool IsFinished() = 0;
  };

 private:
  void OnEndOfPlayback();
  AudioFrame LoadNext();
  void ClipFrame(AudioFrame&) const;
  sample_count_t PcmLocation() const;
  sample_count_t TimeToSampleCount(time_ms_t time);
  time_ms_t SampleCountToTime(sample_count_t samples);

 private:
  std::string name_;
  AudioDecoder decoder_;
  std::optional<size_t> loop_fr_, loop_to_;
  std::atomic<STATUS> status_;
  AVSpec spec;
  std::optional<AudioFrame> buffer_;
  float volume_;
  std::list<std::unique_ptr<ICommand>> cmd_;

  mutable std::mutex mutex_;
};

using player_t = std::shared_ptr<AudioPlayer>;

template <typename... Ts>
player_t CreateAudioPlayer(Ts&&... params) {
  AudioDecoder decoder(std::forward<Ts>(params)...);
  return std::make_shared<AudioPlayer>(std::move(decoder));
}
