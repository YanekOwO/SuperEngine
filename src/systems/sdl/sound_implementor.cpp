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

#include "sound_implementor.hpp"

#include "core/resampler.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

using std::string_literals::operator""s;

// -----------------------------------------------------------------------

bool SDLSoundImpl::ChannelInfo::IsIdle() const { return player == nullptr; }

void SDLSoundImpl::ChannelInfo::Reset() {
  player = nullptr;
  fade_ms = 0;
}

// -----------------------------------------------------------------------

SDLSoundImpl::SDLSoundImpl() = default;
SDLSoundImpl::~SDLSoundImpl() { CloseAudio(); }

void SDLSoundImpl::InitSystem() const {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    throw std::runtime_error("SDL Error: "s + GetError());
}
void SDLSoundImpl::QuitSystem() const { SDL_QuitSubSystem(SDL_INIT_AUDIO); }

void SDLSoundImpl::AllocateChannels(int num) const {
  const SDL_AudioSpec src{.format = ToSDLSoundFormat(spec_.sample_format),
                          .channels = spec_.channel_count,
                          .freq = spec_.sample_rate};

  ch_.resize(num);
  for (int i = 0; i < num; ++i) {
    ch_[i].stream = SDL_CreateAudioStream(&src, &src);
    if (!ch_[i].stream)
      throw std::runtime_error("SDL Error: "s + GetError());
    SDL_SetAudioStreamGetCallback(
        ch_[i].stream, &SDLSoundImpl::OnChannelData,
        reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    if (!SDL_BindAudioStream(device_, ch_[i].stream))
      throw std::runtime_error("SDL Error: "s + GetError());
  }
}

void SDLSoundImpl::OpenAudio(AVSpec spec, int /*buf_size*/) const {
  const SDL_AudioSpec want{.format = ToSDLSoundFormat(spec.sample_format),
                           .channels = spec.channel_count,
                           .freq = spec.sample_rate};
  device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want);
  if (device_ == 0)
    throw std::runtime_error("SDL Error: "s + GetError());

  spec_ = spec;

  bgm_stream_ = SDL_CreateAudioStream(&want, &want);
  movie_stream_ = SDL_CreateAudioStream(&want, &want);
  if (!bgm_stream_ || !movie_stream_)
    throw std::runtime_error("SDL Error: "s + GetError());
  SDL_SetAudioStreamGetCallback(bgm_stream_, &SDLSoundImpl::OnBgmData, nullptr);
  SDL_SetAudioStreamGetCallback(movie_stream_, &SDLSoundImpl::OnMovieData,
                                nullptr);
  if (!SDL_BindAudioStream(device_, bgm_stream_) ||
      !SDL_BindAudioStream(device_, movie_stream_))
    throw std::runtime_error("SDL Error: "s + GetError());
}

void SDLSoundImpl::CloseAudio() const {
  if (device_)
    SDL_PauseAudioDevice(device_);

  // SDL_SetAudioStreamGetCallback waits until a callback in progress
  // returns. The device is paused, thus no new callback can start after
  // this block.
  for (auto& channel : ch_) {
    if (channel.stream)
      SDL_SetAudioStreamGetCallback(channel.stream, nullptr, nullptr);
  }
  if (bgm_stream_)
    SDL_SetAudioStreamGetCallback(bgm_stream_, nullptr, nullptr);
  if (movie_stream_)
    SDL_SetAudioStreamGetCallback(movie_stream_, nullptr, nullptr);

  for (auto& channel : ch_) {
    if (channel.player)
      channel.player->Terminate();
    SDL_DestroyAudioStream(channel.stream);
    channel.stream = nullptr;
    channel.Reset();
  }
  ch_.clear();

  SDL_DestroyAudioStream(bgm_stream_);
  SDL_DestroyAudioStream(movie_stream_);
  bgm_stream_ = nullptr;
  movie_stream_ = nullptr;
  if (bgm_player_)
    bgm_player_->Terminate();
  if (movie_player_)
    movie_player_->Terminate();
  bgm_player_ = nullptr;
  movie_player_ = nullptr;

  if (device_) {
    SDL_CloseAudioDevice(device_);
    device_ = 0;
  }
}

