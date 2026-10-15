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

#include "libsiglus/bindings/registry.hpp"

#include "core/asset_scanner.hpp"
#include "core/avdec/ffmpeg.hpp"
#include "core/queue.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/bindings/wait_helpers.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "srbind/module.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"
#include "vm/exception.hpp"
#include "vm/future.hpp"
#include "vm/value.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace libsiglus::binding {
namespace sb = srbind;
namespace sr = serilang;
namespace fs = std::filesystem;
namespace chr = std::chrono;

namespace {

const std::set<std::string> kMovieExtensions{"wmv", "asf", "avi", "mpg",
                                             "mpeg"};

struct MovieRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

class SiglusMovie {
 public:
  explicit SiglusMovie(SiglusMachine& runtime)
      : vm_(runtime.vm.get()),
        system_(runtime.system.get()),
        asset_scanner_(runtime.asset_scanner) {}

  struct MoviePlayer : public CoroutineTask {
    MoviePlayer(sr::VM& vm,
                System* sys,
                fs::path pth,
                MovieRect rect,
                int volume,
                bool wait_key)
        : CoroutineTask(vm, sys->event_ptr().get()),
          path_(std::move(pth)),
          rect_(rect),
          wait_key_(wait_key),
          volume_(volume),
          clock_(sys->event().GetClock()),
          sound_(sys->sound()),
          graphics_(sys->graphics_ptr()) {}

    struct Frame {
      int timestamp;
      std::vector<char> data;
    };

    TaskCoroutine Run() override {
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
      surface_ =
          graphics_->CreateSurfaceBGRA(dec.info().size, current_frame.data);

      start_ = clock_->GetTime();
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

      TakeGraphicsUpdateResponsibility();

      try {
        while (playing_) {
          UpdatePlaybackTime();
          if (!queue->Pop(current_frame))
            break;

          const int now = UpdatePlaybackTime();
          bool late = now - current_frame.timestamp > msec_per_frame;
          if (late && queue->Size() >= 1)
            continue;
          surface_->UpdateBGRA(current_frame.data, false);

          graphics_->RenderCustomFrame([&] {
            surface_->RenderToScreen(surface_->GetRect(),
                                     DestinationRect(surface_->GetRect()), 255);
          });

          if ((co_await WaitFor(chr::milliseconds(msec_per_frame), wait_key_) ==
               WaitOutcome::InterruptedByInput)) {
            Stop();
            co_return 1;
          }
        }
        RethrowDecoderException();
      } catch (...) {
        Stop();
        throw;
      }

      Stop();
      co_return 0;
    }

    int CurrentTimeMs() const {
      if (sound_.MovieAudioPlaying())
        return std::max(0, sound_.MovieAudioTimeMs());

      const Clock::timepoint_t now = clock_->GetTime();
      const auto duration = chr::duration_cast<chr::milliseconds>(now - start_);
      const auto ms = duration.count();
      return ms > 0 ? static_cast<int>(ms) : 0;
    }

    int UpdatePlaybackTime() {
      const int ms = CurrentTimeMs();
      playback_time_ms_.store(ms, std::memory_order_relaxed);
      return ms;
    }

    bool IsPlaying() const { return playing_ || decoder_.joinable(); }

    Rect DestinationRect(const Rect& source) const {
      const int width = rect_.width > 0 ? rect_.width : source.width();
      const int height = rect_.height > 0 ? rect_.height : source.height();
      return Rect::REC(rect_.x, rect_.y, width, height);
    }

    void SetDecoderException(std::exception_ptr exception) {
      std::lock_guard<std::mutex> lock(decoder_exception_mutex_);
      if (!decoder_exception_)
        decoder_exception_ = std::move(exception);
    }

    void RethrowDecoderException() {
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

    void Stop() {
      if (stopped_)
        return;
      stopped_ = true;
      playing_ = false;
      RestoreGraphicsUpdateResponsibility();
      sound_.StopMovieAudio();
      if (frame_queue_)
        frame_queue_->Stop();
      if (decoder_.joinable()) {
        decoder_.request_stop();
        decoder_.join();
      }
    }

    void TakeGraphicsUpdateResponsibility() {
      if (previous_graphics_update_responsibility_)
        return;
      previous_graphics_update_responsibility_ =
          graphics_->is_responsible_for_update();
      graphics_->set_is_responsible_for_update(false);
    }

    void RestoreGraphicsUpdateResponsibility() {
      if (!previous_graphics_update_responsibility_)
        return;
      graphics_->set_is_responsible_for_update(
          *previous_graphics_update_responsibility_);
      previous_graphics_update_responsibility_.reset();
    }

    fs::path path_;
    MovieRect rect_;
    bool wait_key_;
    int volume_ = 255;
    bool playing_ = false;
    bool stopped_ = false;
    std::shared_ptr<Clock> clock_;
    SoundSystem& sound_;
    std::shared_ptr<GraphicsSystem> graphics_;
    Clock::timepoint_t start_;
    std::atomic<int> playback_time_ms_ = 0;
    std::jthread decoder_;
    std::shared_ptr<Queue<Frame>> frame_queue_;
    std::mutex decoder_exception_mutex_;
    std::exception_ptr decoder_exception_;
    std::optional<bool> previous_graphics_update_responsibility_;
  };

