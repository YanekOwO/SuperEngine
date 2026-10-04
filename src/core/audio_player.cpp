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

#include "core/audio_player.hpp"

#include <stdexcept>

using sample_count_t = AudioPlayer::sample_count_t;

namespace predefined_audioplayer_commands {
class AdjustVolume : public AudioPlayer::ICommand {
 public:
  AdjustVolume(float start_volume,
               float end_volume,
               sample_count_t fadein_samples)
      : start_volume_(start_volume),
        end_volume_(end_volume),
        samples_(fadein_samples),
        faded_samples_(0) {}
  std::string Name() const override { return "AdjustVolume"; }
  void Execute(AudioPlayer::AudioFrame& af) override {
    std::visit(
        [&](auto& data) {
          for (auto& it : data) {
            if (faded_samples_ >= samples_)
              break;
            auto fade_factor = static_cast<float>(faded_samples_++) / samples_;
            it *= start_volume_ + fade_factor * (end_volume_ - start_volume_);
          }
        },
        af.ad.data);
  }
  bool IsFinished() override { return faded_samples_ >= samples_; }

 private:
  float start_volume_, end_volume_;
  sample_count_t samples_, faded_samples_;
};

class TerminateAfterNLoops : public AudioPlayer::ICommand {
 public:
  TerminateAfterNLoops(AudioPlayer& player, sample_count_t cur, int n)
      : player_(player), cur_(cur), n_(n) {}
  std::string Name() const override { return "TerminateAfterNLoops"; }
  void Execute(AudioPlayer::AudioFrame& af) override {
    if (af.cur < cur_)
      --n_;
    cur_ = af.cur;
    if (IsFinished()) {
      af.ad.Clear();
      player_.TerminateImpl();
    }
  }
  bool IsFinished() override { return n_ < 0; }

 private:
  AudioPlayer& player_;
  sample_count_t cur_;
  int n_;
};

}  // namespace predefined_audioplayer_commands

inline static size_t aplayer_id = 0;
AudioPlayer::AudioPlayer(AudioDecoder&& dec)
    : name_("AudioPlayer (" + std::to_string(++aplayer_id) + ')'),
      decoder_(std::move(dec)),
      loop_fr_(),
      loop_to_(),
      status_(decoder_.HasNext() ? STATUS::PLAYING : STATUS::TERMINATED),
      spec(decoder_.GetSpec()),
      buffer_(),
      volume_(1.0f) {}

AVSpec AudioPlayer::GetSpec() const { return spec; }

AudioPlayer::time_ms_t AudioPlayer::GetCurrentTime() const {
  return PcmLocation() * 1000 / spec.sample_rate;
}

struct Multiply {
  Multiply(float x) : x_(x) {}
  template <typename T>
  void operator()(std::vector<T>& data) {
    for (auto& it : data)
      it *= x_;
  }
  float x_;
};

struct FillZeros {
  FillZeros(size_t count) : cnt(count) {}
  template <typename T>
  void operator()(std::vector<T>& data) {
    T silent = T();
    if constexpr (std::is_unsigned_v<T>)
      silent = static_cast<T>(std::numeric_limits<T>::max() / 2);
    data.resize(data.size() + cnt);
    std::fill(data.end() - cnt, data.end(), silent);
  }
  size_t cnt;
};

AudioData AudioPlayer::LoadPCM(sample_count_t nsamples) {
  if (nsamples <= 0)
    throw std::invalid_argument("Samples should be greater than 0, got: " +
                                std::to_string(nsamples));

  AudioData ret;
  ret.spec = spec;
  ret.PrepareDatabuf();

  if (IsPlaying()) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (IsPlaying()) {
      if (buffer_.has_value()) {
        ret.Append(buffer_->ad);
        buffer_.reset();
      }

      auto cur = decoder_.Tell();
      while (ret.SampleCount() < nsamples && IsPlaying()) {
        auto next = LoadNext();
        if (next.SampleCount() == 0)
          continue;
        ClipFrame(next);
        cur = next.cur + next.SampleCount() / spec.channel_count;
        ret.Append(std::move(next.ad));
      }

      std::visit(Multiply(volume_), ret.data);

      if (ret.SampleCount() > nsamples) {
        std::visit(
            [&](auto& data) {
              const auto n = data.size() - nsamples;
              cur -= n / spec.channel_count;

              using container_t = std::decay_t<decltype(data)>;
              container_t buf(data.end() - n, data.end());
              buffer_ = AudioFrame{.ad = {spec, std::move(buf)}, .cur = cur};
              data.erase(data.end() - n, data.end());
            },
            ret.data);
      }
    }
  }

  nsamples -= ret.SampleCount();
  if (nsamples > 0)
    std::visit(FillZeros(nsamples), ret.data);

  return ret;
}