inline static void CheckChannel(int ch_id,
                                size_t tot_channel,
                                std::string function_name = "sdl implementor") {
  if (ch_id < 0 || ch_id >= tot_channel)
    throw std::invalid_argument(function_name + ": Invalid channel number " +
                                std::to_string(ch_id));
}

template <typename T>
T SilenceValue() {
  if constexpr (std::is_unsigned_v<T>)
    return static_cast<T>(std::numeric_limits<T>::max() / 2 + 1);
  else
    return T();
}

template <typename T>
T ClampSample(long double value) {
  const long double low =
      static_cast<long double>(std::numeric_limits<T>::min());
  const long double high =
      static_cast<long double>(std::numeric_limits<T>::max());
  return static_cast<T>(std::clamp(value, low, high));
}

template <typename T>
T AverageSamples(const T* samples, int count) {
  long double sum = 0;
  for (int i = 0; i < count; ++i)
    sum += static_cast<long double>(samples[i]);
  return ClampSample<T>(sum / count);
}

void MatchChannelCount(AudioData& audio, int channel_count) {
  if (audio.spec.channel_count == channel_count)
    return;

  const int input_channels = audio.spec.channel_count;
  std::visit(
      [&](auto& data) {
        using container_t = std::decay_t<decltype(data)>;
        using sample_t = typename container_t::value_type;

        const std::size_t frames = data.size() / input_channels;
        container_t converted;
        converted.reserve(frames * channel_count);

        for (std::size_t frame = 0; frame < frames; ++frame) {
          const sample_t* input = data.data() + frame * input_channels;
          if (channel_count == 1) {
            converted.push_back(AverageSamples(input, input_channels));
          } else {
            for (int channel = 0; channel < channel_count; ++channel) {
              converted.push_back(
                  input[input_channels == 1
                            ? 0
                            : std::min(channel, input_channels - 1)]);
            }
          }
        }

        data = std::move(converted);
      },
      audio.data);

  audio.spec.channel_count = channel_count;
}

avsample_buffer_t LoadForOutput(player_t player,
                                std::size_t output_samples,
                                const AVSpec& output_spec) {
  const AVSpec player_spec = player->GetSpec();
  const std::size_t output_frames =
      output_samples / static_cast<std::size_t>(output_spec.channel_count);
  std::size_t request_frames = output_frames;
  if (player_spec.sample_rate != output_spec.sample_rate) {
    request_frames =
        (output_frames * static_cast<std::size_t>(player_spec.sample_rate) +
         static_cast<std::size_t>(output_spec.sample_rate) - 1) /
        static_cast<std::size_t>(output_spec.sample_rate);
  }

  AudioData audio = player->LoadPCM(
      request_frames * static_cast<std::size_t>(player_spec.channel_count));
  if (audio.spec.sample_rate != output_spec.sample_rate) {
    Resampler resampler(output_spec.sample_rate);
    resampler.Resample(audio);
  }
  MatchChannelCount(audio, output_spec.channel_count);

  avsample_buffer_t converted = audio.GetAs(output_spec.sample_format);
  std::visit(
      [&](auto& data) {
        using container_t = std::decay_t<decltype(data)>;
        using sample_t = typename container_t::value_type;
        data.resize(output_samples, SilenceValue<sample_t>());
      },
      converted);
  return converted;
}

void SDLSoundImpl::SetVolume(int channel, int vol) const {
  if (vol < 0 || vol > 127)
    throw std::invalid_argument("sdl SetVolume: Invalid volume " +
                                std::to_string(vol));
  CheckChannel(channel, ch_.size(), "sdl SetVolume");

  SDL_AudioStream* stream = ch_[channel].stream;
  SDL_LockAudioStream(stream);
  ch_[channel].base_gain = static_cast<float>(vol) / 128.0f;
  if (ch_[channel].fade_ms == 0)
    SDL_SetAudioStreamGain(stream, ch_[channel].base_gain);
  SDL_UnlockAudioStream(stream);
}