  MovieRect DefaultRect() const {
    MovieRect rect;
    if (system_) {
      Size size = system_->graphics().screen_size();
      rect.width = size.width();
      rect.height = size.height();
    }
    return rect;
  }

  void Play(std::string file_name,
            std::optional<MovieRect> maybe_rect,
            bool key_skip = false) {
    MovieRect rect = DefaultRect();
    rect = maybe_rect.value_or(rect);

    Stop();
    if (!vm_ || !system_ || !asset_scanner_)
      return;

    fs::path path = FindMovie(file_name);

    if (system_->ShouldFastForward()) {
      FfmpegVideoDecoder validate(path);
      return;
    }

    std::unique_ptr<MoviePlayer> player = std::make_unique<MoviePlayer>(
        *vm_, system_, std::move(path), rect, 255, key_skip);
    player_ = player.get();
    pending_ = FutureBackedCoroutineTask(std::move(player));
    pending_->Start();
  }

  bool IsPlaying() const {
    if (!player_)
      return false;
    return player_->IsPlaying();
  }

  sr::Value Wait(sr::VM& vm, bool key_skip) {
    if (!player_ || !pending_)
      return MakeResolvedFuture(*vm.gc_);
    player_->wait_key_ = key_skip;
    sr::Future* fut = pending_->MakeFuture(*vm.gc_);
    return sr::Value(fut);
  }

  void Stop() {
    if (!player_)
      return;
    player_->Stop();
    player_ = nullptr;
    pending_.reset();
  }

  std::filesystem::path FindMovie(const std::string& file_name) const {
    if (!asset_scanner_)
      throw std::runtime_error("mov.play requires an asset scanner");

    auto path = asset_scanner_->FindFile(file_name, kMovieExtensions);
    if (!path) {
      throw std::runtime_error("mov.play could not find " + file_name + ": " +
                               path.error().what());
    }
    return path.value();
  }

  sr::VM* vm_ = nullptr;
  System* system_ = nullptr;
  std::shared_ptr<AssetScanner> asset_scanner_;
  MoviePlayer* player_ = nullptr;
  std::optional<FutureBackedCoroutineTask> pending_;
};

}  // namespace

static std::pair<std::string, std::optional<MovieRect>> ParsePlayArgs(
    std::vector<sr::Value> args) {
  CallPacket packet = CallPacket::DecodeFrom(std::move(args));
  if (packet.args.empty())
    throw std::runtime_error("mov.play expects a movie name");

  std::optional<MovieRect> rect;

  std::string file_name = AsString(packet.args[0]);
  if (file_name.empty())
    throw std::runtime_error("mov.play movie name is empty");

  if (packet.args.size() >= 5) {
    rect = {};
    rect->x = RequireInt(packet.args[1], "mov.play x");
    rect->y = RequireInt(packet.args[2], "mov.play y");
    rect->width = RequireInt(packet.args[3], "mov.play width");
    rect->height = RequireInt(packet.args[4], "mov.play height");
  }

  return {std::move(file_name), rect};
}

void BindMovie(SiglusMachine& runtime) {
  sr::VM& vm = *runtime.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  sb::class_<SiglusMovie> movie_cls(m, "Movie", false);
  auto movie = movie_cls.inst("mov", runtime);

  movie.def(
      "play",
      [](SiglusMovie* movie, std::vector<sr::Value> args) {
        auto [file_name, rect] = ParsePlayArgs(std::move(args));
        movie->Play(std::move(file_name), rect);
      },
      sb::vararg);
  movie.def(
      "play_wait",
      [](SiglusMovie* movie, sr::VM& vm, std::vector<sr::Value> args) {
        auto [file_name, rect] = ParsePlayArgs(std::move(args));
        movie->Play(std::move(file_name), rect);
        return movie->Wait(vm, false);
      },
      sb::vararg);
  movie.def(
      "play_waitkey",
      [](SiglusMovie* movie, sr::VM& vm, std::vector<sr::Value> args) {
        auto [file_name, rect] = ParsePlayArgs(std::move(args));
        movie->Play(std::move(file_name), rect, true);
        return movie->Wait(vm, true);
      },
      sb::vararg);
  movie.def("stop", &SiglusMovie::Stop);
}

RLVM_REGISTER(SiglusBindingRegistry, "movie", BindMovie)

}  // namespace libsiglus::binding