AudioData AudioPlayer::LoadRemain() {
  std::unique_lock<std::mutex> lock(mutex_, std::defer_lock);

  AudioData ret;
  ret.spec = spec;
  ret.PrepareDatabuf();

  lock.lock();
  if (status_ == STATUS::PAUSED)
    return ret;

  auto cur = PcmLocation();
  if (buffer_.has_value()) {
    ret = buffer_->ad;
    cur = buffer_->cur + buffer_->SampleCount() / spec.channel_count;
    buffer_.reset();
  }

  while (true) {
    auto next = LoadNext();
    if (next.SampleCount() == 0)
      break;
    if (next.cur < cur) {
      buffer_ = std::move(next);
      break;
    }

    ClipFrame(next);
    const auto next_to = next.cur + next.SampleCount() / spec.channel_count;
    ret.Append(std::move(next.ad));
    cur = next_to;
  }
  lock.unlock();

  std::visit(Multiply(volume_), ret.data);
  return ret;
}

bool AudioPlayer::IsLoopingEnabled() const { return loop_fr_.has_value(); }

void AudioPlayer::SetLoop(size_t ab_loop_a, size_t ab_loop_b) {
  if (ab_loop_a >= ab_loop_b) {
    std::ostringstream oss;
    oss << "Loop from (" << ab_loop_a << ')';
    oss << " must be less than loop to (" << ab_loop_b << ").";
    throw std::invalid_argument(oss.str());
  }

  std::lock_guard<std::mutex> lock(mutex_);
  SetLoopImpl(ab_loop_a, ab_loop_b);
}

void AudioPlayer::SetLoopImpl(size_t ab_loop_a, size_t ab_loop_b) {
  loop_fr_ = ab_loop_a;
  loop_to_ = ab_loop_b;

  auto cur = PcmLocation();
  if (cur < ab_loop_a || cur >= ab_loop_b) {
    buffer_.reset();
    decoder_.Seek(ab_loop_a, SEEKDIR::BEG);
  }
}

void AudioPlayer::SetPLoop(size_t from, size_t to, size_t loop) {
  // Gameexe uses -1 for an open-ended track. It reaches this size_t API as
  // size_t::max(), which cannot be safely converted back to the signed frame
  // positions used by ClipFrame.
  if (to == std::numeric_limits<size_t>::max())
    to = npos;

  if (!(from < to && loop < to)) {
    std::ostringstream oss;
    oss << "Invalid p-loop: (";
    oss << from << ',' << loop << ',' << to << ").";
    throw std::invalid_argument(oss.str());
  }

  std::lock_guard<std::mutex> lock(mutex_);
  SetLoopImpl(from, to);
  struct RegisterNextLoop : public ICommand {
    RegisterNextLoop(AudioPlayer& player, size_t from, size_t to, long long cur)
        : player_(player), from_(from), to_(to), cur_(cur), fin_(false) {}
    std::string Name() const override { return "NextLoop"; }
    void Execute(AudioFrame& af) override {
      if (IsFinished())
        return;
      if (af.cur < cur_) {
        player_.SetLoopImpl(from_, to_);
        af.ad.Clear();
        fin_ = true;
      }
      cur_ = af.cur;
    }
    bool IsFinished() override { return fin_; }

    AudioPlayer& player_;
    size_t from_, to_;
    long long cur_;
    bool fin_;
  };

  cmd_.emplace_front(
      std::make_unique<RegisterNextLoop>(*this, loop, to, PcmLocation()));
}

void AudioPlayer::SetLoopTimes(int n) {
  using namespace predefined_audioplayer_commands;
  std::lock_guard<std::mutex> lock(mutex_);

  for (auto it = cmd_.begin(); it != cmd_.end();) {
    if (it->get()->Name() == "TerminateAfterNLoops")
      it = cmd_.erase(it);
    else
      ++it;
  }

  loop_fr_ = loop_fr_.value_or(0);
  loop_to_ = loop_to_.value_or(npos);

  if (n < 0) {  // infinity loop
    return;
  } else {
    cmd_.emplace_back(
        std::make_unique<TerminateAfterNLoops>(*this, PcmLocation(), n));
  }
}

bool AudioPlayer::IsPlaying() const { return status_ == STATUS::PLAYING; }

AudioPlayer::STATUS AudioPlayer::GetStatus() const { return status_; }

void AudioPlayer::Terminate() {
  std::lock_guard<std::mutex> lock(mutex_);
  TerminateImpl();
}

void AudioPlayer::TerminateImpl() { status_ = STATUS::TERMINATED; }

void AudioPlayer::SetName(std::string name) {
  std::lock_guard<std::mutex> lock(mutex_);
  name_ = std::move(name);
}

std::string AudioPlayer::GetName() const { return name_; }