bool SDLSoundImpl::IsPlaying(int channel) const {
  CheckChannel(channel, ch_.size(), "sdl IsPlaying");

  SDL_AudioStream* stream = ch_[channel].stream;
  SDL_LockAudioStream(stream);
  const bool playing = !ch_[channel].IsIdle();
  SDL_UnlockAudioStream(stream);
  return playing;
}

int SDLSoundImpl::FindIdleChannel() const {
  if (ch_.empty())
    throw std::runtime_error("SDL Error: Channel not allocated.");

  for (int i = 0; i < ch_.size(); ++i) {
    if (IsPlaying(i))
      continue;
    return i;
  }

  throw std::runtime_error("All channels are busy.");
}

std::vector<uint8_t> SDLSoundImpl::RenderChunk(player_t audio) {
  AudioData audio_data = audio->LoadRemain();
  const auto system_frequency = spec_.sample_rate;
  if (audio_data.spec.sample_rate != system_frequency) {
    Resampler resampler(system_frequency);
    resampler.Resample(audio_data);
  }

  return std::visit(
      [&](auto&& pcm_data) -> std::vector<uint8_t> {
        using container_t = std::decay_t<decltype(pcm_data)>;
        using value_t = typename container_t::value_type;

        // TODO: For now, this is the only place where mono to stereo conversion
        // is needed. Consider extract function and pull it up to `AudioData` if
        // needed frequently in the future
        if (spec_.channel_count == 2 && audio_data.spec.channel_count == 1) {
          pcm_data.resize(pcm_data.size() * 2);
          for (size_t i = pcm_data.size(); i-- > 0;)
            pcm_data[i] = pcm_data[i >> 1];
        }

        std::vector<uint8_t> raw_bytes(pcm_data.size() * sizeof(value_t));
        std::memmove(raw_bytes.data(), pcm_data.data(), raw_bytes.size());
        return raw_bytes;
      },
      audio_data.GetAs(spec_.sample_format));
}

int SDLSoundImpl::PlayChannel(int channel, player_t audio) {
  CheckChannel(channel, ch_.size(), "sdl PlayChannel");

  std::vector<uint8_t> pcm = RenderChunk(audio);

  SDL_AudioStream* stream = ch_[channel].stream;
  SDL_LockAudioStream(stream);
  SDL_ClearAudioStream(stream);
  ch_[channel].player = audio;
  ch_[channel].fade_ms = 0;
  SDL_SetAudioStreamGain(stream, ch_[channel].base_gain);
  const bool ok =
      SDL_PutAudioStreamData(stream, pcm.data(), static_cast<int>(pcm.size()));
  if (!ok)
    ch_[channel].Reset();
  SDL_UnlockAudioStream(stream);

  if (!ok)
    throw std::runtime_error("Failed to play on channel: " +
                             std::to_string(channel));

  return channel;
}

void SDLSoundImpl::OnChannelData(void* userdata,
                                 SDL_AudioStream* stream,
                                 int additional,
                                 int) {
  const int channel = static_cast<int>(reinterpret_cast<intptr_t>(userdata));
  ChannelInfo& info = ch_[channel];
  if (info.IsIdle())
    return;

  if (info.fade_ms > 0) {
    const uint64_t elapsed = SDL_GetTicks() - info.fade_start;
    if (elapsed >= info.fade_ms) {
      SDL_ClearAudioStream(stream);
      SDL_SetAudioStreamGain(stream, info.base_gain);
      info.player->Terminate();
      info.Reset();
      return;
    }
    SDL_SetAudioStreamGain(
        stream, info.base_gain * (1.0f - static_cast<float>(elapsed) /
                                             static_cast<float>(info.fade_ms)));
  }

  if (additional <= 0)
    return;

  if (!info.player->IsPlaying()) {
    if (SDL_GetAudioStreamAvailable(stream) == 0)
      info.Reset();  // The stream is empty, so the channel becomes idle.
    return;
  }

  // SDL has already accounted for queued audio. The deficit is in input
  // bytes, whereas SDL_GetAudioStreamAvailable reports converted output bytes.
  std::size_t remaining = static_cast<std::size_t>(additional);
  while (info.player->IsPlaying() && remaining > 0) {
    std::vector<uint8_t> pcm = RenderChunk(info.player);
    // A fully clipped frame can be empty while the player continues.
    if (pcm.empty())
      break;
    if (!SDL_PutAudioStreamData(stream, pcm.data(),
                                static_cast<int>(pcm.size())))
      break;
    remaining -= std::min(remaining, pcm.size());
  }
}

