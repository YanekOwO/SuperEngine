// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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

#include "long_operations/siglus_movie.hpp"

#include "core/avdec/ffmpeg.hpp"
#include "core/queue.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"
#include "vm/exception.hpp"

#include <algorithm>
#include <chrono>
#include <stop_token>
#include <string>
#include <utility>

namespace lop {
namespace sr = serilang;
namespace fs = std::filesystem;
namespace chr = std::chrono;

SiglusMovieOperation::SiglusMovieOperation(System* sys,
                                           fs::path pth,
                                           MovieRect rect,
                                           int volume,
                                           bool wait_key)
    : path_(std::move(pth)),
      rect_(rect),
      wait_key_(wait_key),
      volume_(volume),
      clock_(sys->event().GetClock()),
      sound_(sys->sound()),
      graphics_(sys->graphics_ptr()) {}

SiglusMovieOperation::~SiglusMovieOperation() { Stop(); }

Task<int> SiglusMovieOperation::Run(WaitContext& waits) {
  playing_ = true;

  std::shared_ptr<SDLSurface> surface_;
  Frame current_frame;
  auto queue = std::make_shared<Queue<Frame>>(32);
  frame_queue_ = queue;

  FfmpegVideoDecoder dec(path_);
  const int msec_per_frame = std::max(1, dec.info().usec_per_frame / 1000);
  const std::size_t frame_size =
      static_cast<std::size_t>(dec.width()) * dec.height() * 4;
  current_frame.data.assign(frame_size, 0);
  surface_ = graphics_->CreateSurfaceBGRA(dec.info().size, current_frame.data);

  start_ = clock_->GetTime();
  // Stop and join the worker before destroying the frame locals it
  // captures, including when cancellation destroys a suspended frame.
  struct PlaybackGuard {
    SiglusMovieOperation& player;
    ~PlaybackGuard() { player.Stop(); }
  } playback_guard{*this};
  decoder_ = std::jthread([&](std::stop_token st) {
    try {
      for (int timestamp = 0; !st.stop_requested();
           timestamp += msec_per_frame) {
        if (int cur = playback_time_ms_.load(std::memory_order_relaxed);
            timestamp < cur) {
          timestamp = ((cur / msec_per_frame) + 1) * msec_per_frame;
        }

        Frame next;
        next.data.resize(frame_size);
        const bool updated = dec.DecodeTime(timestamp, next.data);
        if (!updated) {
          if (!dec.IsPlaying(timestamp))
            break;
          continue;
        }
        if (st.stop_requested())
          break;

        next.timestamp = timestamp;
        queue->Push(std::move(next));
      }
    } catch (...) {
      SetDecoderException(std::current_exception());
    }
    queue->Stop();
  });

  try {
    sound_.PlayMovieAudio(path_, volume_);
  } catch (const std::exception&) {
    sound_.StopMovieAudio();
  }

  while (playing_) {
    UpdatePlaybackTime();
    if (!queue->Pop(current_frame))
      break;

    const int now = UpdatePlaybackTime();
    bool late = now - current_frame.timestamp > msec_per_frame;
    if (late && queue->Size() >= 1)
      continue;
    surface_->UpdateBGRA(current_frame.data, false);
    frame_->draw = [surface = surface_,
                    destination = DestinationRect(surface_->GetRect())] {
      surface->RenderToScreen(surface->GetRect(), destination, 255);
    };

    if (co_await waits.WaitFor(chr::milliseconds(msec_per_frame), wait_key_) ==
        WaitOutcome::InterruptedByInput) {
      co_return 1;
    }
  }
  RethrowDecoderException();
  co_return 0;
}

int SiglusMovieOperation::CurrentTimeMs() const {
  if (sound_.MovieAudioPlaying())
    return std::max(0, sound_.MovieAudioTimeMs());

  const Clock::timepoint_t now = clock_->GetTime();
  const auto duration = chr::duration_cast<chr::milliseconds>(now - start_);
  const auto ms = duration.count();
  return ms > 0 ? static_cast<int>(ms) : 0;
}

int SiglusMovieOperation::UpdatePlaybackTime() {
  const int ms = CurrentTimeMs();
  playback_time_ms_.store(ms, std::memory_order_relaxed);
  return ms;
}

bool SiglusMovieOperation::IsPlaying() const {
  return playing_ || decoder_.joinable();
}

Rect SiglusMovieOperation::DestinationRect(const Rect& source) const {
  const int width = rect_.width > 0 ? rect_.width : source.width();
  const int height = rect_.height > 0 ? rect_.height : source.height();
  return Rect::REC(rect_.x, rect_.y, width, height);
}

void SiglusMovieOperation::SetDecoderException(std::exception_ptr exception) {
  std::lock_guard<std::mutex> lock(decoder_exception_mutex_);
  if (!decoder_exception_)
    decoder_exception_ = std::move(exception);
}

void SiglusMovieOperation::RethrowDecoderException() {
  std::exception_ptr exception;
  {
    std::lock_guard<std::mutex> lock(decoder_exception_mutex_);
    exception = decoder_exception_;
  }
  if (!exception)
    return;

  try {
    std::rethrow_exception(exception);
  } catch (const sr::RuntimeError&) {
    throw;
  } catch (const std::exception& e) {
    throw sr::RuntimeError(std::string("movie decode failed: ") + e.what());
  } catch (...) {
    throw sr::RuntimeError("movie decode failed");
  }
}

void SiglusMovieOperation::Stop() {
  if (stopped_)
    return;
  stopped_ = true;
  playing_ = false;
  frame_->draw = {};
  sound_.StopMovieAudio();
  if (frame_queue_)
    frame_queue_->Stop();
  if (decoder_.joinable()) {
    decoder_.request_stop();
    decoder_.join();
  }
}

}  // namespace lop
