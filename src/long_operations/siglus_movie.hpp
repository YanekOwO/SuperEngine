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

#pragma once

namespace serilang {
class Promise;
}

namespace libsiglus {
struct MovieOperationState;
struct SiglusMachine;
class ExecutionWriter;
class ExecutionReader;
}  // namespace libsiglus

#include "long_operations/long_operation.hpp"
#include "utilities/clock.hpp"

#include <atomic>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class Rect;
class SoundSystem;
class IGraphicsBackend;
class System;
template <class T>
class Queue;

namespace lop {

struct MovieRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// Only decoded frame resources are shared with the engine, not coroutine
// locals.
struct MovieFrame {
  FrameRenderer draw;
};

class SiglusMovieOperation final : public LongOperation {
 public:
  SiglusMovieOperation(System* system,
                       std::shared_ptr<IGraphicsBackend> backend,
                       std::filesystem::path path,
                       MovieRect rect,
                       int volume,
                       bool wait_key);
  ~SiglusMovieOperation() override;
  Task<int> Run(WaitContext& waits) override;
  libsiglus::MovieOperationState CaptureCheckpoint(
      libsiglus::ExecutionWriter& execution) const;
  static std::unique_ptr<LongOperation> RestoreCheckpoint(
      libsiglus::SiglusMachine& machine,
      libsiglus::ExecutionReader& execution,
      const libsiglus::MovieOperationState& state);
  std::weak_ptr<MovieFrame> frame() const { return frame_; }
  bool IsPlaying() const;
  void Stop();
  void SetWaitKey(bool wait_key) { wait_key_ = wait_key; }
  void SetCompletionPromise(std::shared_ptr<serilang::Promise> promise) {
    completion_ = std::move(promise);
  }

 private:
  struct Frame {
    int timestamp;
    std::vector<char> data;
  };

  int CurrentTimeMs() const;
  int UpdatePlaybackTime();
  Rect DestinationRect(const Rect& source) const;
  void SetDecoderException(std::exception_ptr exception);
  void RethrowDecoderException();

  std::filesystem::path path_;
  MovieRect rect_;
  bool wait_key_;
  int volume_ = 255;
  bool playing_ = false;
  bool stopped_ = false;
  bool owns_audio_ = false;
  std::function<void()> on_start_;
  std::shared_ptr<serilang::Promise> completion_;
  std::shared_ptr<Clock> clock_;
  SoundSystem& sound_;
  std::shared_ptr<IGraphicsBackend> backend_;
  Clock::timepoint_t start_;
  std::atomic<int> playback_time_ms_ = 0;
  std::jthread decoder_;
  std::shared_ptr<Queue<Frame>> frame_queue_;
  std::mutex decoder_exception_mutex_;
  std::exception_ptr decoder_exception_;
  std::shared_ptr<MovieFrame> frame_ = std::make_shared<MovieFrame>();
};

}  // namespace lop