void SDLSoundImpl::PumpPlayer(player_t& player,
                              bool enabled,
                              SDL_AudioStream* stream,
                              int additional) {
  if (!player || !enabled || additional <= 0)
    return;
  if (player->GetStatus() == AudioPlayer::STATUS::TERMINATED) {
    player = nullptr;
    return;
  }

  // The callback gives additional as a count of bytes in the input format of
  // the stream.
  SDL_AudioSpec src;
  SDL_GetAudioStreamFormat(stream, &src, nullptr);
  const int src_frame_size = SDL_AUDIO_FRAMESIZE(src);
  const std::size_t src_frames =
      (static_cast<std::size_t>(additional) + src_frame_size - 1) /
      src_frame_size;
  if (src_frames == 0)
    return;

  const AVSpec out{.sample_rate = src.freq,
                   .sample_format = spec_.sample_format,
                   .channel_count = spec_.channel_count};
  avsample_buffer_t buf =
      LoadForOutput(player, src_frames * out.channel_count, out);
  std::visit(
      [&](auto& data) {
        using T = typename std::decay_t<decltype(data)>::value_type;
        SDL_PutAudioStreamData(stream, data.data(),
                               static_cast<int>(data.size() * sizeof(T)));
      },
      buf);

  if (player->GetStatus() == AudioPlayer::STATUS::TERMINATED)
    player = nullptr;
}

void SDLSoundImpl::OnBgmData(void*,
                             SDL_AudioStream* stream,
                             int additional,
                             int) {
  PumpPlayer(bgm_player_, bgm_enabled_, stream, additional);
}

void SDLSoundImpl::OnMovieData(void*,
                               SDL_AudioStream* stream,
                               int additional,
                               int) {
  PumpPlayer(movie_player_, true, stream, additional);
}

void SDLSoundImpl::PlayBgm(player_t audio) {
  if (!bgm_stream_)
    return;

  const SDL_AudioSpec src{.format = ToSDLSoundFormat(spec_.sample_format),
                          .channels = spec_.channel_count,
                          .freq = audio->GetSpec().sample_rate};

  SDL_LockAudioStream(bgm_stream_);
  SDL_ClearAudioStream(bgm_stream_);
  SDL_SetAudioStreamFormat(bgm_stream_, &src, nullptr);
  bgm_player_ = audio;
  SDL_UnlockAudioStream(bgm_stream_);
}

player_t SDLSoundImpl::GetBgm() const {
  if (!bgm_stream_)
    return nullptr;

  SDL_LockAudioStream(bgm_stream_);
  player_t player = bgm_player_;
  SDL_UnlockAudioStream(bgm_stream_);
  return player;
}

void SDLSoundImpl::EnableBgm() { bgm_enabled_ = true; }

void SDLSoundImpl::DisableBgm() { bgm_enabled_ = false; }

void SDLSoundImpl::PlayMovieAudio(player_t audio) {
  if (!movie_stream_)
    return;

  const SDL_AudioSpec src{.format = ToSDLSoundFormat(spec_.sample_format),
                          .channels = spec_.channel_count,
                          .freq = audio->GetSpec().sample_rate};

  SDL_LockAudioStream(movie_stream_);
  SDL_ClearAudioStream(movie_stream_);
  SDL_SetAudioStreamFormat(movie_stream_, &src, nullptr);
  movie_player_ = audio;
  SDL_UnlockAudioStream(movie_stream_);
}

player_t SDLSoundImpl::GetMovieAudio() const {
  if (!movie_stream_)
    return nullptr;

  SDL_LockAudioStream(movie_stream_);
  player_t player = movie_player_;
  SDL_UnlockAudioStream(movie_stream_);
  return player;
}