void AudioPlayer::FadeIn(float fadein_ms) {
  using namespace predefined_audioplayer_commands;
  std::lock_guard<std::mutex> lock(mutex_);

  auto fadein_samples = TimeToSampleCount(fadein_ms);
  cmd_.emplace_back(std::make_unique<AdjustVolume>(0, 1, fadein_samples));
}

void AudioPlayer::FadeOut(float fadeout_ms, bool should_then_terminate) {
  using namespace predefined_audioplayer_commands;
  std::lock_guard<std::mutex> lock(mutex_);

  auto fadeout_samples = TimeToSampleCount(fadeout_ms);
  cmd_.emplace_back(std::make_unique<AdjustVolume>(1, 0, fadeout_samples));

  if (should_then_terminate) {
    class TerminateAfter : public ICommand {
     public:
      TerminateAfter(AudioPlayer& client, sample_count_t samples)
          : client_(client), samples_(samples) {}
      std::string Name() const override { return "TerminateAfter"; }
      void Execute(AudioFrame& af) override {
        samples_ -= af.SampleCount();
        if (IsFinished())
          client_.TerminateImpl();
      }
      bool IsFinished() override { return samples_ <= 0; }

     private:
      AudioPlayer& client_;
      sample_count_t samples_;
    };

    cmd_.emplace_back(std::make_unique<TerminateAfter>(*this, fadeout_samples));
  }
}

void AudioPlayer::SetVolume(float vol) { volume_ = vol; }

float AudioPlayer::GetVolume() const { return volume_; }

void AudioPlayer::Pause() {
  if (!IsPlaying())
    return;

  std::lock_guard<std::mutex> lock(mutex_);
  if (IsPlaying())
    status_ = STATUS::PAUSED;
}

void AudioPlayer::Unpause() {
  if (status_ != STATUS::PAUSED)
    return;

  std::lock_guard<std::mutex> lock(mutex_);
  if (status_ == STATUS::PAUSED)
    status_ = STATUS::PLAYING;
}

void AudioPlayer::OnEndOfPlayback() {
  size_t seek_to = loop_fr_.value_or(npos);
  if (seek_to != npos)
    decoder_.Seek(seek_to, SEEKDIR::BEG);
  else
    TerminateImpl();
}

AudioPlayer::AudioFrame AudioPlayer::LoadNext() {
  if (!IsPlaying())
    return {};
  if (!decoder_.HasNext())
    return {};

  auto cur = decoder_.Tell();
  if (cur >= loop_to_.value_or(npos)) {
    decoder_.Seek(loop_fr_.value_or(0), SEEKDIR::BEG);
    cur = decoder_.Tell();
  }

  auto next_chunk = decoder_.DecodeNext();
  if (next_chunk.SampleCount() == 0)
    throw std::runtime_error("AudioPlayer error: Found empty pcm chunk");

  if (!decoder_.HasNext())
    OnEndOfPlayback();

  AudioFrame frame{.ad = std::move(next_chunk), .cur = std::move(cur)};
  for (auto it = cmd_.begin(); it != cmd_.end();) {
    if (it->get()->IsFinished()) {
      it = cmd_.erase(it);
      continue;
    }
    it->get()->Execute(frame);
    if (it->get()->IsFinished()) {
      it = cmd_.erase(it);
      continue;
    }
    ++it;
  }

  return frame;
}

void AudioPlayer::ClipFrame(AudioFrame& frame) const {
  long long frame_fr = frame.cur,
            frame_to = frame.cur + frame.SampleCount() / spec.channel_count;
  long long audio_fr = loop_fr_.value_or(0), audio_to = loop_to_.value_or(npos);

  if (frame_to < audio_fr || audio_to <= frame_fr) {
    std::visit([](auto& data) { data.clear(); }, frame.ad.data);
  } else if (frame_fr < audio_fr || audio_to < frame_to) {
    long long clip_front = std::max(0ll, audio_fr - frame_fr);
    long long clip_back = std::max(0ll, frame_to - audio_to);
    frame.cur += clip_front;
    std::visit(
        [&](auto& data) {
          clip_front *= spec.channel_count;
          clip_back *= spec.channel_count;
          using container_t = std::decay_t<decltype(data)>;
          container_t clipped_data(data.cbegin() + clip_front,
                                   data.cend() - clip_back);
          data = std::move(clipped_data);
        },
        frame.ad.data);
  }
}

sample_count_t AudioPlayer::PcmLocation() const {
  return buffer_.has_value() ? buffer_->cur : decoder_.Tell();
}

static constexpr auto s_over_ms = 1000;

AudioPlayer::sample_count_t AudioPlayer::TimeToSampleCount(time_ms_t time) {
  return time * spec.sample_rate * spec.channel_count / s_over_ms;
}

AudioPlayer::time_ms_t AudioPlayer::SampleCountToTime(sample_count_t samples) {
  return samples * s_over_ms / (spec.sample_rate * spec.channel_count);
}