void SDLSoundImpl::StopMovieAudio() {
  if (!movie_stream_)
    return;

  SDL_LockAudioStream(movie_stream_);
  if (movie_player_)
    movie_player_->Terminate();
  movie_player_ = nullptr;
  SDL_ClearAudioStream(movie_stream_);
  SDL_UnlockAudioStream(movie_stream_);
}

int SDLSoundImpl::FadeOutChannel(int channel, int fadetime) const {
  CheckChannel(channel, ch_.size(), "sdl FadeOutChannel");

  if (fadetime <= 0) {
    HaltChannel(channel);
    return 0;
  }

  SDL_AudioStream* stream = ch_[channel].stream;
  SDL_LockAudioStream(stream);
  int fading = 0;
  if (!ch_[channel].IsIdle() && ch_[channel].fade_ms == 0) {
    ch_[channel].fade_start = SDL_GetTicks();
    ch_[channel].fade_ms = static_cast<uint32_t>(fadetime);
    fading = 1;
  }
  SDL_UnlockAudioStream(stream);
  return fading;
}

void SDLSoundImpl::HaltChannel(int channel) const {
  if (channel < 0) { /* all channels */
    for (int i = 0; i < ch_.size(); ++i)
      HaltChannel(i);
    return;
  }
  CheckChannel(channel, ch_.size(), "sdl HaltChannel");

  SDL_AudioStream* stream = ch_[channel].stream;
  SDL_LockAudioStream(stream);
  SDL_ClearAudioStream(stream);
  SDL_SetAudioStreamGain(stream, ch_[channel].base_gain);
  if (ch_[channel].player)
    ch_[channel].player->Terminate();
  ch_[channel].Reset();
  SDL_UnlockAudioStream(stream);
}

void SDLSoundImpl::HaltAllChannels() const { HaltChannel(-1); }

const char* SDLSoundImpl::GetError() const { return SDL_GetError(); }

SDL_AudioFormat SDLSoundImpl::ToSDLSoundFormat(AV_SAMPLE_FMT fmt) const {
  switch (fmt) {
    case AV_SAMPLE_FMT::U8:
      return SDL_AUDIO_U8;
    case AV_SAMPLE_FMT::S8:
      return SDL_AUDIO_S8;
    case AV_SAMPLE_FMT::S16:
      return SDL_AUDIO_S16;
    case AV_SAMPLE_FMT::S32:
      return SDL_AUDIO_S32;
    case AV_SAMPLE_FMT::FLT:
      return SDL_AUDIO_F32;
    case AV_SAMPLE_FMT::S64:
    case AV_SAMPLE_FMT::DBL:
      throw std::invalid_argument("Unsupported SDL audio format for: " +
                                  to_string(fmt));

    default:
      throw std::invalid_argument("Invalid AV_SAMPLE_FMT format: " +
                                  std::to_string(static_cast<int>(fmt)));
  }
}

AV_SAMPLE_FMT SDLSoundImpl::FromSDLSoundFormat(SDL_AudioFormat fmt) const {
  switch (fmt) {
    case SDL_AUDIO_U8:
      return AV_SAMPLE_FMT::U8;
    case SDL_AUDIO_S8:
      return AV_SAMPLE_FMT::S8;
    case SDL_AUDIO_S16:
      return AV_SAMPLE_FMT::S16;
    case SDL_AUDIO_S32:
      return AV_SAMPLE_FMT::S32;
    case SDL_AUDIO_F32:
      return AV_SAMPLE_FMT::FLT;
    default:
      throw std::invalid_argument("Invalid SDL audio format: " +
                                  std::to_string(static_cast<int>(fmt)));
  }
}

std::vector<SDLSoundImpl::ChannelInfo> SDLSoundImpl::ch_;
player_t SDLSoundImpl::bgm_player_ = nullptr;
player_t SDLSoundImpl::movie_player_ = nullptr;
std::atomic<bool> SDLSoundImpl::bgm_enabled_ = true;
AVSpec SDLSoundImpl::spec_;
SDL_AudioDeviceID SDLSoundImpl::device_ = 0;
SDL_AudioStream* SDLSoundImpl::bgm_stream_ = nullptr;
SDL_AudioStream* SDLSoundImpl::movie_stream_ = nullptr;
